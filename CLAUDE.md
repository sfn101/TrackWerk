# TrackWerk

A music tracker (8 tracks, phrases → chains → song) for Android, Linux handhelds (TrimUI Brick, Miyoo, RGB20S, PortMaster), Linux desktop and Windows. GPL-3.0-or-later. This repo is the fork `sfn101/TrackWerk` of `kidbee85-beep/TrackWerk`, which is itself a fork of PocketTracker.

The work here is bug fixes, new features and improvements, **for every platform**. Changes are tested on Windows first and on the other platforms afterwards. Keep diffs small and reviewable.

## Layout (all under `source/`)

| Path | What |
|---|---|
| `native/` | The shared C++17 engine: audio engine, decoders, effects, voices |
| `native/songcore/` | Song model, `.ptp` load/save (`project_io.h`), sequencer (`scheduler.cpp`), WAV writer |
| `native/ui/` | The portable UI (`pt-ui`): screens in `ui/modules/`, input in `ui/dispatch/`, software canvas, fonts |
| `native/studio/` | Studio synths (`pt-mutable`: Mutable Instruments engines), M8 import |
| `native/vendor/` | Third-party code. Edit only when unavoidable; see the rules below |
| `shell/` | Per-platform shells around the same `app.cpp`: `sdl/` (desktop/handheld), `windows/` (WinMM MIDI, ASIO), `linux/` (ALSA, sources, recorders), `android/` |
| `app/` | Android Gradle app (Kotlin) |
| `tests/studio/` | Host check programs (`pt*`), built with `-DPT_STUDIO_CHECK=ON` (Linux only) |

## Build and run on Windows

From `source/`, using Visual Studio 2022 Build Tools and CMake. If `cmake` isn't on PATH in this shell, use `"C:\Program Files\CMake\bin\cmake.exe"`.

```
cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON   # once
cmake --build shell/build --config Release
```

- **Output:** `shell/build/Release/pockettracker-sdl.exe`.
- **Build times:** the first build takes about 5 minutes; after that, only changed files recompile.
- **SDL2 flag:** `-DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON` is required on this machine, because MSYS2's MinGW SDL2 is installed and MSVC can't link it.
- **Running:** close any running `pockettracker-sdl`/`TrackWerk` process before rebuilding, or the link fails.
- **Log:** user data and the log are in `%USERPROFILE%\Documents\TrackWerk\`, with each session's output in `trackwerk-log.txt`.
- **Packaging:** `powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1` writes `build/windows/TrackWerk-<version>-windows-x64.zip` and checks it: static runtime, no DLLs, icon, version and licence notices.

Use the `win-build-loop` skill for the build → fix → run loop.

## Other platforms

- **CI:** `.github/workflows/build.yml` builds the Android APK and the Linux SDL shell, and runs the host checks, on every push to `main` and on every PR. This is how changes made on Windows get verified on Linux and Android, so check the run after pushing (`gh run list`).
- **Device builds:** the Linux handhelds are cross-built with Zig toolchains; see `BUILD-TRACKWERK.txt`. Android is built with Gradle from `source/` (`./gradlew :app:assembleDebug`).
- **Version:** read from `versionName` in `source/app/build.gradle.kts` for every platform.

## Rules that keep every platform working

- **One codebase.** Logic goes in the shared `native/` code, never in a per-platform copy. Platform differences belong in `shell/<platform>/` or behind a guard.
- **Guards:**
  - `#ifdef _WIN32` / `__linux__` / `__ANDROID__` for OS differences, and `_MSC_VER` only for compiler differences.
  - In CMake, `if(MSVC)` / `else()`: every compiler flag needs an MSVC branch and a GCC/Clang branch. MSVC rejects GCC flags (`-W…`, `-include`) and ignores `-f…` flags.
- **MSVC isn't GCC.** No `__attribute__`, `__builtin_*`, zero-size arrays, VLAs or designated initializers in code compiled for Windows. `long` is 32-bit on Windows: use `int64_t`, `_fseeki64` / `_ftelli64` for file offsets.
- **Floating point must be exact in songcore and the UI.** `songcore`, `pt-ui` and `pockettracker-sdl` are compiled with `-fno-fast-math -ffp-contract=off` (`/fp:precise` on MSVC), because their results are compared bit-for-bit against recorded goldens. Never add fast-math or a directory-wide float flag. Set flags per target.
- **The audio callback is real-time.** In code run by the audio thread, no allocation, no locks the UI can hold for long, no file I/O and no `printf`. Data crosses threads through the existing queues and seqlocks (`note-queue.h`, `songcore/seqlock.h`).
- **Song files are untrusted input.** Range-check every index, size and count read from `.ptp`, WAV, SF2 or presets before using it.
- **Vendored code:** prefer a CMake flag or a shim (as `native/studio/msvc_compat.h` does). If a vendor file must change, guard the change and mark it with a `// TrackWerk:` comment so it survives a re-vendor.
- **Licence notices:** a new third-party component needs its notice in `docs/licenses/THIRD-PARTY-NOTICES.md`. `build-windows.ps1` fails if it's missing.

## Roadmap and specs

`specs/ROADMAP.md` is the order of work. When asked to "start" or "continue" without a named task, take the first item not marked Done, read its spec, and build from it. When an item is finished (built, tested on Windows, CI green), mark it Done in the roadmap in the same branch.

New features start with a `grill-me` session. Once the user confirms the design, write it to `specs/<feature-name>.md`: the goal, the controls and screens, file-format and platform effects, and what's out of scope. Then build from that spec. Bug fixes from `AUDIT.md` skip this step.

## Known bugs

`AUDIT.md` (repo root) lists 20 audited findings with file:line and suggested fixes, starting with the song-file crashes and the lost-work paths in LOAD and SAVE. Work through it before adding risky features near those paths. Its findings 3–6 were reported upstream as kidbee85-beep/TrackWerk#2.

## Git workflow

- **Remotes:** `fork` is `sfn101/TrackWerk`, where all work is pushed; local `main` tracks `fork/main`. `origin` is upstream, `kidbee85-beep/TrackWerk`.
- **Day-to-day work:** branch from `main`, commit and push to `fork`.
- **Contributions to upstream:** branch from `origin/main` with only that change, push to `fork` and open a PR to `kidbee85-beep/TrackWerk`. The Windows build fix is PR #3, from branch `windows-msvc-build`.
- **Releases:** publish Windows releases on the fork as unofficial builds, tagged at the exact commit the exe was built from.
