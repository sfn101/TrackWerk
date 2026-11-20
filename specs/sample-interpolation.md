# Sound: sample interpolation

Agreed 2026-11-18, detailed 2026-11-20 after a grill-me session.

## Goal

Sample playback reads with linear interpolation (`sampler-voice.h` `Playhead::read`). Samples pitched up alias and
pitched down go dull. Better modes fix that.

## Modes

- **The list:** NONE, LINEAR (today's), CUBIC, SINC 8, SINC 8 LP.
- **Source:** ported from OpenMPT's resampler, only where it fits the float mixer. It is BSD-3, which fits GPL-3; its
  notice goes in `docs/licenses/THIRD-PARTY-NOTICES.md`.
- No separate Hermite: it would sound the same as CUBIC.
- The downsample effect keeps its own quantized read.

## Where the mode is saved

- **The song** keeps its mode in the file. Opening a song plays the song's mode.
- **The device** keeps a default. A new song starts with it, and closing a song and starting a new one goes back to it.
  Opening a song never changes it.
- **Old songs** (no mode in the file) load as LINEAR, so they sound as they did. Saving writes the mode; not saving
  changes nothing.
- **The SETTINGS mode row** changes the open song's mode. On a new song it first asks whether the choice is for this song
  only or for the device default, with an info text that explains the difference. That is the only way to change the
  device default.
- **File format:** the mode raises the song's `version` and `minReader` (refuse-newer-files.md). An older app would play
  it as LINEAR, which changes the sound.

## Device limit

- A second SETTINGS row sets the highest mode this device may play in real time.
- A song asking for more plays at the limit, keeps its saved mode, and the status line says `INTERP LIMITED TO <mode>`.
- **Factory default and limit:**

  | Platform | Default | Limit |
  |---|---|---|
  | Android, Windows, Linux desktop | CUBIC | SINC 8 LP (all modes) |
  | Handhelds (Brick, RGB20S, Miyoo) | CUBIC or LINEAR (see below) | same as the default |

  - The spec's first build step measures CUBIC on the weakest handheld: RGB20S, Cortex-A35, 8 tracks plus chords
    playing.
  - If it holds with room to spare, handhelds start on CUBIC; otherwise on LINEAR.
- On any device the user can raise the limit up to SINC 8 LP. The row's info text warns that higher modes cost CPU, and
  to lower it again if the audio crackles.
- LINEAR and NONE stay available everywhere as cheaper choices.

## Exports and tests

- Export, stems and resample render offline, so they use the song's saved mode even above the device limit. An export
  from a handheld sounds the same as one from a PC.
- Each test sets the mode explicitly, so results never depend on device settings.

## Real-time

- The interpolation runs in the audio callback: no allocation and no locks.
- Sinc tables are built once at startup.
- The mode is read through the existing engine state, not per sample from the UI.

## Tests

- Each mode against a reference sine at several pitches: aliasing and high-frequency loss within set bounds, and LINEAR
  bit-identical to today.
- The song and device-default rules: new song, open, close, and old song as LINEAR.
- The limit: the song plays at the limit and keeps its saved mode.
- Exports ignore the limit.

## Out of scope

- Per-instrument or per-sample modes.
- SoundFont interpolation: tsf keeps its own.

## Help

- SELECT help for the two SETTINGS rows (mode and limit), including the "this song or device default" question
  (`help_text.h`).
- Manual, SETTINGS section: the modes, the two saved places, and the limit.
- THIRD-PARTY-NOTICES: the OpenMPT notice.
