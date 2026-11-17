---
name: win-build-loop
description: Builds TrackWerk on Windows with MSVC, fixes compile/link errors without breaking the other platforms, then launches the app and reads its log. Use after any code change, or when asked to build, run or test the app on Windows.
---
Run from `source/`. If `cmake` isn't on PATH, use `"C:\Program Files\CMake\bin\cmake.exe"`.

1. **Configure once.** If `shell/build/` is missing, run `cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON`. It takes a few minutes the first time because it downloads SDL2.

2. **Close a running copy** of the app (`pockettracker-sdl` or `TrackWerk` process) first, or the link fails with LNK1104.

3. **Build** in the background, logging errors only:
   `cmake --build shell/build --config Release -- /m /v:minimal "/clp:ErrorsOnly;Summary" > <scratchpad>/build.log`
   Then group the errors by root cause:
   - compiler errors (`file(line): error Cxxxx`)
   - linker errors (`LNKxxxx`, unresolved external symbols)
   - CMake errors

   Fix the first error of each group first; later ones are often follow-on errors.

4. **Fix rules.** The same code ships on Linux, the handhelds and Android, so a fix must not break them:
   - Fix the shared code once; don't patch around it in one platform.
   - Use `#ifdef _WIN32` for OS differences and `_MSC_VER` for compiler differences. In CMake, use `if(MSVC)` with an `else()` that keeps the GCC/Clang flags exactly as they were.
   - In vendored code, prefer a CMake flag or a force-included shim. If a vendor file must change, guard the change and mark it `// TrackWerk:`.
   - For unresolved externals, check `target_link_libraries` (Windows SDK libs such as `winmm`, `ole32`, `shell32`) before changing logic.
   - Never add fast-math or a directory-wide float flag (see CLAUDE.md).

5. **Repeat** until the summary shows `0 Error(s)` and `shell/build/Release/pockettracker-sdl.exe` is newer than the edit.

6. **Launch and check.** Start the exe and wait a few seconds. Confirm the process is still running, then read the new `%USERPROFILE%\Documents\TrackWerk\trackwerk-log.txt` for the audio, video and project lines and any errors. Leave the app open for the user to test, and say exactly what to try for the change you made.

7. **Other platforms.** After pushing to `main` on the fork, CI builds Linux and Android and runs the host checks. Check it with `gh run list -R sfn101/TrackWerk`, and report a failure with the failing step's log.

8. **Package only for a release.** `powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1` builds the zip and verifies it. Report its output as-is.
