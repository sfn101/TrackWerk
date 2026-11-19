#ifndef POCKETTRACKER_SONGCORE_WAV_WRITER_H
#define POCKETTRACKER_SONGCORE_WAV_WRITER_H

// PCM WAV: `WavStreamWriter` for the render (chunk by chunk, 16-bit), `write_wav` for the sample editor
// (any depth, with slice cue points), and `read_cue_points`.
// Both write "<path>.tmp" and rename on success. ⚠️ Open, remove and rename all go through
// byte_source.h — `std::remove` cannot see URI-addressed storage.

#include <cerrno>
#include <cmath>
#include <limits>
#include <system_error>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>        // _commit
#include <process.h>   // _getpid
#else
#include <unistd.h>    // getpid
#endif
#include "../common/byte_source.h"   // pt_fopen / pt_remove / pt_rename
#include "media_path.h"              // is_wav_file

namespace songcore {

// Clamp, × 32767, TRUNCATE. ⚠️ Rounding would move half the samples of every existing render an LSB.
inline int16_t float_to_int16(float v) {
    if (v < -1.0f) v = -1.0f;
    else if (v > 1.0f) v = 1.0f;
    return static_cast<int16_t>(static_cast<int>(v * 32767.0f));
}

namespace detail {
// Flush stdio and the OS cache to the card before the rename publishes the file (AUDIT 10), so power
// loss can't leave an empty WAV. A descriptor that can't sync (EINVAL: some SAF pipes) is not an error.
inline bool flush_to_disk(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0;
#else
    int r;
    do { r = fsync(fileno(f)); } while (r < 0 && errno == EINTR);
    return r == 0 || errno == EINVAL || errno == EROFS;
#endif
}

// What sits at a path, as far as publishing a WAV over it is concerned. A directory opens with
// "rb" on some systems, so the probe also reads: only a readable file (or an empty one) is FILE.
enum class PathKind { ABSENT, FILE, OTHER };
inline PathKind probe_path(const char* path) {
    errno = 0;
    std::FILE* f = pt_fopen(path, "rb");
    if (!f) {
        // Only a path shown to be missing is ABSENT. Anything else (no read permission, an I/O
        // error) may well be a file: OTHER, so nothing is replaced or restored over it. A storage
        // that reports no reason at all (errno 0) keeps the earlier reading: absent.
        return (errno == 0 || errno == ENOENT || errno == ENOTDIR) ? PathKind::ABSENT : PathKind::OTHER;
    }
    unsigned char byte = 0;
    const bool readable = std::fread(&byte, 1, 1, f) == 1 || std::feof(f);
    std::fclose(f);
    return readable ? PathKind::FILE : PathKind::OTHER;
}

// The writer's own backup suffix: nothing else makes "<path>.ptbak", so finding one is finding ours.
constexpr const char* kBackupSuffix = ".ptbak";

// Set by publish_replacing when both the publish and the restore failed: the original is then
// safe at "<path>.ptbak" (and recover_interrupted_save puts it back on the next load).
inline bool& last_publish_kept_backup() { static bool kept = false; return kept; }
// Set when a save found a foreign "<path>.ptbak" (an earlier interrupted save's original) and moved
// it to the visible "<name>_ptbak.wav" beside it, so it shows in the WAV browser and stays.
inline bool& last_publish_rescued_backup() { static bool rescued = false; return rescued; }
// Set when a publish that must not replace (a numbered export) found its target taken meanwhile.
inline bool& last_publish_target_taken() { static bool taken = false; return taken; }

// The visible name a foreign backup is kept under: "take.wav" → "take_ptbak.wav".
inline std::string rescue_path_for(const std::string& path) {
    const size_t n = path.size();
    const bool wav = n >= 4 && (path[n - 4] == '.') && (path[n - 3] | 32) == 'w' &&
                     (path[n - 2] | 32) == 'a' && (path[n - 1] | 32) == 'v';
    return wav ? path.substr(0, n - 4) + "_ptbak.wav" : path + "_ptbak.wav";
}

// A temp opened fresh for this save only: whatever sits at its name (a stale temp, a link to some
// other file) is removed first — the link itself, never what it points to — and a local temp is
// created exclusively ("x"), so the write can only ever land in a new file of its own.
// The temp name for one save. Local: "<dir>/.ptw-<pid>-<n>.tmp" — beside the target (so the rename
// stays atomic), unique to this process and write (two app instances never share or remove each
// other's temp), and short whatever the target's name (a 251-byte name saves as before).
// URI storage (SAF) keeps "<path>.tmp": one app owns that tree.
constexpr const char* kTempPrefix = ".ptw-";
inline std::string temp_path_for(const std::string& path) {
    if (pt_path_is_uri(path.c_str())) return path + ".tmp";
    static std::atomic<unsigned> counter{0};
#if defined(_WIN32)
    const long pid = static_cast<long>(_getpid());
#else
    const long pid = static_cast<long>(::getpid());
#endif
    const size_t slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    return dir + kTempPrefix + std::to_string(pid) + "-" + std::to_string(counter.fetch_add(1)) + ".tmp";
}

// "<digits>-<digits>": exactly the pid and counter temp_path_for writes, nothing looser.
inline bool parse_temp_tag(const std::string& tag, long& pid) {
    const size_t dash = tag.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= tag.size()) return false;
    if (tag.find('-', dash + 1) != std::string::npos) return false;
    for (size_t k = 0; k < tag.size(); ++k)
        if (k != dash && (tag[k] < '0' || tag[k] > '9')) return false;
    // Only values this writer can produce: a pid that fits pid_t exactly, a counter that fits
    // `unsigned` (at most 10 digits each, so the parse cannot overflow either).
    const std::string pidText = tag.substr(0, dash), counterText = tag.substr(dash + 1);
    if (pidText.size() > 10 || counterText.size() > 10) return false;
    const long long pidValue = std::strtoll(pidText.c_str(), nullptr, 10);
    const unsigned long long counter = std::strtoull(counterText.c_str(), nullptr, 10);
#if defined(_WIN32)
    const long long pidMax = std::numeric_limits<int>::max();
#else
    const long long pidMax = std::numeric_limits<pid_t>::max();
#endif
    if (pidValue <= 0 || pidValue > pidMax || counter > std::numeric_limits<unsigned>::max()) return false;
    pid = static_cast<long>(pidValue);
    return true;
}

// Temps a crashed earlier save left beside `path` ("<path>.<pid>-<n>.tmp" of a process that no longer
// runs) are removed; a live writer's temp — another instance saving right now — is never touched.
inline void remove_orphan_temps(const std::string& path) {
    if (pt_path_is_uri(path.c_str())) return;
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::path(path).parent_path();
    const std::string prefix = kTempPrefix;
    for (const auto& entry : std::filesystem::directory_iterator(folder.empty() ? "." : folder, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - 4, 4, ".tmp") != 0) continue;
        long pid = 0;
        if (!parse_temp_tag(name.substr(prefix.size(), name.size() - prefix.size() - 4), pid)) continue;
#if defined(_WIN32)
        if (pid == static_cast<long>(_getpid())) continue;
#else
        if (pid == static_cast<long>(::getpid())) continue;
#endif
        if (!pt_process_gone(pid)) continue;   // alive, or unsure
        if (entry.is_regular_file(ec) && !entry.is_symlink(ec)) std::filesystem::remove(entry.path(), ec);
    }
}

inline std::FILE* open_fresh_temp(const char* tmp, const std::string& path) {
    try { remove_orphan_temps(path); } catch (...) {}   // housekeeping only; never blocks a save
    // Only a file (or a link to one) is cleared; a folder, or anything unreadable, is left alone and
    // the exclusive create below then fails — the save is refused, nothing else touched.
    if (probe_path(tmp) == PathKind::FILE) pt_remove(tmp);
    return pt_fopen(tmp, pt_path_is_uri(tmp) ? "wb" : "wbx");
}

// Set when a save was refused because "<path>.ptbak" already holds an earlier file (the only copy of
// it, possibly): the writer never deletes a backup it did not make in this same save.
inline bool& last_publish_backup_in_way() { static bool inWay = false; return inWay; }

// Put the finished `tmp` at `path`; `bak` is "<path>.ptbak", built before the file was written so
// nothing here allocates. ⚠️ The target cannot simply be removed first and the rename hoped for: a
// failed rename would then leave neither file. And it must be gone before the rename (`rename()`
// fails on an existing target on Windows, SAF's `renameDocument` de-duplicates to `song (1).wav`).
// So the old file is moved ASIDE, the temp renamed into place, the old one renamed back if that
// fails, and the backup dropped only once the new file is there. Never touches what is not ours:
// a directory (or anything unreadable) at the target refuses the save.
inline bool publish_replacing(const char* tmp, const char* path, const char* bak, const char* rescue,
                              bool noReplace = false) {
    last_publish_target_taken() = false;
    if (noReplace) {
        // A name chosen as free (a numbered export) is claimed only if it still is: never replaced.
#if !defined(_WIN32)
        if (!pt_path_is_uri(path)) {
            // link() fails with EEXIST on a taken name — atomically, also against another instance.
            if (::link(tmp, path) == 0) {
                pt_remove(tmp);
                return true;
            }
            if (errno == EEXIST) {
                last_publish_target_taken() = true;
                pt_remove(tmp);
                return false;
            }
            // No hard links here (FAT32/exFAT cards: ENOTSUP, EPERM, …): the name is checked, then
            // the temp renamed onto it. Not atomic against a second app instance — out of scope, one
            // app runs on the device — but a file there before the save is never replaced.
            if (probe_path(path) != PathKind::ABSENT) {
                last_publish_target_taken() = true;
                pt_remove(tmp);
                return false;
            }
            if (pt_rename(tmp, path) != 0) {
                pt_remove(tmp);
                return false;
            }
            return true;
        }
#endif
        if (probe_path(path) != PathKind::ABSENT) {
            last_publish_target_taken() = true;
            pt_remove(tmp);
            return false;
        }
    }
    last_publish_kept_backup() = false;
    last_publish_backup_in_way() = false;
    last_publish_rescued_backup() = false;
    const PathKind target = probe_path(path);
    if (target == PathKind::OTHER) {   // a folder, or a file that cannot be read: never replaced
        pt_remove(tmp);
        return false;
    }
    // A foreign backup beside the target (an earlier save's original) is never deleted: when it is a
    // WAV and its visible name is free, it moves there and stays; otherwise it is left where it is.
    const auto rescue_backup = [&] {
        if (probe_path(bak) == PathKind::FILE && is_wav_file(bak) &&
            probe_path(rescue) == PathKind::ABSENT && pt_rename(bak, rescue) == 0)
            last_publish_rescued_backup() = true;
    };
#if !defined(_WIN32)
    if (!pt_path_is_uri(path)) {
        // A local POSIX rename replaces the target atomically: the old file stays until the new one
        // is in its place, so no backup is made. A foreign backup is moved aside only AFTER the
        // publish: until the new file stands, it stays where the load finds it.
        if (pt_rename(tmp, path) != 0) {
            pt_remove(tmp);
            return false;
        }
        rescue_backup();
        return true;
    }
#endif
    rescue_backup();   // SAF/Windows: it must leave "<path>.ptbak" free before the move aside
    const PathKind backup = probe_path(bak);
    // Storage that cannot rename over an existing file (SAF, Windows): the old file is moved aside
    // to "<path>.ptbak" first, put back if the publish fails, and dropped only once the new file is
    // there. A backup still in the way (not a WAV, or its visible name taken) refuses the save.
    if (backup != PathKind::ABSENT) {
        pt_remove(tmp);
        last_publish_backup_in_way() = true;
        return false;
    }
    bool movedAside = false;
    if (target == PathKind::FILE) {
        if (pt_rename(path, bak) != 0) {   // cannot move it: replace nothing
            pt_remove(tmp);
            return false;
        }
        movedAside = true;
    }
    if (pt_rename(tmp, path) != 0) {
        pt_remove(tmp);
        if (movedAside && pt_rename(bak, path) != 0) last_publish_kept_backup() = true;
        return false;
    }
    if (movedAside) pt_remove(bak);   // only the backup this save made
    return true;
}
}  // namespace detail

// An interrupted save (or one whose restore failed) can leave the original only at "<path>.ptbak".
// With nothing at `path` itself, that backup is unambiguously the file that belongs there: put it
// back. Called before a WAV is loaded by path. True when something was restored.
inline bool recover_interrupted_save(const std::string& path) {
    try {
        const std::string bak = path + detail::kBackupSuffix;
        if (detail::probe_path(path.c_str()) != detail::PathKind::ABSENT) return false;
        if (detail::probe_path(bak.c_str()) != detail::PathKind::FILE || !is_wav_file(bak)) return false;
        return pt_rename(bak.c_str(), path.c_str()) == 0;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

class WavStreamWriter {
  public:
    // `noReplace`: the target is a name chosen as free (a numbered export) — publish only if it still
    // is, never over a file that appeared meanwhile.
    WavStreamWriter(const std::string& path, int sampleRate, int channels = 2, bool noReplace = false)
        : sampleRate_(sampleRate), channels_(channels) {
        noReplace_ = noReplace;
        // Every path the publish needs is built here, before anything is written: no allocation can
        // then fail between a finished temp and its rename. Without the memory, not open.
        detail::last_publish_kept_backup() = false;   // this write's own outcome only
        detail::last_publish_backup_in_way() = false;
        detail::last_publish_rescued_backup() = false;
        detail::last_publish_target_taken() = false;
        try {
            path_ = path;
            tmpPath_ = detail::temp_path_for(path);
            bakPath_ = path + detail::kBackupSuffix;
            rescuePath_ = detail::rescue_path_for(path);
        } catch (const std::bad_alloc&) {
            return;
        }
        file_ = detail::open_fresh_temp(tmpPath_.c_str(), path_);
        if (!file_) return;
        uint8_t header[44];
        build_header(header, 0);
        // Placeholder sizes, patched in finish(). No room for 44 bytes fails here as "could not open".
        if (std::fwrite(header, 1, sizeof(header), file_) != sizeof(header)) close_and_remove();
    }

    ~WavStreamWriter() { abort(); }

    WavStreamWriter(const WavStreamWriter&) = delete;
    WavStreamWriter& operator=(const WavStreamWriter&) = delete;

    bool is_open() const { return file_ != nullptr; }

    // Append interleaved float frames ([L0, R0, L1, R1, …]), converted to 16-bit.
    void append_interleaved(const float* data, int frames) {
        if (!file_ || frames <= 0) return;
        const int samples = frames * channels_;
        // Little-endian bytes assembled explicitly (WAV is little-endian by spec, whatever the host).
        // One fwrite per chunk.
        try {
            buf_.resize(static_cast<size_t>(samples) * 2);
        } catch (const std::bad_alloc&) {   // a write that cannot happen: finish() drops the temp
            writeFailed_ = true;
            return;
        }
        for (int i = 0; i < samples; ++i) {
            const uint16_t u = static_cast<uint16_t>(float_to_int16(data[i]));
            buf_[static_cast<size_t>(i) * 2 + 0] = static_cast<uint8_t>(u & 0xFF);
            buf_[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
        }
        // ⚠️ Sticky: a card filling mid-render lands here, and finish() must not publish a short file.
        if (std::fwrite(buf_.data(), 1, buf_.size(), file_) != buf_.size()) {
            writeFailed_ = true;
            return;
        }
        framesWritten_ += frames;
    }

    // Patch the RIFF/data sizes, close, rename into place. False on I/O failure or a > 2 GB data size;
    // the temp is removed either way.
    bool finish() {
        if (!file_) return false;

        const int64_t dataSize = framesWritten_ * bytes_per_frame();
        // A failed append makes every count below a lie: drop the temp.
        if (writeFailed_ || dataSize > static_cast<int64_t>(INT32_MAX) - 44) {
            close_and_remove();
            return false;
        }

        uint8_t header[44];
        build_header(header, dataSize);
        if (std::fseek(file_, 0, SEEK_SET) != 0 ||
            std::fwrite(header, 1, sizeof(header), file_) != sizeof(header)) {
            close_and_remove();
            return false;
        }

        // ⚠️ `fclose` is a WRITE: it flushes stdio's buffer, and for a small render it is the ONLY place
        // a full card can report. Unchecked, the rename publishes a short file.
        const bool synced = detail::flush_to_disk(file_);
        const bool closed = (std::fclose(file_) == 0) && synced;
        file_ = nullptr;
        if (!closed) {
            pt_remove(tmpPath_.c_str());
            return false;
        }

        // The old file is kept until the new one is in place (see detail::publish_replacing).
        return detail::publish_replacing(tmpPath_.c_str(), path_.c_str(), bakPath_.c_str(),
                                         rescuePath_.c_str(), noReplace_);
    }

    // Discard everything written so far. Safe any time; the destructor calls it, so no early return
    // leaves a .tmp behind.
    void abort() {
        if (!file_) return;
        close_and_remove();
    }


  private:
    int bytes_per_frame() const { return channels_ * 2; }   // 16-bit

    void close_and_remove() {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
        pt_remove(tmpPath_.c_str());
    }

    // The standard 44-byte RIFF/fmt/data header.
    void build_header(uint8_t* h, int64_t dataSize) const {
        const uint32_t byteRate   = static_cast<uint32_t>(sampleRate_ * bytes_per_frame());
        const uint16_t blockAlign = static_cast<uint16_t>(bytes_per_frame());

        std::memcpy(h + 0, "RIFF", 4);
        put_u32(h + 4, static_cast<uint32_t>(36 + dataSize));
        std::memcpy(h + 8, "WAVE", 4);
        std::memcpy(h + 12, "fmt ", 4);
        put_u32(h + 16, 16);                                    // PCM fmt chunk size
        put_u16(h + 20, 1);                                     // AudioFormat = PCM
        put_u16(h + 22, static_cast<uint16_t>(channels_));
        put_u32(h + 24, static_cast<uint32_t>(sampleRate_));
        put_u32(h + 28, byteRate);
        put_u16(h + 32, blockAlign);
        put_u16(h + 34, 16);                                    // bits per sample
        std::memcpy(h + 36, "data", 4);
        put_u32(h + 40, static_cast<uint32_t>(dataSize));
    }

    static void put_u16(uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    static void put_u32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    }

    std::string path_;
    std::string tmpPath_;
    std::string bakPath_;
    std::string rescuePath_;
    bool noReplace_ = false;
    int      sampleRate_ = 44100;
    int      channels_   = 2;
    std::FILE* file_     = nullptr;
    int64_t  framesWritten_ = 0;
    std::vector<uint8_t> buf_;   // reused across chunks — no per-chunk allocation
    // Sticky: set by any short write, read by finish() — the only way a mid-render disk failure is
    // reported.
    bool     writeFailed_ = false;
};

// ─── The sample editor's writer: whole buffers, plus the `cue ` chunk ────────────────────────────

namespace detail {

inline void wav_put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
inline void wav_put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
inline void wav_put_tag(std::vector<uint8_t>& b, const char* tag) {
    b.insert(b.end(), tag, tag + 4);
}

// One sample at the file's depth. 16 goes through `float_to_int16`; others ROUND to 2^(bits−1), so an
// 8- or 24-bit buffer round-trips exactly. 8-bit WAV is unsigned, centred on 128.
inline void wav_put_sample(std::vector<uint8_t>& b, float v, int bits, bool isFloat) {
    if (bits == 32 && isFloat) {
        uint32_t u = 0;
        std::memcpy(&u, &v, sizeof(u));
        wav_put_u32(b, u);
        return;
    }
    if (bits == 16) {
        wav_put_u16(b, static_cast<uint16_t>(float_to_int16(v)));
        return;
    }
    if (v < -1.0f) v = -1.0f;
    else if (v > 1.0f) v = 1.0f;
    const double  scale = static_cast<double>(int64_t{1} << (bits - 1));
    const int64_t top   = static_cast<int64_t>(scale) - 1;
    int64_t q = static_cast<int64_t>(std::floor(static_cast<double>(v) * scale + 0.5));
    if (q > top) q = top;
    if (q < -static_cast<int64_t>(scale)) q = -static_cast<int64_t>(scale);
    if (bits == 8) {
        b.push_back(static_cast<uint8_t>(q + 128));
        return;
    }
    const uint32_t u = static_cast<uint32_t>(static_cast<int32_t>(q));
    b.push_back(static_cast<uint8_t>(u & 0xFF));
    b.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    if (bits == 32) b.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
}

/** Write `bytes` to `path` via "<path>.tmp" + rename, so a failed write leaves no partial file. */
inline bool wav_write_atomic(const std::string& path, const std::vector<uint8_t>& bytes) {
    last_publish_kept_backup() = false;   // this write's own outcome only
    last_publish_backup_in_way() = false;
    last_publish_rescued_backup() = false;
    last_publish_target_taken() = false;
    // Both helper paths first: nothing may need memory once the temp is written.
    std::string tmp, bak, rescue;
    try {
        tmp = temp_path_for(path);
        bak = path + kBackupSuffix;
        rescue = rescue_path_for(path);
    } catch (const std::bad_alloc&) {
        return false;
    }
    std::FILE* f = open_fresh_temp(tmp.c_str(), path);
    if (!f) return false;
    const size_t written = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
    // ⚠️ `fclose` checked, and on its own line: a full card surfaces in this flush or nowhere.
    const bool   synced  = flush_to_disk(f);
    const bool   flushed = (std::fclose(f) == 0) && synced;
    const bool   ok      = (written == bytes.size()) && flushed;
    if (!ok) {
        pt_remove(tmp.c_str());
        return false;
    }
    return publish_replacing(tmp.c_str(), path.c_str(), bak.c_str(), rescue.c_str());   // the original stays until replaced
}

}  // namespace detail

// A PCM WAV at 8/16/24/32 bits (32 float with `isFloat`; other values write 16), with an optional
// `cue ` chunk. `right` is read only when `channels == 2`. Chunk order is pinned by a golden.
inline bool write_wav(const std::string& path, const std::vector<float>& left,
                      const std::vector<float>& right, int sampleRate,
                      const std::vector<int>& cuePoints = {}, int channels = 2, int bits = 16,
                      bool isFloat = false) {
    if (channels == 2 && left.size() != right.size()) return false;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) bits = 16;
    if (bits != 32) isFloat = false;

    const int64_t frames        = static_cast<int64_t>(left.size());
    const int     numChannels   = (channels < 1) ? 1 : (channels > 2 ? 2 : channels);
    const int     blockAlign    = numChannels * (bits / 8);
    const int64_t dataSize      = frames * blockAlign;
    const int64_t dataPadding   = dataSize & 1;

    // WAV's size fields are 32-bit. A sample big enough to overflow one is not a sample.
    if (dataSize > static_cast<int64_t>(INT32_MAX)) return false;

    const size_t  numCue           = cuePoints.size();
    const int64_t cueChunkDataSize = numCue > 0 ? static_cast<int64_t>(4 + numCue * 24) : 0;
    const int64_t cueChunkBytes    = numCue > 0 ? 8 + cueChunkDataSize : 0;
    const int64_t riffContentSize  = 36 + dataSize + dataPadding + cueChunkBytes;

    std::vector<uint8_t> b;
    b.reserve(static_cast<size_t>(8 + riffContentSize));

    // RIFF header (12 bytes)
    detail::wav_put_tag(b, "RIFF");
    detail::wav_put_u32(b, static_cast<uint32_t>(riffContentSize));
    detail::wav_put_tag(b, "WAVE");

    // fmt chunk (24 bytes)
    detail::wav_put_tag(b, "fmt ");
    detail::wav_put_u32(b, 16);                                                    // PCM fmt size
    detail::wav_put_u16(b, isFloat ? 3 : 1);                                       // AudioFormat: PCM / float
    detail::wav_put_u16(b, static_cast<uint16_t>(numChannels));
    detail::wav_put_u32(b, static_cast<uint32_t>(sampleRate));
    detail::wav_put_u32(b, static_cast<uint32_t>(sampleRate * blockAlign));        // byte rate
    detail::wav_put_u16(b, static_cast<uint16_t>(blockAlign));
    detail::wav_put_u16(b, static_cast<uint16_t>(bits));                           // bits per sample

    // data chunk (8 + dataSize)
    detail::wav_put_tag(b, "data");
    detail::wav_put_u32(b, static_cast<uint32_t>(dataSize));
    for (int64_t i = 0; i < frames; ++i) {
        const size_t k = static_cast<size_t>(i);
        detail::wav_put_sample(b, left[k], bits, isFloat);
        if (numChannels == 2) detail::wav_put_sample(b, right[k], bits, isFloat);
    }
    if (dataPadding) b.push_back(0);  // A mono odd-length PCM8 region still needs a word-aligned next chunk.

    // cue chunk (8 + 4 + n*24), one point per slice boundary
    if (numCue > 0) {
        detail::wav_put_tag(b, "cue ");
        detail::wav_put_u32(b, static_cast<uint32_t>(cueChunkDataSize));
        detail::wav_put_u32(b, static_cast<uint32_t>(numCue));
        for (size_t i = 0; i < numCue; ++i) {
            const uint32_t frame = static_cast<uint32_t>(cuePoints[i]);
            detail::wav_put_u32(b, static_cast<uint32_t>(i + 1));   // ID (1-based)
            detail::wav_put_u32(b, frame);                          // position (play order)
            detail::wav_put_tag(b, "data");                         // the chunk it points into
            detail::wav_put_u32(b, 0);                              // chunk start (0 = unknown)
            detail::wav_put_u32(b, 0);                              // block start
            detail::wav_put_u32(b, frame);                          // sample offset within `data`
        }
    }

    return detail::wav_write_atomic(path, b);
}

// ⚠️ A STEREO file with the data in both channels — the format every chop has always had.
inline bool write_wav_mono(const std::string& path, const std::vector<float>& samples, int sampleRate,
                           const std::vector<int>& cuePoints = {}, int bits = 16, bool isFloat = false) {
    return write_wav(path, samples, samples, sampleRate, cuePoints, /*channels=*/2, bits, isFloat);
}

// The frames in a WAV's `cue ` chunk, frame 0 excluded (it would make an empty slice 0). Seeks past
// the other chunks — it runs on every sample load.
inline std::vector<int> read_cue_points(const std::string& path) {
    std::vector<int> frames;

    std::FILE* f = pt_fopen(path.c_str(), "rb");
    if (!f) return frames;
    // Closed on every way out, an allocation failure in the cue list included.
    struct Closer {
        std::FILE*& file;
        ~Closer() { if (file) std::fclose(file); }
    } closer{f};

    auto close_and_return = [&](std::vector<int> out) {
        std::fclose(f);
        f = nullptr;
        return out;
    };

    if (std::fseek(f, 0, SEEK_END) != 0) return close_and_return({});
    const long fileLen = std::ftell(f);
    if (fileLen < 12 || std::fseek(f, 0, SEEK_SET) != 0) return close_and_return({});

    auto read_u32 = [&](uint32_t& out) -> bool {
        uint8_t p[4];
        if (std::fread(p, 1, 4, f) != 4) return false;
        out = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
              (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        return true;
    };
    auto read_tag = [&](char* tag) -> bool { return std::fread(tag, 1, 4, f) == 4; };

    char     riff[4], wave[4];
    uint32_t riffSize = 0;
    if (!read_tag(riff) || !read_u32(riffSize) || !read_tag(wave)) return close_and_return({});
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
        return close_and_return({});

    // Walk the 8-byte chunk headers, reading only the body of "cue ".
    while (std::ftell(f) + 8 <= fileLen) {
        char     id[4];
        uint32_t chunkSize = 0;
        if (!read_tag(id) || !read_u32(chunkSize)) break;

        // A size with the top bit set is a > 2 GB chunk or garbage; stop, or the skip below could seek
        // backwards and loop forever.
        if (chunkSize > static_cast<uint32_t>(INT32_MAX)) break;

        if (std::memcmp(id, "cue ", 4) == 0) {
            const long remaining = fileLen - std::ftell(f);
            if (remaining < 4) break;
            uint32_t count = 0;
            if (!read_u32(count)) break;

            // Trust the chunk, not the count field: a truncated file can claim more points than it holds.
            const long  bodyLeft = (static_cast<long>(chunkSize) < remaining ? static_cast<long>(chunkSize)
                                                                             : remaining) - 4;
            const uint32_t maxPoints = static_cast<uint32_t>(bodyLeft / 24);
            if (count > maxPoints) count = maxPoints;

            for (uint32_t i = 0; i < count; ++i) {
                uint32_t cueId = 0, position = 0, dataTag = 0, chunkStart = 0, blockStart = 0, offset = 0;
                if (!read_u32(cueId) || !read_u32(position) || !read_u32(dataTag) ||
                    !read_u32(chunkStart) || !read_u32(blockStart) || !read_u32(offset))
                    break;
                if (position > 0) frames.push_back(static_cast<int>(position));
            }
            return close_and_return(frames);
        }

        // Skip the body, padded to an even boundary (the RIFF spec's word alignment).
        const long skip = static_cast<long>(chunkSize) + (chunkSize & 1u);
        if (std::ftell(f) + skip > fileLen) break;
        if (std::fseek(f, skip, SEEK_CUR) != 0) break;
    }

    return close_and_return(frames);
}

}  // namespace songcore

#endif  // POCKETTRACKER_SONGCORE_WAV_WRITER_H
