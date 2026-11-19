# Audit fixes, batch 2: save safety

Agreed 2026-11-17 (finding 8) and 2026-11-19. Source of the findings: `AUDIT.md` (repo root). File paths below are under `source/`. Bug fixes from `AUDIT.md`, so no grill-me session.

## Goal

A SAVE never destroys the old file, the new file, or both, and never publishes a half-loaded song.

## Findings in this batch

| # | Problem | Fix |
|---|---|---|
| 8 | When the rename of `<file>.tmp` onto the target fails (on Windows: OneDrive or antivirus holding the file), `StdFileSystem::write_bytes` copies over the target and deletes the temp even if the copy failed (`native/ui/std_filesystem.cpp`) | No copy fallback (the temp is always beside the target, so the rename never crosses file systems). Retry the rename for about a second (10 tries, 100 ms apart). If it still fails, remove the temp and return false. **SAVE of a song** then writes `<name>-backup.ptp` beside the song, leaves the original untouched and says `FILE LOCKED: SAVED AS <name>-backup.ptp`. The backup loads from LOAD like any song. If the backup can't be written either, the folder is read-only and today's Projects fallback runs. |
| 10 | No `fsync` before the rename, so power loss after a save can leave an empty song (`std_filesystem.cpp`, `songcore/wav_writer.h`) | Flush and `fsync` (`_commit` on Windows) the temp before the rename, as `studio/granular_disk_take.cpp` does. `write_bytes` uses `FILE*` instead of `ofstream` so it has the descriptor. |
| 11 | Android SAF save deletes the old file before a rename that can fail (`shell/android/saf-filesystem.cpp`) | If the rename fails, write the data straight to the real path, then drop the temp. |
| 12 | COLLECT merges different samples that share a file name (`native/ui/project_actions.cpp`) | When a name is already taken by a different source, the copy gets `_2`, `_3`… before the extension (`kick_2.wav`). |
| 19 | Background autosave can save a half-loaded song (`native/ui/input_dispatcher.cpp`) | No autosave while `s_.loading.running`, in both `run_due_autosave` and `flush_autosave`. |

## Status messages

- `FILE LOCKED: SAVED AS <name>-backup.ptp` (new, finding 8). The song counts as saved (the work is safe), but its path stays the original file, so the next SAVE tries the original again without asking.

## Out of scope

- A backup name that already belongs to a different song (`foo-backup.ptp` saved on purpose) is overwritten by `foo`'s locked save. Rare; not handled.
- Other files (presets, themes, scales…) get the retry and the fsync, not the backup; they fail with their existing message.

## Tests

One portable check program, `ptsave-safety-check` (GCC and MSVC, no assert), run in CI on Linux and Windows:

- **8:** the song's target is made un-replaceable (a non-empty folder with the song's file name, which no rename can replace on any OS). SAVE writes `<name>-backup.ptp`, the target is unchanged, no `.tmp` is left, the status names the backup.
- **12:** two instruments play `a/kick.wav` and `b/kick.wav` with different content; COLLECT keeps both, as `kick.wav` and `kick_2.wav`, and each instrument points at its own.
- **19:** with `loading.running` set and the song dirty, a due autosave and `flush_autosave` write nothing.
- **10, 11:** not testable without pulling power or an Android device; 11 is built by CI's Android job.

## Help

No new screens or controls. The manual's SAVE row gains one sentence on `FILE LOCKED: SAVED AS …`, and its COLLECT row one sentence on same-named samples kept apart as `_2`. The in-game SELECT entries stay as they are: their boxes are full, and neither change needs the user to do anything differently.
