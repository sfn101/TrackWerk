#include "ui/project_actions.h"
#include "ui/source_credits.h"
#include "songcore/wav_writer.h"   // the writer's backup suffix

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "common/byte_source.h"   // pt_fopen / pt_remove / pt_rename
#include "songcore/project_io.h"
#include "songcore/midi_export.h"
#include "songcore/render.h"
#include "ui/lifecycle.h"

namespace pt::ui {

namespace {

// ⚠️ Every file a user would miss goes through `FileSystem::write_file` (temp, rename), so a pulled
// card mid-save leaves the old file whole — never a truncating `ofstream`.
bool write_project(const songcore::SongcoreHost& host, FileSystem& fs, const std::string& path) {
    return fs.write_file(path, songcore::serialize_project(host.project()));
}

std::string pad4(int v) {
    std::string s = std::to_string(v);
    while (s.size() < 4) s.insert(s.begin(), '0');
    return s;
}

}  // namespace

std::string unique_render_path(FileSystem& fs, const std::string& dir, const std::string& safeName) {
    for (int index = 1; index < 10000; ++index) {
        const std::string path = dir + "/" + safeName + "_" + pad4(index) + ".wav";
        // A number whose file survives only as the writer's backup (an interrupted replace) is taken
        // too: a new export there would supersede, and so delete, the only copy of that mix.
        if (!fs.file_exists(path) && !fs.file_exists(path + songcore::detail::kBackupSuffix)) return path;
    }
    return std::string();   // all 9 999 taken: no name — never the last one again
}

// ─── SAVE ────────────────────────────────────────────────────────────────────────────────────────

std::string project_backup_path(FileSystem& fs, const std::string& stem, int generation) {
    return fs.make_app_folder("Backups") + "/" + stem + "_bak" + std::to_string(generation) + ".ptp";
}

namespace {

// The file SAVE is about to replace is kept first: Backups/<NAME>_bak1.ptp is the previous version,
// _bak2 the one before, up to kProjectBackupsKept. Best effort: a failed backup (a full card, a folder
// that cannot be made) never stops the save. An identical file, or one the newest backup already holds,
// is not kept twice, so saving over and over without changes does not push the real history out.
void keep_previous_version(FileSystem& fs, const std::string& path, const std::string& stem,
                           const std::string& incoming) {
    std::string old;
    if (!fs.file_exists(path) || !fs.read_file(path, old) || old.empty() || old == incoming) return;
    std::string newest;
    const std::string first = project_backup_path(fs, stem, 1);
    if (fs.file_exists(first) && fs.read_file(first, newest) && newest == old) return;
    for (int generation = kProjectBackupsKept; generation >= 2; --generation) {
        std::string older;
        const std::string from = project_backup_path(fs, stem, generation - 1);
        if (fs.file_exists(from) && fs.read_file(from, older)) fs.write_file(project_backup_path(fs, stem, generation), older);
    }
    fs.write_file(first, old);
}

}  // namespace

static std::string project_file(const songcore::SongcoreHost& host) {
    return songcore::project_file_name(host.project().name) + ".ptp";
}

// A song opened from a file saves into that file's folder (as <name>.ptp, so a renamed song stays
// beside it); a new, recovered or imported song has no path and goes to Projects.
std::string project_save_path(const songcore::SongcoreHost&, FileSystem& fs, const AppState& s,
                              const std::string& name) {
    const std::string folder = s.projectPath.empty() ? std::string() : fs.parent_path(s.projectPath);
    return (folder.empty() ? fs.projects_directory() : folder) + "/" + songcore::project_file_name(name) + ".ptp";
}

static std::string project_save_path(const songcore::SongcoreHost& host, FileSystem& fs, const AppState& s) {
    return project_save_path(host, fs, s, host.project().name);
}

ActionResult save_project(songcore::SongcoreHost& host, FileSystem& fs, AppState& s) {
    std::string path = project_save_path(host, fs, s);
    std::string message = "SAVED";

    const bool existed = fs.file_exists(path);

    const std::string text = songcore::serialize_project(host.project());
    keep_previous_version(fs, path, songcore::project_file_name(host.project().name), text);
    if (!fs.write_file(path, text)) {
        // The song's own file exists but can't be replaced (on Windows: OneDrive or a virus scanner
        // holds it): keep it untouched and save beside it (AUDIT 8). The path stays the original, so
        // the next SAVE tries it again without asking.
        if (existed) {
            // Numbered, never over an older backup: -backup, -backup-2, -backup-3…
            const std::string stem = songcore::project_file_name(host.project().name) + "-backup";
            std::string backupName = stem + ".ptp";
            for (int n = 2; n < 10000 && fs.file_exists(fs.parent_path(path) + "/" + backupName); ++n)
                backupName = stem + "-" + std::to_string(n) + ".ptp";
            if (!fs.file_exists(fs.parent_path(path) + "/" + backupName) &&
                write_project(host, fs, fs.parent_path(path) + "/" + backupName)) {
                s.savedProjectVersion = s.projectVersion;
                s.projectPath         = path;
                autosave_clear(fs);
                fs.forget_listing(fs.parent_path(path));
                return ActionResult{true, "FILE LOCKED: SAVED AS " + backupName};
            }
            // The backup failed too: the folder itself refuses writes.
        }
        // Its own folder refused the write (read-only, or no permission on Android): Projects instead.
        const std::string fallback = fs.projects_directory() + "/" + project_file(host);
        if (fallback == path) return ActionResult{false, "SAVE FAILED"};
        // Never silently over another song there: that question was asked about the other folder.
        if (fs.file_exists(fallback)) return ActionResult{false, "FOLDER READ-ONLY: NAME TAKEN IN PROJECTS"};
        if (!write_project(host, fs, fallback)) return ActionResult{false, "SAVE FAILED"};
        path    = fallback;
        message = "FOLDER READ-ONLY: SAVED IN PROJECTS";
    }

    // Clean, and the autosave goes with it (its pending deadline re-checks the dirty flag).
    s.savedProjectVersion = s.projectVersion;
    s.projectPath         = path;
    autosave_clear(fs);

    return ActionResult{true, message};
}

// ─── EXPORT > MIDI ───────────────────────────────────────────────────────────────────────────────

ActionResult export_midi(const songcore::SongcoreHost& host, FileSystem& fs) {
    songcore::MidiExportReport report;
    const std::string bytes = songcore::export_midi_file(host.project(), &report);
    if (bytes.empty()) return ActionResult{false, "NO NOTES TO EXPORT"};
    const std::string dir = fs.renders_directory();
    const std::string name = songcore::project_file_name(host.project().name);
    std::string path;
    for (int index = 1; index < 10000 && path.empty(); ++index) {
        const std::string candidate = dir + "/" + name + "_" + pad4(index) + ".mid";
        if (!fs.file_exists(candidate)) path = candidate;
    }
    if (path.empty()) return ActionResult{false, "EXPORT FAILED: NO FREE NUMBER"};
    if (!fs.write_file(path, bytes)) return ActionResult{false, "EXPORT FAILED"};
    return ActionResult{true, "MIDI: " + std::to_string(report.notes) + " NOTES, " + std::to_string(report.tracks) +
                                  (report.tracks == 1 ? " TRACK" : " TRACKS") +
                                  (report.stepsWithEffects > 0 ? " (FX NOT INCLUDED)" : "")};
}

// ─── COLLECT ─────────────────────────────────────────────────────────────────────────────────────

std::string collect_folder_name(const songcore::SongcoreHost& host) {
    return songcore::project_file_name(host.project().name);
}

bool collect_folder_exists(FileSystem& fs, const std::string& folder) {
    return fs.file_exists(fs.songs_directory() + "/" + folder);
}

std::string collect_free_folder_name(FileSystem& fs, const std::string& folder) {
    for (int n = 2; n < 1000; ++n) {
        const std::string name = folder + "_" + std::to_string(n);
        if (!collect_folder_exists(fs, name)) return name;
    }
    return folder + "_999";
}

namespace {

// For comparing two spellings of one path: one kind of slash, one case.
std::string comparable_path(std::string p) {
    for (char& c : p)
        c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}

// One slash kind, case kept: two spellings that may still be different files.
std::string slashed_path(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

// Do two paths name ONE file? The same spelling does. Spellings that differ only in case are asked
// of the filesystem: "Kick.wav" and "kick.wav" are one file on FAT, macOS and Windows, two on Linux.
// A path it cannot answer for (a SAF document, a vanished file) counts as a different file — the
// cautious answer: at worst one more copy or one more question, never two files merged into one.
// Spellings that differ beyond case are different without a filesystem call: COLLECT compares every
// pair, and a stat per pair on a slow card is not worth catching a symlink (one extra copy).
bool same_file(const std::string& a, const std::string& b) {
    if (slashed_path(a) == slashed_path(b)) return true;
    if (comparable_path(a) != comparable_path(b)) return false;
    std::error_code ec;
    const bool same = std::filesystem::equivalent(std::filesystem::path(a), std::filesystem::path(b), ec);
    return !ec && same;
}

// Is `path` inside the folder `dir` (given with a trailing '/')? By spelling, then — where only the
// case differs — by asking the filesystem whether the folder part is the same folder.
bool inside_folder(const std::string& path, const std::string& dir) {
    const std::string p = slashed_path(path), d = slashed_path(dir);
    if (p.size() <= d.size()) return false;
    if (p.compare(0, d.size(), d) == 0) return true;
    if (comparable_path(p.substr(0, d.size())) != comparable_path(d)) return false;
    return same_file(p.substr(0, d.size() - 1), d.substr(0, d.size() - 1));
}

}  // namespace

bool save_replaces_other(const songcore::SongcoreHost& host, FileSystem& fs, const AppState& s) {
    return save_would_replace_other_song(fs, s, project_save_path(host, fs, s, host.project().name));
}

std::string free_project_name(FileSystem& fs, const std::string& folder, const std::string& name) {
    constexpr size_t NAME_MAX_CHARS = 20;   // the NAME row's columns
    for (int n = 2; n <= 999; ++n) {
        const std::string suffix = "_" + std::to_string(n);
        const std::string base   = name.substr(0, NAME_MAX_CHARS - suffix.size());
        const std::string candidate = base + suffix;
        if (!fs.file_exists(folder + "/" + songcore::project_file_name(candidate) + ".ptp"))
            return candidate;
    }
    return std::string();
}

std::string free_project_name(FileSystem& fs, const std::string& name) {
    return free_project_name(fs, fs.projects_directory(), name);
}

namespace {

// Bytes in the file, or 0 when it cannot be measured (it then adds nothing to the bar).
int64_t media_file_bytes(const std::string& path) {
    std::FILE* f = pt_fopen(path.c_str(), "rb");
    if (!f) return 0;
    const bool    seeked = std::fseek(f, 0, SEEK_END) == 0;
    const int64_t bytes  = seeked ? static_cast<int64_t>(std::ftell(f)) : 0;
    std::fclose(f);
    return bytes > 0 ? bytes : 0;
}

// In pieces, so `onBytes` can move the bar inside a big file, and through "<dest>.tmp" + rename, so a
// failed copy leaves no partial file. ⚠️ Through pt_fopen, not `FileSystem::copy_file`: on Android
// that reads the whole file into memory first.
bool copy_in_pieces(const std::string& from, const std::string& to,
                    const std::function<void(int64_t)>& onBytes) {
    std::FILE* in = pt_fopen(from.c_str(), "rb");
    if (!in) return false;
    const std::string tmp = to + ".tmp";
    std::FILE* out = pt_fopen(tmp.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }

    std::vector<char> buf(256 * 1024);
    bool ok = true;
    for (;;) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n == 0) break;
        if (std::fwrite(buf.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
        onBytes(static_cast<int64_t>(n));
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    // ⚠️ `fclose` checked, and on its own line: a full card surfaces in this flush or nowhere.
    const bool flushed = std::fclose(out) == 0;
    ok = ok && flushed;

    if (ok) {
        pt_remove(to.c_str());   // a rename onto a taken name fails (or, on Android, de-duplicates)
        ok = pt_rename(tmp.c_str(), to.c_str()) == 0;
    }
    if (!ok) pt_remove(tmp.c_str());
    return ok;
}

}  // namespace

bool save_would_replace_other_song(FileSystem& fs, const AppState& s, const std::string& target) {
    if (!fs.file_exists(target)) return false;
    // A repaired song not saved yet: its own file is the damaged one, so the first SAVE asks too.
    if (s.repairedUnsaved) return true;
    // ⚠️ Identity, not folded spelling: on Linux "Projects/Song.ptp" (this document) and a separate
    // "Projects/SONG.ptp" are two songs, and renaming this one to SONG must ask.
    return s.projectPath.empty() || !same_file(s.projectPath, target);
}

ActionResult collect_project(const songcore::SongcoreHost& host, FileSystem& fs,
                             const std::string& folder, const std::function<void(float)>& progress) {
    const std::string songSub = std::string(songcore::SONGS_FOLDER) + "/" + folder;
    const std::string songDir = fs.make_app_folder(songSub);
    if (songDir.empty()) return ActionResult{false, "COLLECT FAILED"};
    const std::string songDirSlash = songDir + "/";

    songcore::Project copy = host.project();

    // The one path per instrument the loader opens (engine_setup.h `load_project_media`).
    std::vector<std::string*> paths;
    for (songcore::Instrument& ins : copy.instruments) {
        if (ins.instrumentType == songcore::InstrumentType::SOUNDFONT && ins.soundfontPath)
            paths.push_back(&*ins.soundfontPath);
        else if (ins.sampleFilePath)
            paths.push_back(&*ins.sampleFilePath);
    }

    // First what to copy, each file once however many instruments play it — so the bar knows the
    // total before the first byte moves.
    struct Copy {
        std::string source;
        std::string rel;
        int64_t     bytes = 0;
        bool        ok    = false;
        std::vector<std::string*> users;   // the paths that name it once it is copied
    };
    std::vector<Copy> copies;
    int64_t totalBytes = 0;
    int     missing    = 0;

    // Pass 1: where each path's file is, and which names in the song folder are already IN USE — a
    // file the song plays from its own folder (it was opened from there, or collected before) is the
    // copy itself. Its name is reserved before any other file is placed, whatever the instrument
    // order, or a namesake from elsewhere would be copied over it (R1).
    struct Found { std::string* stored; std::string source; bool inTarget; };
    std::vector<Found> found;
    std::vector<std::string> reserved;   // song-folder-relative names in use
    for (std::string* stored : paths) {
        if(stored->rfind("/@mutable/",0)==0) continue; // built-in DSP: no media to copy
        const std::string source = songcore::resolve_media_path(*stored, host.media_roots());
        if (!songcore::path_exists(source)) {
            ++missing;   // keeps its old path: the file may turn up where it was
            continue;
        }
        const bool inTarget = inside_folder(source, songDirSlash);
        if (inTarget) reserved.push_back(slashed_path(source).substr(songDirSlash.size()));
        found.push_back(Found{stored, source, inTarget});
    }

    // A target name is taken by a reserved file or an earlier copy — compared folded, so the
    // collected folder also works on a FAT card or Windows, where Kick.wav and kick.wav collide.
    const auto rel_taken = [&copies, &reserved](const std::string& r) {
        const std::string key = comparable_path(r);
        const auto same = [&key](const std::string& other) { return comparable_path(other) == key; };
        return std::any_of(reserved.begin(), reserved.end(), same) ||
               std::any_of(copies.begin(), copies.end(), [&same](const Copy& c) { return same(c.rel); });
    };

    // Pass 2: one copy per SOURCE FILE, by its real identity (same_file): the same file named twice
    // is copied once; two different files are never merged, even when their names differ only in
    // case (R2) or map to one place ("Songs/A/Samples/kick.wav", "Songs/B/Samples/kick.wav"). A name
    // already taken gets "_2", "_3" … (E7).
    for (const Found& f : found) {
        if (f.inTarget) {
            // ⚠️ Already in this folder: it IS the copy. Replacing it would delete the source before
            // reading it.
            *f.stored = slashed_path(f.source).substr(songDirSlash.size());
            continue;
        }
        auto it = std::find_if(copies.begin(), copies.end(),
                               [&f](const Copy& c) { return same_file(c.source, f.source); });
        if (it == copies.end()) {
            std::string rel = songcore::collected_media_path(*f.stored);
            if (rel_taken(rel)) {
                const size_t slash = rel.find_last_of('/');
                const size_t dot   = rel.find_last_of('.');
                const bool   hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
                const std::string stem = hasExt ? rel.substr(0, dot) : rel;
                const std::string ext  = hasExt ? rel.substr(dot) : std::string();
                for (int n = 2; rel_taken(rel); ++n) rel = stem + "_" + std::to_string(n) + ext;
            }
            Copy c;
            c.source = f.source;
            c.rel    = rel;
            c.bytes  = media_file_bytes(f.source);
            totalBytes += c.bytes;
            copies.push_back(std::move(c));
            it = copies.end() - 1;
        }
        it->users.push_back(f.stored);
    }

    // Reported only when the whole percent changes: each report is a repaint.
    int64_t doneBytes   = 0;
    int     lastPercent = -1;
    const auto onBytes = [&](int64_t n) {
        doneBytes += n;
        if (!progress || totalBytes <= 0) return;
        const int percent = static_cast<int>(std::min<int64_t>(100, doneBytes * 100 / totalBytes));
        if (percent == lastPercent) return;
        lastPercent = percent;
        progress(static_cast<float>(percent) / 100.0f);
    };
    onBytes(0);

    int failed = 0;
    for (Copy& c : copies) {
        const int64_t      before = doneBytes;
        const std::string  dest   = songDir + "/" + c.rel;
        const size_t       slash  = c.rel.find_last_of('/');
        c.ok = slash == std::string::npos ||
               !fs.make_app_folder(songSub + "/" + c.rel.substr(0, slash)).empty();
        if (c.ok && fs.file_exists(dest)) c.ok = fs.delete_path(dest);
        c.ok = c.ok && copy_in_pieces(c.source, dest, onBytes);
        // A found sound keeps its licence and source beside it (source_credits.h).
        if (c.ok && fs.file_exists(c.source + ".source.json") && !fs.file_exists(dest + ".source.json"))
            fs.copy_file(c.source + ".source.json", dest + ".source.json");
        // A file that failed, or measured wrong, still moves the bar by what it was counted as.
        onBytes(before + c.bytes - doneBytes);
        if (!c.ok) {
            ++failed;
            continue;
        }
        for (std::string* stored : c.users) *stored = c.rel;
    }

    if (!fs.write_file(songDir + "/" + folder + ".ptp", songcore::serialize_project(copy)))
        return ActionResult{false, "COLLECT FAILED"};
    source_credits_write(host.project(), host.media_roots(), fs, songDir + "/CREDITS.txt");
    fs.forget_listing(fs.songs_directory());
    fs.forget_listing(songDir);

    if (failed > 0)  return ActionResult{false, "COLLECTED: " + std::to_string(failed) + " NOT COPIED"};
    if (missing > 0) return ActionResult{false, "COLLECTED: " + std::to_string(missing) + " MISSING"};
    return ActionResult{true, "COLLECTED: " + songSub};
}

// ─── EXPORT → MIX ────────────────────────────────────────────────────────────────────────────────

namespace {

/** The rows a render covers. An unset range means the whole song — derived here once. */
songcore::SongBounds resolve_range(const songcore::Project& project, const RenderRange& range) {
    if (range.startRow < 0) return songcore::find_song_bounds(project);
    songcore::SongBounds b;
    b.startRow = range.startRow;
    b.endRow   = std::max(range.startRow, range.endRow);
    // ⚠️ A range over unwritten rows is EMPTY, not a file of silence that reads as a broken export.
    for (int row = b.startRow; row <= b.endRow; ++row)
        if (songcore::song_row_filled(project, row)) return b;
    return songcore::SongBounds();
}

}  // namespace

ActionResult render_mix(songcore::SongcoreHost& host, FileSystem& fs, AppState& s,
                        const RenderRange& range, const std::function<void(float)>& progress) {
    (void)s;

    const songcore::SongBounds bounds = resolve_range(host.project(), range);
    if (bounds.empty()) return ActionResult{false, "SONG IS EMPTY"};

    const std::string safeName = songcore::safe_project_name(host.project().name);
    const std::string path     = unique_render_path(fs, fs.renders_directory(), safeName);
    if (path.empty()) return ActionResult{false, "EXPORT FAILED: NO FREE NUMBER"};

    // With the master bus; the file runs on until the tails decay.
    songcore::RenderOptions opts;
    opts.stemsMode      = 0;
    opts.applyMasterBus = true;
    opts.keepExisting   = true;   // the number was free when chosen; never take it over now

    const songcore::RenderStats stats =
        host.render_song_range_to_wav(bounds.startRow, bounds.endRow, path, opts, progress,
                                      range.repeat);

    if (!stats.ok || stats.totalFrames <= 0) {
        if (songcore::detail::last_publish_target_taken())
            return ActionResult{false, "EXPORT FAILED: NUMBER TAKEN MEANWHILE"};
        return ActionResult{false, "EXPORT FAILED"};
    }
    const int credited = source_credits_write(host.project(), host.media_roots(), fs,
                                              path.substr(0, path.size() - 4) + "-CREDITS.txt");
    if (credited < 0) return ActionResult{true, "EXPORTED, CREDITS NOT WRITTEN"};
    return ActionResult{true, credited > 0 ? "EXPORTED + CREDITS" : "EXPORTED!"};
}

// ─── EXPORT → STEMS ──────────────────────────────────────────────────────────────────────────────

ActionResult render_stems(songcore::SongcoreHost& host, FileSystem& fs, AppState& s,
                          const RenderRange& range, const std::function<void(float)>& progress) {
    (void)s;

    // Once, so every stem covers the same rows.
    const songcore::SongBounds bounds = resolve_range(host.project(), range);
    if (bounds.empty()) return ActionResult{false, "SONG IS EMPTY"};

    const std::vector<songcore::StemPass> passes =
        songcore::stems_plan(host.project(), bounds.startRow, bounds.endRow);
    if (passes.empty()) return ActionResult{false, "NO ACTIVE TRACKS"};

    // Renders/<name>/; an empty name would put them straight into Renders/.
    std::string safeName = songcore::safe_project_name(host.project().name);
    if (safeName.empty()) safeName = "project";

    const std::string rendersDir = fs.renders_directory();
    const std::string stemDir    = rendersDir + "/" + safeName;
    if (!fs.file_exists(stemDir)) {
        const std::string created = fs.create_folder(rendersDir, safeName);
        if (created.empty()) return ActionResult{false, "STEMS FAILED"};
    }

    const int total = static_cast<int>(passes.size());
    int       done  = 0;

    for (const songcore::StemPass& pass : passes) {
        songcore::RenderOptions opts;
        opts.stemsMode = pass.stemsMode;
        opts.applyMasterBus = false;   // re-mixed in a DAW, it would be applied twice

        const std::string path = stemDir + "/" + safeName + pass.suffix + ".wav";

        // One prepare per pass, so no stem begins inside the last one's reverb tail.
        const int   from = done;
        const auto slice = [&progress, from, total](float p) {
            if (progress) progress((static_cast<float>(from) + p) / static_cast<float>(total));
        };

        const songcore::RenderStats stats =
            host.render_song_range_to_wav(bounds.startRow, bounds.endRow, path, opts,
                                          progress ? slice : std::function<void(float)>(),
                                          range.repeat);

        // Stop at the first failure (most likely a full card) and say how many landed.
        if (!stats.ok || stats.totalFrames <= 0)
            return ActionResult{false, done == 0 ? "STEMS FAILED"
                                                 : "STEMS: " + std::to_string(done) + " OF " +
                                                       std::to_string(total)};
        ++done;
    }

    if (progress) progress(1.0f);
    const int credited = source_credits_write(host.project(), host.media_roots(), fs,
                                              stemDir + "/" + safeName + "-CREDITS.txt");
    if (credited < 0) return ActionResult{true, "STEMS EXPORTED, CREDITS NOT WRITTEN"};
    return ActionResult{true, credited > 0 ? "STEMS + CREDITS" : "STEMS EXPORTED!"};
}

// ─── SONG selection → RESAMPLE ─────────────────────────────────────────────────────────────────

std::string resample_base_name(FileSystem& fs) {
    const std::string dir = fs.resampled_directory();
    for (int index = 1; index < 10000; ++index) {
        const std::string base = "Resample_" + pad4(index);
        const std::string wav = dir + "/" + base + ".wav";
        if (!fs.file_exists(wav) && !fs.file_exists(wav + songcore::detail::kBackupSuffix)) return base;
    }
    return std::string();   // all taken: the field starts empty, and an empty name is refused below
}

ActionResult render_resample(songcore::SongcoreHost& host, FileSystem& fs,
                             int startRow, int endRow, const std::set<int>& trackFilter,
                             const std::string& customBaseName, std::string& outPath,
                             const std::function<void(float)>& progress) {
    const std::string dir = fs.resampled_directory();

    // A typed name is used as is and overwrites; an empty one is numbered.
    const std::string path = customBaseName.empty()
                                 ? unique_render_path(fs, dir, "Resample")
                                 : dir + "/" + songcore::safe_project_name(customBaseName) + ".wav";
    if (path.empty()) return ActionResult{false, "RESAMPLE FAILED: NO FREE NUMBER"};

    // ⚠️ finish_render runs even when nothing was scheduled: prepare silenced the live stream.
    host.prepare_render(startRow, endRow);
    const int64_t songFrames = host.schedule_song_range(startRow, endRow, &trackFilter);
    if (songFrames <= 0) {
        host.finish_render();
        return ActionResult{false, "RESAMPLE FAILED"};
    }

    // A mix of the selected tracks, with the master bus.
    const songcore::RenderStats stats =
        host.render_to_wav(path, songFrames, /*stemsMode=*/0, /*applyMasterBus=*/true, progress,
                           /*keepExisting=*/customBaseName.empty());   // numbered: never over a newcomer
    host.finish_render();

    if (!stats.ok || stats.totalFrames <= 0) return ActionResult{false, "RESAMPLE FAILED"};

    outPath = path;
    return ActionResult{true, ""};   // the caller reports "RESAMPLED -> INST xx" once the slot lands
}

int create_resampled_instrument(songcore::SongcoreHost& host, const std::string& wavPath) {
    songcore::Project& p = host.edit_project();

    int slot = -1;
    for (int i = 0; i < static_cast<int>(p.instruments.size()); ++i) {
        if (songcore::instrument_is_free(p.instruments[static_cast<size_t>(i)])) { slot = i; break; }
    }
    if (slot < 0) return -1;

    if (!host.load_sample(slot, wavPath)) return -1;   // the slot stays free on failure

    // Set anyway, so it can never be a hybrid.
    songcore::Instrument& ins = p.instruments[static_cast<size_t>(slot)];
    ins.instrumentType = songcore::InstrumentType::SAMPLER;
    ins.soundfontPath.reset();
    ins.sampleFilePath = wavPath;
    ins.sampleId       = slot;
    ins.root           = songcore::Note::C4();
    ins.volume         = 0xFF;
    ins.pan            = 0x80;
    host.push_instrument(slot);

    return slot;
}

// ─── The instrument preset and the song template ─────────────────────────────────────────────────

bool save_instrument_preset(const songcore::SongcoreHost& host, FileSystem& fs, int id,
                            const std::string& path) {
    return fs.write_file(path, songcore::serialize_instrument_preset(
                                   songcore::make_instrument_preset(host.project(), id)));
}

ActionResult save_template(songcore::SongcoreHost& host, FileSystem& fs) {
    if (!write_project(host, fs, fs.template_project_path()))
        return ActionResult{false, "SAVE FAILED"};
    return ActionResult{true, "TEMPLATE SAVED"};
}

ActionResult clear_template(FileSystem& fs) {
    const std::string path = fs.template_project_path();
    if (!fs.file_exists(path)) return ActionResult{true, "TEMPLATE CLEARED"};
    if (!fs.delete_path(path)) return ActionResult{false, "CLEAR FAILED"};
    return ActionResult{true, "TEMPLATE CLEARED"};
}

}  // namespace pt::ui
