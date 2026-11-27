# Sound: master limiter

Agreed 2026-11-18, detailed 2026-11-20 after a grill-me session.

## Goal

Replace the always-on DaisySP limiter (`effects/primitives/daisysp/limiter.cpp`). Today it:
- scales the whole mix by 0.7 and runs every sample through a soft-saturation curve, so a full mix leaves about 4 dB
  under full scale and is slightly coloured;
- reacts in under 0.5 ms with no look-ahead, so bass distorts;
- releases over about 1 s, so the mix pumps;
- limits left and right separately, so the stereo image shifts.

Before building, measure a real render to confirm the −4 dB.

## The setting

- PROJECT gets `LIMITER: CLASSIC / CLEAN`, stored in the song.
  - **CLASSIC** is today's limiter, unchanged.
  - **CLEAN** is the new one.
- Existing songs (no field) open as CLASSIC, so they sound exactly as before.
- New songs start as CLEAN.
- Any song can be switched.
- **File format (decided 2026-11-27):** a CLEAN song is saved with `version` 2 and `minReader` 2, so an older app
  that knows file versions (refuse-newer-files.md) refuses it with `UPDATE TO OPEN` instead of playing it through
  CLASSIC. A CLASSIC song writes no field and stays `version` 1 / `minReader` 1, so it still opens everywhere.
- Imported songs (M8) start from the default song and stay CLASSIC: their levels were set against it.
- The old limiter is kept, not replaced: every existing song was mixed through it, and some drive it on purpose.

## CLEAN

- **Stereo-linked:** one gain for both channels, so the image never shifts.
- **Ceiling:** −0.3 dBFS, sample peak.
- **Look-ahead:** 1.5 ms. The audio is delayed so the gain can ease down before a peak arrives, with no distortion on bass
  or kicks.
- **Release:** adapts to the material: short for isolated peaks, longer for dense material, so there's no pumping.
- **Transparent** below the ceiling, with no fixed gain cut. A full mix is about 3–4 dB louder than through CLASSIC.
- No knobs. Shaping the master belongs to the Studio's Master tab; this is a safety net.
- Real-time rules: no allocation and no locks in the audio callback. The look-ahead buffer is sized once, at startup or
  at a sample-rate change.

## Latency and MIDI

- The look-ahead adds 1.5 ms of output delay when CLEAN is on.
- The Settings latency row (latency-display.md) includes it.
- The automatic MIDI-out offset (`OFFSET AUTO`) adds it, so external gear stays in time.
- Offline work (export, stems, resample, render) compensates for it: files carry no added delay.

## Tests

- **CLASSIC** is bit-identical to today on a fixed render.
- **CLEAN:**
  - Its output never exceeds −0.3 dBFS on a hot test signal.
  - It passes a signal below the ceiling unchanged, after the 1.5 ms delay.
  - Both channels get the same gain.
  - Its exported files are aligned with CLASSIC's, with no added offset.
- **Song files:** a song without the field loads as CLASSIC, and a new song saves CLEAN.
- Any golden recordings of CLEAN renders are re-recorded. CLASSIC's stay as they are.

## Out of scope

- A ceiling or release control.
- A limiter per track.
- True-peak (inter-sample) detection.

## Help

- SELECT help for PROJECT `LIMITER` (`help_text.h`): what CLASSIC and CLEAN do, the 1.5 ms delay, and old songs
  staying CLASSIC.
- Manual, PROJECT section: the `LIMITER` row.
