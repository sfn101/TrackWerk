# Latency display

Agreed 2026-11-17.

## Goal

Show the audio output latency of the current audio output in Settings, so you can switch between outputs (SYSTEM, ASIO drivers, USB cards) and compare them.

## Screen

- A new read-only row `LATENCY` directly under `AUDIO OUT` in Settings, shown on **every** platform (Android and Linux under Pulse/PipeWire have no AUDIO OUT row, but still get LATENCY).
- Value in milliseconds with one decimal: `10.7 MS`.
- When the backend only knows its buffer size, not the real latency, the value starts with `>=`: `>= 10.7 MS` (the pixel font has no `≥` glyph). This is honest about SYSTEM on Windows, where the real latency is at least the buffer.
- The value updates on its own when the audio output changes; no button.
- The row is information only: drawn faded (the theme's placeholder grey), and the cursor never stops on it. UP/DOWN step over it, so it has no help entry.

## Where the number comes from

Nothing new is measured. `AudioBackend::outputLatency()` (`native/audio-backend.h`) already returns `{frames, measured}` for every backend, and `shell/app.cpp` already turns it into milliseconds for MIDI OFFSET AUTO. The row shows the same number.

| Backend | What it reports | Shown as |
|---|---|---|
| ASIO (Windows) | Driver's real output latency (`ASIOGetLatencies`) | `10.7 MS` |
| SDL: Windows SYSTEM, Linux | Buffer size only | `>= 10.7 MS` |
| Oboe AAudio (Android) | Real latency | `10.7 MS` |
| Oboe OpenSL ES (older Android) | Buffer size only | `>= 10.7 MS` |

## File format

None. The value is not saved.

## Out of scope

A real loopback "Test" button and a buffer-size setting (both dropped from the roadmap: device- and driver-specific).
