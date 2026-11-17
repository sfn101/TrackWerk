# TrackWerk → native Windows `.exe` (MSVC) — Build Plan

Static audit only; nothing has been compiled yet. Paths are relative to `source/`.

## 0. Summary

The tree already contains a complete Windows target built with **MSVC + CMake**. Expected source changes: **none**. The work is installing the toolchain, running the existing build, and fixing only what the first real compile reports.

What's already in place:

- `shell/windows/`: WinMM MIDI in/out, ASIO output, the audio-output switch, `TrackWerk.ico`, the manifest (UTF-8 code page) and `trackwerk.rc.in` (icon + version stamp)
- `shell/CMakeLists.txt`, `if(WIN32)` / `if(MSVC)` blocks:
  - static CRT (`CMAKE_MSVC_RUNTIME_LIBRARY`)
  - GUI subsystem (`/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup`)
  - `/MANIFEST:NO`, so the `.rc` manifest is the only one
  - `/fp:precise`
  - `winmm`
  - the ASIO static lib
- `native/CMakeLists.txt`, MSVC arms:
  - `_USE_MATH_DEFINES`, `/fp:precise`, `/W4`
  - `_CRT_SECURE_NO_WARNINGS`
  - links `shell32`/`ole32`
  - no `libm` on Windows
- `shell/build-windows.ps1`: packages `TrackWerk-<version>-windows-x64.zip` and checks that the exe is:
  - x64
  - statically linked to the VC runtime and to SDL2
  - free of DLLs beside it
  - carrying the right icon and version
  - shipped with all licence notices

## 1. Build system

| Item | Finding |
|---|---|
| Build system | CMake ≥ 3.22.1; entry point `shell/CMakeLists.txt` → `add_subdirectory(../native)` |
| Target | `pockettracker-sdl` (packaging renames it `TrackWerk.exe`) |
| Generator | Visual Studio 17 2022, x64 (multi-config: the exe lands in `shell/build/Release/`) |
| Version | `app/build.gradle.kts` `versionName`, parsed by `native/cmake/pt_version.cmake` |
| Autotools / Gradle | Third-party trees and Android only, so not used here |

## 2. Dependencies

| Dependency | Provided by | Action |
|---|---|---|
| SDL2 2.30.9 | `FetchContent` (git clone at first configure), linked statically | Needs Git on PATH (installed) |
| Steinberg ASIO SDK 2.3 (host side) | **Vendored** in `native/vendor/asio` (GPLv3 option) | None: no download, no Steinberg account |
| libogg, libopus, opusfile, FAAD2, minimp4, dr_mp3, dr_flac, stb_*, TinySoundFont, nlohmann/json, kissfft, Mutable/stmlib, DaisySP, soundpipe, freeverb | Vendored, built from source | None |
| winmm, ole32, advapi32, shell32 | Windows SDK import libs | Comes with the Windows SDK |

## 3. Toolchain to install

| Tool | Status on this machine | Install |
|---|---|---|
| Visual Studio 2022 Build Tools: workload "Desktop development with C++" (MSVC v143 x64, Windows 11 SDK) | **missing** | `winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` |
| CMake ≥ 3.22.1 on PATH | **missing** | `winget install Kitware.CMake` |
| Git | present | — |

Open a new terminal afterwards so PATH picks up CMake. The Visual Studio generator finds MSVC by itself, so no `vcvars64.bat` / Developer Prompt is needed.

## 4. Build commands (from `source/`)

```bash
cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON
```

```bash
cmake --build shell/build --config Release
```

```bash
powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1
```

`-DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON` matters on this machine. MSYS2's SDL2 (MinGW libraries, unusable by MSVC) is installed at `C:\msys64\ucrt64`. If CMake ever finds it, the link breaks. The flag forces the FetchContent SDL that `build-windows.ps1` expects (`shell/build/_deps/sdl2-src/LICENSE.txt`).

Output: `shell/build/Release/pockettracker-sdl.exe`, then `build/windows/TrackWerk-1.2.0-windows-x64.zip`.

## 5. POSIX audit (why no source changes are expected)

Every POSIX-only include and call in the code the Windows build compiles is already behind a `_WIN32` / `__linux__` check, or sits in files CMake leaves out off Linux:

| File | POSIX usage | Windows branch |
|---|---|---|
| `native/common/byte_source.cpp` | `unistd.h` | `<io.h>` |
| `native/common/local_wav_source.cpp` | `fseeko`, `lstat` | `_fseeki64` / `_ftelli64` |
| `native/common/platform_memory.cpp` | `/proc/meminfo` | Win32 memory API |
| `native/songcore/wav_writer.h` | `getpid`, `kill` | `_getpid` |
| `native/studio/granular_disk_take.cpp` | `fcntl`, `fsync`, `lstat` | 18 `_WIN32` branches |
| `native/ui/live_resample.cpp` | `renameat2` syscall | `MoveFileW` |
| `native/ui/modules/file_browser.cpp` | `localtime_r` | `localtime_s` |
| `shell/main.cpp` | `dup2`, `isatty`, `setenv` | `_dup2`, `AttachConsole` |
| `shell/sdl/sdl-audio-engine.cpp`, `shell/startup-log.h`, `shell/sdl/sdl-video.cpp` | pthread, `setenv`, `sys/stat` | guarded out |
| `shell/linux/*`, `shell/sdl/linux-audio-outputs.cpp` | ALSA, `dlopen`, `fork`, sockets | empty TU off Linux, or only added `if(UNIX)` |

No hardcoded `/tmp`, `/dev` or `.so` paths in Windows-compiled code. User data goes to `Documents\TrackWerk\…`.

## 6. Risks and unknowns

1. **Never verified at 1.2.0 on MSVC.** The comments describe CI and packaging runs on Windows, but nothing in the tree proves this version still compiles there. New 1.2 code that only Linux/Android builds have exercised is the most likely source of first-build errors.
2. **Most likely first build error: `native/studio/CMakeLists.txt:16-18`.** The Mutable Instruments synth library (`pt-mutable`, new in 1.2, always linked into the exe) is given GCC-only flags with no MSVC branch: `-fno-fast-math -ffp-contract=off -Wno-narrowing -Wno-unused-local-typedef -include cstdio`. MSVC ignores the `-f`/`-W` ones with D9002 warnings. `-include cstdio` may end up treating `cstdio` as a source file, and code that relied on it may then miss `printf`. Fix if it breaks: put the flags under `if(NOT MSVC)` and give MSVC `/FI cstdio /fp:precise`. The Linux/Android builds are unchanged. See `AUDIT.md` #9.
3. **First configure needs network** for the SDL clone (shallow, one tag).
4. **`PT_WERROR` stays OFF** (the default), so `/W4` warnings do not fail the build.
5. **Do not enable `PT_STUDIO_CHECK`** for the first build. Several of those test targets are `UNIX`-only.

## 7. Fix rules if the build fails

- Fix the root cause once, in the shared code; never break the Linux/Android builds.
- Isolate Windows changes behind `#ifdef _WIN32` (or `_MSC_VER` for compiler-specific code), matching the existing guards.
- No game-logic, UI or feature changes.

## 8. Out of scope

- Linux-only services that are absent on Windows by design: source sampler / YouTube, iPhone recorder, sample recorder, ALSA routing.
- `shell/windows/README.txt` says a console window opens; the build actually uses the GUI subsystem and writes `trackwerk-log.txt`. That's a pre-existing doc mismatch and is not fixed here.
