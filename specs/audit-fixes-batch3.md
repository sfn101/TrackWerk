# Audit fixes, batch 3: the rest

Agreed 2026-11-17/19. Source of the findings: `AUDIT.md` (repo root). File paths below are under `source/`. Bug fixes from `AUDIT.md`, so no grill-me session.

## Goal

Close every remaining audit finding: Linux streaming that stops or dies for no reason, a wrong Windows README, and three small engine and file-format holes.

## Findings in this batch

| # | Problem | Fix |
|---|---|---|
| 13 | Live radio always stops at 10 minutes (`shell/linux/source-sampler.cpp`, `CURLOPT_TIMEOUT` 600) | No total time limit for live streams. The existing low-speed check (under 1 byte/s for 5 s) still ends a dead station. |
| 14 | Large downloads fail on slow Wi-Fi: files are cut at 180 s, progressive archive MP3s at the short metadata limit | No total time limit for file and progressive downloads. Instead they end when under 1 byte/s for 30 s. Metadata requests keep their short limit. |
| 15 | SIGPIPE can kill the app on Linux when a connection drops (`shell/main.cpp`) | Ignore SIGPIPE at start-up (not on Windows, which has no SIGPIPE). Child processes (the YouTube helper, the iPhone recorder) get the default back before `execv`, so they behave as if started from a shell. |
| 18 | The Windows README describes a console window that doesn't exist and links to `conanizer/pockettracker` (`shell/windows/README.txt`) | Say output goes to `Documents\TrackWerk\trackwerk-log.txt`, and link to `sfn101/TrackWerk`'s issues. |
| 20 | The note queue reserves 64 entries, so a busy moment grows it on the audio thread (`native/note-queue.h`, `audio-engine.cpp`) | Reserve 1024 for the queues and the audio thread's batches. |
| — | Cancel-ledger off-by-one (`note-queue.h`): an entry exactly `HISTORY` cancels old reads a slot the UI may be overwriting | `>` → `>=`: such an entry counts as cancelled, as older ones already do. |
| — | Slice-marker overflow (`songcore/project_io.h`): a huge or negative marker in a song is undefined on read and overflows `marker × 255` in `program.h` | A marker outside 0..2147483647 is a damage like a bad tempo: the song is refused with `FILE DAMAGED - REPAIR?`; the repair clamps it into range. |
| — | Orphan WAV temps never cleaned on Windows (`songcore/wav_writer.h` `remove_orphan_temps` is POSIX-only) | Windows checks whether the temp's process is still running (`OpenProcess`) and removes the temps of dead ones, as Linux does with `kill(pid, 0)`. |

## Tests

- **Slice markers:** `tests/corrupt-files/21-huge-slice-marker.ptp` (marker 1e30). The existing `ptcorrupt-files-check` refuses it and repairs it with one fix, on Linux and Windows CI.
- **Orphan temps:** checked on Windows by hand: a `.ptw-99999-1.tmp` beside a render's target disappears on the next render; one with the app's own pid stays.
- **13, 14, 15:** Linux only, no network in CI: built by CI; a manual check on a handheld or Linux desktop (a radio station past 10 minutes).
- **18, 20, cancel ledger:** read in review; the existing host checks must stay green.

## Help

No new controls or messages. The corrupt-files README gains its new file. The Windows README is corrected (finding 18).
