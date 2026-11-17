# TrackWerk Repo Audit

Date: 2026-11-16. Read-only audit; no code was changed. Paths are relative to `source/`.

**What this repo does:** TrackWerk is a music tracker for game-style handhelds, Android and Windows. One C++ engine and UI (`native/`) is shared by every platform, with a thin shell per platform (`shell/`). Songs are JSON `.ptp` files.

**Load assumed:** one person, one app process, with a real-time audio thread beside the UI thread.

**How verified:** findings marked *reviewer-only* came from parallel reviewers and were not re-read; all others were checked against the code.

## Must fix

1. **LOAD discards unsaved work and its crash backup:** `native/ui/dispatch/screens/project.cpp:66-68`
   - **Problem:** NEW, EXIT and FIRST BEAT ask before discarding; LOAD doesn't. Loading also clears `autosave.ptp` (`load_project_done`).
   - **Fix:** `if (s_.project_dirty())` open a confirm dialog before the browser, like NEW.
2. **SAVE silently overwrites another song with the same name:** `native/ui/project_actions.cpp:47-52`
   - **Problem:** The path is always `Projects/<name>.ptp`, with no existence check. Two "UNTITLED" songs overwrite each other; on Windows, "Song" and "SONG" collide.
   - **Fix:** If the target exists and isn't `s.projectPath`, ask first, or pick a free name (`free_first_beat_name`).
3. **Unchecked instrument `id` corrupts memory on play:** `native/songcore/engine_setup.h:459,463`
   - **Problem:** `routing.sfSlot[ins.id]` (128 entries) uses the id from the file; line 81 of the same file checks the range, these don't. `"id": 5000000` reads and writes out of bounds.
   - **Fix:** `if (ins.id < 0 || ins.id >= POOL_INSTRUMENTS) return false;` in `sync_instrument_soundfont` / `request_instrument_soundfont`.
4. **Short phrases crash playback:** `native/songcore/project_io.h:127-135`, `scheduler.cpp:845`
   - **Problem:** `parse_phrase` keeps whatever step count the file has; the scheduler always reads 16, so `"steps":[]` reads past the end.
   - **Fix:** `p.steps.resize(16);` at the end of `parse_phrase` (as `parse_chain` does).
5. **Unvalidated tempo divides by zero:** `native/songcore/project_io.h:368`
   - **Problem:** `"tempo": 1000000` makes `frames_per_step` 0, and `scheduler.cpp:353` then divides by it. 0 or a negative tempo is also broken. `tempo_in_force` doesn't clamp.
   - **Fix:** `p.tempo = std::clamp(get_int(j,"tempo",p.tempo), 20, 999);`
6. **Crafted WAV hangs forever on Windows:** `native/engine-samples.cpp:127,134`; also `soundfont-trim.cpp:96,101,108,172` and `engine-soundfont.cpp:20`
   - **Problem:** `fseek(f, (long)chunkSize, SEEK_CUR)`. `long` is 32-bit on MSVC, so 0xFFFFFFF8 becomes −8 and the loop never ends.
   - **Fix:** `if (chunkSize > INT32_MAX) break;` (as `wav_writer.h:637` does); `if (count > INT32_MAX) return 0;` in `sfStreamSkip`.
7. **Tiny SF2 asks for 4 GB:** `native/soundfont-trim.cpp:113,333,351` *(reviewer-only)*
   - **Problem:** Chunk sizes from the header are never compared with the file size; the `bad_alloc` is uncaught, so the app quits.
   - **Fix:** Reject sub-chunks larger than the bytes remaining.

## Should fix

8. **Save fallback can destroy both files:** `native/ui/std_filesystem.cpp:226-234`
   - **Problem:** When the rename fails, it does `copy_file(overwrite)` and then deletes the temp even if the copy failed. On Windows the rename fails when OneDrive or antivirus has the file open.
   - **Fix:** Drop the copy; on rename failure, remove the temp and return false.
9. **Windows build is not in CI; the new `pt-mutable` flags are GCC-only:** `native/studio/CMakeLists.txt:16-18`, `.github/workflows/build.yml`
   - **Problem:** `-fno-fast-math -ffp-contract=off -Wno-narrowing -Wno-unused-local-typedef -include cstdio` are applied on MSVC too.
   - **Fix:** Wrap them in `if(NOT MSVC)`; on MSVC use `/FI cstdio /fp:precise`.
10. **No fsync before the rename that replaces the file:** `native/ui/std_filesystem.cpp:211-226`, `songcore/wav_writer.h:357,502` *(reviewer-only)*
    - **Problem:** Power loss after a save can leave the song empty.
    - **Fix:** `fflush` + `fsync` (`_commit` on Windows) before the rename, as `granular_disk_take.cpp:97-103` does.
11. **Android SAF save deletes the old file before a rename that can fail:** `shell/android/saf-filesystem.cpp:673-684` *(reviewer-only)*
    - **Fix:** On rename failure, write the data directly to the real path.
12. **COLLECT merges different samples that share a file name:** `native/ui/project_actions.cpp:185-193` *(reviewer-only)*
    - **Fix:** When the name belongs to a different source, rename the copy (`kick_2.wav`).
13. **Live radio always stops at 10 minutes:** `shell/linux/source-sampler.cpp:212` (Linux)
    - **Problem:** `CURLOPT_TIMEOUT` is 600 for live streams.
    - **Fix:** `option(13, live ? 0L : ...)`; the low-speed check already catches stalls.
14. **Large downloads fail on slow Wi-Fi:** `shell/linux/source-sampler.cpp:212` (Linux)
    - **Problem:** The 180 s total limit cuts off file and progressive downloads.
    - **Fix:** Use 0 plus the low-speed pair `option(19,1L); option(20,30L);`.
15. **SIGPIPE can kill the app:** `shell/main.cpp:147`, `source-sampler.cpp:222` *(reviewer-only, Linux)*
    - **Problem:** `CURLOPT_NOSIGNAL` is set, but SIGPIPE is never ignored.
    - **Fix:** `std::signal(SIGPIPE, SIG_IGN);`, and reset it to the default in children before `execv`.
16. **Negative WAV sample rate half-loads a sample:** `native/engine-samples.cpp:139` *(reviewer-only)*
    - **Fix:** `sampleRate < 1 || sampleRate > 768000`.
17. **Negative groove steps make time run backwards:** `native/songcore/project_io.h:196-199` *(reviewer-only)*
    - **Fix:** Clamp each step to −1..255.
18. **Windows README is wrong:** `shell/windows/README.txt`
    - **Problem:** It describes a console window that doesn't exist (output goes to `trackwerk-log.txt`), and the issue link points to `conanizer/pockettracker`.

## Nice to have

19. **Background autosave can save a half-loaded song:** `native/ui/input_dispatcher.cpp:201-206` *(reviewer-only, Android)*
    - **Fix:** `if (s_.loading.running) return;` in `flush_autosave`.
20. **Note queue reserves only 64 entries:** `native/note-queue.h:157,192`, `audio-engine.cpp:37-39` *(reviewer-only)*
    - **Problem:** Growth allocates on, or blocks, the audio thread.
    - **Fix:** Reserve 1024.

**Left out (smaller):**
- note-queue cancel-ledger off-by-one (`note-queue.h:135-137`, `>` → `>=`)
- slice-marker overflow (`project_io.h:309`)
- no orphan WAV temp cleanup on Windows (`wav_writer.h:126-145`)

## Verdict

- Fix the song-file crashes (3, 4, 5, 7) and the lost-work paths (1, 2, 8) first.
- For the Windows build, 6 and 9 matter most.
- No test loads a damaged `.ptp`. One test with a few malformed song files would cover 3–5 and 17.

**Not checked:**
- the screen modules beyond these paths
- the Android Kotlin code
- the vendored decoders
- the packaging and release scripts

Nothing was compiled or run.
