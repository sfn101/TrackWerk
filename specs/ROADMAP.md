# Roadmap

Work top to bottom. Each item has its own spec; finish (build, test on Windows, CI green) before starting the next.

| # | Item | Spec | Status |
|---|---|---|---|
| 1 | Audit fixes, batch 1 (crashes and lost work) | [audit-fixes-batch1.md](audit-fixes-batch1.md) | Done |
| 2 | Latency display in Settings | [latency-display.md](latency-display.md) | Done |
| 3 | Full-screen visualizer (VI screen) | [fullscreen-visualizer.md](fullscreen-visualizer.md) | Planned |

## Later (no spec yet)

- **Audit fixes, batch 2 (save safety):** AUDIT.md findings 8, 10, 11, 12, 19.
- **Audit fixes, batch 3 (the rest):** findings 13, 14, 15 (Linux streaming), 18 (Windows README), 20 (note queue on the audio thread).
- **Loopback latency test:** a real round-trip measurement (output → cable → input). Needs an audio input path, which Windows doesn't have yet.
- **Buffer-size setting:** pick the SDL buffer size in Settings instead of the `POCKETTRACKER_AUDIO_FRAMES` environment variable.
- **Save under a free name:** a third answer on the `SONG EXISTS - OVERWRITE?` prompt, shown only when SAVE would replace a different song (never when saving the open one). One press saves to the next free file name, `<name>-2.ptp`, then `-3`…; the song's own name stays exactly as typed (only the file name changes, with symbols as `_` as today). Label as short as possible (e.g. `NEW NAME`). Its button is decided in the spec: not A or B; COLLECT's REPLACE / NEW COPY / CANCEL picker is a model.
- **Edge-to-edge visualizer:** let the VI screen fill wide screens instead of keeping the 4:3 area with bars.
