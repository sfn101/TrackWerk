# Fake stereo loads as mono

Agreed 2026-11-19, detailed 2026-11-20 after a grill-me session.

## Goal

Many "stereo" samples have the same audio on both sides. Samples are 32-bit float in RAM, so each one costs double the
memory for nothing. Keeping one channel halves that, with no audible change.

## Rule

- When a stereo sample whose left and right channels are **bit-identical** (exact copies only, no tolerance) is stored in
  an instrument slot, only one channel is kept.
- **Where:** the two load points that call `setSampleBuffers`:
  - WAV: `engine-samples.cpp`, after decoding.
  - Compressed formats (mp3, flac, ogg, opus, mp4): `engine-samples.cpp`, after `pcm.release`.
- **How:** compare L and R. If they are identical, free R and pass a null right buffer (`samplesRight[id] == nullptr`
  means mono). The comparison runs at load time on the loading thread, never on the audio thread.

## Covered

Every sample stored in an instrument:
- picked in the file browser;
- loaded with a song;
- imported from Studio SOURCE;
- recorded takes;
- resamples;
- chopped slices.

Not covered: the file-browser audition (START on a file), which plays once and is discarded.

## Sound

- Mono and identical-stereo samples produce bit-identical output.
- Mixing (`engine-mix.cpp`):
  - Mono sets `procR = procL`.
  - The instrument chain's per-channel modules keep separate but identical L/R state.
  - Pan uses the same constant-power gains in both paths.
  - Sends follow the same pattern.
- Read from the code. The test below checks it by rendering both.

## Files

- The original file is never touched.
- SAVE or OVERWRITE from the sample editor writes what's in memory: a mono WAV.
- COLLECT copies the original file unchanged.

## Display

- The sample editor shows the channels as `MONO (L=R)` for such a sample.
- The log notes each one at load: the file, and the memory saved.

## Tests

- **Render test:** play a stereo sample with identical sides as stereo, and the same sample as mono. Use pan, an
  instrument chain (filter, crush, EQ, drive) and sends. The output is compared bit for bit.
- **Load test:** an identical-sides WAV and an identical-sides compressed file become mono. A WAV whose two sides differ
  by one sample value stays stereo.
- **Save test:** SAVE writes a 1-channel WAV.

## Out of scope

- Near-identical channels.
- A question at load time.
- Changing files on disk.

## Help

- The sample editor's channel/SOURCE help (`help_text.h`) explains `MONO (L=R)`.
- Manual, Sample files: a sentence on identical stereo files loading as mono.
