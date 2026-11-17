# Audit fixes, batch 1: crashes and lost work

Agreed 2026-11-17. Source of the findings: `AUDIT.md` (repo root). File paths below are under `source/`.

## Goal

The app must never crash, hang or silently lose work because of a damaged file or a careless LOAD/SAVE. Every fix lands with an automated test that fails before the fix and passes after it.

## Findings in this batch

| # | Problem | Fix |
|---|---|---|
| 1 | LOAD throws away unsaved work and clears `autosave.ptp` with no question | If `project_dirty()`, open a confirm dialog: `UNSAVED CHANGES - LOAD ANYWAY?` (A = yes, B = no). Clear autosave only after the new song loaded. |
| 2 | SAVE overwrites a different song with the same name (`Song` vs `SONG` on Windows) | If the target exists and isn't the song currently open, confirm: `SONG EXISTS - OVERWRITE?`. Name comparison is case-insensitive on Windows. |
| 3 | Bad instrument `id` reads/writes outside an array (`songcore/engine_setup.h`) | Reject the file. |
| 4 | Phrase `steps` shorter than 16 overruns (`project_io.h`, `scheduler.cpp`) | Reject the file. |
| 5 | Tempo 0, negative or huge divides by zero (`project_io.h`, `scheduler.cpp`) | Reject the file. |
| 6 | WAV/SF2 chunk loop never ends on Windows (32-bit `fseek`) | Use 64-bit offsets (`_fseeki64`) and stop on any chunk that doesn't move forward or runs past the end of the file. |
| 7 | Tiny SF2 asks for 4 GB; `bad_alloc` kills the app | Check sizes against the file size before allocating; refuse the file. |
| 16 | Negative WAV sample rate half-loads a sample | Refuse the WAV. |
| 17 | Negative groove steps make time run backwards | Reject the file. |
| 9 | Windows not in CI; GCC-only flags reach MSVC | Add a Windows job to `.github/workflows/build.yml` that builds the app and runs the new tests. Give the flags in `native/studio/CMakeLists.txt` MSVC branches. |

## Behaviour on a damaged file

- **Refuse the whole file.** Nothing is half-loaded and the current song stays open untouched, so a later SAVE can't overwrite a good file with a broken one.
- Show a short status message through the existing `set_status`: `LOAD FAILED: FILE DAMAGED` (for samples: `SAMPLE FAILED: FILE DAMAGED`).
- Write the details (file, which check failed) to `trackwerk-log.txt`.
- Range checks happen when the file is read (`parse_project` / `normalize_and_migrate` in `songcore/project_io.h`, the WAV/SF2 readers), not later in the engine.

## Prompts

Both prompts reuse the existing confirm dialog (`ConfirmDialogState::Kind` in `ui/modules/confirm_dialog.h`, handled in `ui/dispatch/layers/confirm.cpp`), with two new kinds. They look and work like the NEW and EXIT prompts.

## Tests

- **Test files:** `source/tests/corrupt-files/`, one small hand-made file per finding, named after it, e.g. `03-bad-instrument-id.ptp`, `04-short-steps.ptp`, `05-zero-tempo.ptp`, `06-looping-chunk.wav`, `06-looping-chunk.sf2`, `07-huge-size.sf2`, `16-negative-rate.wav`, `17-negative-groove.ptp`. Each is a few hundred bytes at most.
- **README** in that folder: one line per file saying what it is and what the app should do now (refuse with the message, not crash). These are the files for manual testing too: copy one into `Documents\TrackWerk` and LOAD it.
- **Automated test:** one check program that loads every file in the folder through the real loaders and asserts that each one is refused cleanly. It is written to build with both GCC and MSVC (no POSIX-only calls) and runs in CI on Linux and on Windows. On Windows it also runs locally with one command, documented in the README.
- **Findings 1 and 2:** a check that drives the input dispatcher: dirty song + LOAD opens the confirm dialog; B keeps the song; SAVE onto an existing other name opens the overwrite prompt.
- Each fix is committed with its test; the test must fail on the code before the fix.

## Platforms

All fixes are in the shared code (`native/`), so they apply to every platform. The only platform-specific work is the Windows CI job.

## Out of scope

Batches 2 and 3 (see `ROADMAP.md`). Repairing damaged files or loading their good parts.
