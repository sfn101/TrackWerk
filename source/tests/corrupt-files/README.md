# Damaged test files

Each file is damaged on purpose. Numbers are findings in `AUDIT.md`. The app must refuse every one with a status message and must not crash or hang. The song that is open stays as it was.

| File | What's wrong | What the app does now |
|---|---|---|
| `03-bad-instrument-id.ptp` | instrument `id` 5000000 | `LOAD FAILED: FILE DAMAGED` |
| `04-short-steps.ptp` | a phrase with 0 steps | `LOAD FAILED: FILE DAMAGED` |
| `05-zero-tempo.ptp` | tempo 0 | `LOAD FAILED: FILE DAMAGED` |
| `05-huge-tempo.ptp` | tempo 1000000 | `LOAD FAILED: FILE DAMAGED` |
| `06-looping-chunk.wav` | chunk size 0xFFFFFFF8 (hung forever on Windows) | `SAMPLE FAILED: FILE DAMAGED` |
| `06-looping-chunk.sf2` | the same, in a SoundFont | `SAMPLE FAILED: FILE DAMAGED` |
| `07-huge-size.sf2` | a 70-byte font whose table claims 3.8 GB | `SAMPLE FAILED: FILE DAMAGED` |
| `16-negative-rate.wav` | negative sample rate | `SAMPLE FAILED: FILE DAMAGED` |
| `17-negative-groove.ptp` | groove step -6 | `LOAD FAILED: FILE DAMAGED` |

The reason for each refusal goes to `trackwerk-log.txt`.

**Manual test:** copy a `.ptp` file into `Documents\TrackWerk\Projects` and LOAD it. To test a sample file, load it from INSTRUMENT.

**Automated test (Windows),** from `source/`:

```
cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON -DPT_SAFETY_CHECK=ON
cmake --build shell/build --config Release
shell\build\native\Release\ptcorrupt-files-check.exe tests\corrupt-files
```

On Linux, `-DPT_STUDIO_CHECK=ON` builds the same check as `build/host/native/ptcorrupt-files-check`. CI runs it on both systems, together with `ptload-save-prompts-check` (the LOAD and SAVE prompts).
