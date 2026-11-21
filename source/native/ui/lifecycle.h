#pragma once

// The crash-recovery autosave. Its presence is the signal: written while there is unsaved work, deleted
// on every clean save, load, new and exit — so a file at launch means an unclean exit.
// ⚠️ The deletions matter as much as the writes: a stale file asks to recover work already saved.
// Written only through `FileSystem::write_file`, so a write cut short keeps the last good one.

#include <string>

#include "songcore/host.h"
#include "songcore/project_io.h"   // FileFormat
#include "ui/filesystem.h"

namespace pt::ui {

bool autosave_exists(FileSystem& fs);

// A failure returns false and says nothing: the user did not ask for this write.
// `durable` false (the 3 s debounce, on the UI thread) skips the sync; the flush on background and exit
// stays durable, because that is the write a power-off or a killed app follows.
bool autosave_write(const songcore::SongcoreHost& host, FileSystem& fs, bool durable = true);

// Deleting one that is not there succeeds.
bool autosave_clear(FileSystem& fs);

// Into the live document, media and all. ⚠️ `mediaBaseDir` is the session's, never the autosave's own
// folder: relative sample paths resolved there come back silent.
// `repair` fixes a damaged but readable autosave instead of refusing it.
bool autosave_load(songcore::SongcoreHost& host, FileSystem& fs, const std::string& mediaBaseDir,
                   bool repair = false);

// After a load failed: the refusal for a file from a newer TrackWerk, else `fallback`.
std::string load_failed_message(FileSystem& fs, const std::string& path, const songcore::FileFormat& format,
                                const std::string& fallback);

}  // namespace pt::ui
