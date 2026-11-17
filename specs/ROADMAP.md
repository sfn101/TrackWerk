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
- **Save under a free name:** a third answer on the `SONG EXISTS - OVERWRITE?` prompt, shown only when SAVE would replace a different song (never when saving the open one). One press saves to the next free file name, `<name>-2.ptp`, then `-3`…; the song's own name stays exactly as typed (only the file name changes, with symbols as `_` as today). Label as short as possible (e.g. `NEW NAME`). Its button is decided in the spec: not A or B; COLLECT's REPLACE / NEW COPY / CANCEL picker is a model.
