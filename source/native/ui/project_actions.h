#pragma once

// SAVE, EXPORT, RESAMPLE and the song template: where the host meets the FileSystem.

#include <functional>
#include <set>
#include <string>

#include "songcore/host.h"
#include "ui/app_state.h"
#include "ui/filesystem.h"

namespace pt::ui {

// The status line's text, and whether it is green or red.
struct ActionResult {
    bool        ok = false;
    std::string message;
};

// `<dir>/<name>_0001.wav`, counting up until free.
// "<dir>/<name>_NNNN.wav" for the first number taken neither by a WAV nor by its .ptbak; empty when
// all 9 999 are taken.
std::string unique_render_path(FileSystem& fs, const std::string& dir, const std::string& safeName);

// Writes `<Projects>/<name>.ptp` and marks the document clean.
ActionResult save_project(songcore::SongcoreHost& host, FileSystem& fs, AppState& s);

// Where SAVE writes the song when it is called `name`: the folder it came from, else Projects.
std::string project_save_path(const songcore::SongcoreHost& host, FileSystem& fs, const AppState& s,
                              const std::string& name);

// Saving as `name` would write over a file that is not the song open now (Windows ignores case:
// "Song" is "SONG"), or over the damaged file of a song repaired and not saved yet.
bool save_would_replace_other_song(FileSystem& fs, const AppState& s, const std::string& path);

// SAVE keeps what it replaces: <home>/Backups/<NAME>_bak1.ptp is the previous version, _bak2 the one before,
// up to this many. (Also kept when SAVE > REPLACE overwrites another project of the same file name.)
inline constexpr int kProjectBackupsKept = 5;
std::string project_backup_path(FileSystem& fs, const std::string& stem, int generation);

// SAVE's own question: save_would_replace_other_song for the path SAVE would write. True when SAVE would
// replace a file this document did not come from (another project with the same
// file name: two UNTITLED, "MY SONG" and "MY_SONG", or names that differ in case on FAT/Windows/macOS).
// ⚠️ A re-save of the loaded or last-saved file is never asked about.
bool save_replaces_other(const songcore::SongcoreHost& host, FileSystem& fs, const AppState& s);

// `name` with "_2", "_3" … (kept within the NAME row's 20 characters) whose project file is free; ""
// when none up to 999.
std::string free_project_name(FileSystem& fs, const std::string& name);
// The same, free in `folder` (where SAVE writes: the song's own folder, else Projects).
std::string free_project_name(FileSystem& fs, const std::string& folder, const std::string& name);

// EXPORT > MIDI: the song as `<Renders>/<NAME>_NNNN.mid` (songcore/midi_export.h), a note sketch for a
// DAW: effects, grooves and sounds are not in it. The message says how many notes and tracks.
ActionResult export_midi(const songcore::SongcoreHost& host, FileSystem& fs);

// ─── COLLECT: the song and every file it plays, copied into one folder under Songs/ ──────────────

// The folder COLLECT writes: the project's file name.
std::string collect_folder_name(const songcore::SongcoreHost& host);

// Is there already a Songs/<folder>?
bool collect_folder_exists(FileSystem& fs, const std::string& folder);

// The first free `<folder>_2`, `<folder>_3`… under Songs/.
std::string collect_free_folder_name(FileSystem& fs, const std::string& folder);

// Songs/<folder>/<folder>.ptp plus a copy of every sample and SoundFont the song plays, the .ptp
// naming the copies relative to itself. An existing folder is written into, its files replaced.
// The open document and its file are untouched.
ActionResult collect_project(const songcore::SongcoreHost& host, FileSystem& fs,
                             const std::string& folder, const std::function<void(float)>& progress);

// The rows a render covers and how many times. ⚠️ `startRow < 0` is the whole song.
struct RenderRange {
    int startRow = -1;
    int endRow   = -1;
    int repeat   = 1;
};

// One WAV of `range` with the master bus, into Renders/. Synchronous: the caller pauses the audio first.
ActionResult render_mix(songcore::SongcoreHost& host, FileSystem& fs, AppState& s,
                        const RenderRange& range, const std::function<void(float)>& progress);

// One WAV per track active in the range, plus the two returns, into `Renders/<name>/`; no master bus.
ActionResult render_stems(songcore::SongcoreHost& host, FileSystem& fs, AppState& s,
                          const RenderRange& range, const std::function<void(float)>& progress);

// ─── RESAMPLE: a SONG selection mixed to a WAV and loaded into a free instrument slot ────────────

// The name the keyboard opens on: the first free `Resample_NNNN`.
std::string resample_base_name(FileSystem& fs);

// Rows [startRow,endRow] of `trackFilter` to Resampled/, with the master bus; synchronous. An empty
// `customBaseName` is numbered, a typed one overwrites. `outPath` is set only on success.
ActionResult render_resample(songcore::SongcoreHost& host, FileSystem& fs,
                             int startRow, int endRow, const std::set<int>& trackFilter,
                             const std::string& customBaseName, std::string& outPath,
                             const std::function<void(float)>& progress);

// The WAV into the first free slot as a clean SAMPLER at C-4; the slot, or -1.
int create_resampled_instrument(songcore::SongcoreHost& host, const std::string& wavPath);

// Instrument `id`, and its table if it has content, as a `.pti`.
bool save_instrument_preset(const songcore::SongcoreHost& host, FileSystem& fs, int id,
                            const std::string& path);

// The current project becomes what the app boots into.
ActionResult save_template(songcore::SongcoreHost& host, FileSystem& fs);

// Clearing a template that is not there succeeds.
ActionResult clear_template(FileSystem& fs);

}  // namespace pt::ui
