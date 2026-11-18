# Roadmap

Work top to bottom. Each item has its own spec; finish (build, test on Windows, CI green) before starting the next.

| # | Item | Spec | Status |
|---|---|---|---|
| 1 | Audit fixes, batch 1 (crashes and lost work) | [audit-fixes-batch1.md](audit-fixes-batch1.md) | Done |
| 2 | Latency display in Settings | [latency-display.md](latency-display.md) | Done |
| 3 | Full-screen visualizer (VI screen) | [fullscreen-visualizer.md](fullscreen-visualizer.md) | Done |
| 4 | Repair a damaged song on request | [repair-damaged-song.md](repair-damaged-song.md) | Planned |

## Later (no spec yet)

- **Audit fixes, batch 2 (save safety):** AUDIT.md findings 8, 10, 11, 12, 19. Finding 8 as agreed 2026-11-17: no copy fallback; retry the rename for about a second; if still stuck, save the song as `<name>-backup.ptp` beside it (loadable from LOAD), keep the original untouched, and say so on the status line.
- **Audit fixes, batch 3 (the rest):** findings 13, 14, 15 (Linux streaming), 18 (Windows README), 20 (note queue on the audio thread: reserve 1024, as the audit says).
- **Save under a new name (agreed 2026-11-18):** when SAVE would write over an existing file (songs: only a *different* song, never the open one), ask `<THING> EXISTS - OVERWRITE?`. A overwrites. B opens the on-screen keyboard to type a new name; saving it goes through the same check again. Cancelling the keyboard goes back to where SAVE was pressed: nothing saved, nothing changed. No automatic `-2` numbering. One rule for **every save that writes a file**: songs, instrument presets (`.pti`), sample SAVE-AS and RESAMPLE (`.wav`, which number themselves today), themes (`.ptt`), scales (`.pts`), grooves (`.ptg`), reverb (`.ptr`), delay (`.ptd`), and chords when the chord editor exists. Also a FILE RENAME onto a name that is taken. Fixed-name saves (the template, Studio's custom FX/master slots) and COLLECT (already asks) are reviewed in the spec.
