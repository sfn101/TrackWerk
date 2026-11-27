# Sound: oversampled drive

Agreed 2026-11-20 after a grill-me session.

## Goal

The DaisySP overdrive (instrument chain drive module, `effects/modules/drive-module.h`, the `DRV` command) runs at the base
sample rate. High notes through heavy drive alias: harsh, out-of-tune tones above the note. Running the drive stage at 2×,
with a small up/down filter pair, removes most of it.

## Rule

- **No new setting.** The drive follows the song's interpolation mode (sample-interpolation.md):
  - LINEAR or NONE: today's drive, unchanged. Old songs load as LINEAR, so they sound identical.
  - CUBIC, SINC 8, SINC 8 LP: 2× drive.
- **Real-time cap:** the device's interpolation limit applies here too. A device limited to LINEAR does no oversampling,
  whatever the song asks for.
- **Exports:** export, stems and resample use the song's saved mode (as in spec 14), so they get the 2× drive even above
  the limit.

## Where

- **The drive module in the instrument chain**, wherever it runs: mono and stereo paths. `odPrev` (the fading-out
  setting) is oversampled the same way.
- **The sample editor's destructive DRIVE** runs offline, so it always uses the oversampled drive, with its delay
  removed so the edit stays in place. Built at 2× (2026-11-27): 4× through two stages has a delay that isn't a whole
  frame, so the edit would shift by half a frame. Revisit if 2× is not clean enough there.
- **Unchanged:** crush (its aliasing is its sound), the Studio's own drives, and every other effect.

## CPU

- Only voices with drive on pay anything. The module is skipped when its amount is 0.
- A driven voice costs roughly 25–50% more: two drive evaluations plus half-band up/down filters, about 50 multiply-adds
  per sample. This is an estimate to measure.
- **Handhelds:** the RGB20S measurement in spec 14 includes 8 tracks with drive on in its CUBIC test. Handhelds start on
  CUBIC only if both fit together.
- **Real-time rules:** no allocation and no locks in the audio callback. Filter state lives in the module beside `od`.

## Tests

- LINEAR or NONE is bit-identical to today's drive on a fixed render.
- 2× drive: a high sine through heavy drive has aliasing energy below a set bound and clearly below today's.
- A device limit of LINEAR turns oversampling off in real time; an export still uses 2×.
- The sample editor DRIVE at 4× runs and is applied once.

## Out of scope

- A separate drive-quality setting.
- Oversampling of other effects.
- Per-track insert effects (their own roadmap item, where distortion costs one instance per track).

## Help

- `DRV` help (`fx_helper.h`) and the instrument chain DRIVE help (`help_text.h`): one line saying it is cleaner on
  CUBIC and above.
- Manual: a sentence in the interpolation section.
