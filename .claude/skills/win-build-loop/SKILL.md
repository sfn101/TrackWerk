---
name: win-build-loop
description: Runs the native Windows MSVC build via CMake, parses compiler/linker errors, and applies minimal Windows fixes iteratively.
---
When invoked to build or fix compilation on Windows:
1. Build from `source/` with the Visual Studio generator (it finds MSVC itself, so no Developer Prompt or `vcvars64.bat` is needed):
   - `cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON`
   - `cmake --build shell/build --config Release`
   - Run long builds in the background. If `cmake` or the generator is missing, stop and tell the user what to install (see `PORT_PLAN.md`).
2. Group errors by root cause: MSVC compiler errors (`file(line): error Cxxxx`), linker errors (`LNKxxxx`, unresolved external symbols), and CMake configure errors. Fix the first error of each group first. Later ones are often consequences.
3. **Fix Rules**:
   - Never break the Linux or Android builds. Isolate Windows changes behind `#ifdef _WIN32` (or `_MSC_VER` for compiler-specific code) and `if(WIN32)` / `if(MSVC)` in CMake, matching the guards already in the tree.
   - Prefer the standard library, SDL2, or a fix in existing shared code over new Win32 wrappers.
   - Fix unresolved externals by checking the target's `target_link_libraries` (Windows SDK import libs such as `winmm`, `ole32`, `shell32`) before altering logic.
   - No game-logic, UI or feature changes.
4. Re-run the build after each batch of fixes until `shell/build/Release/pockettracker-sdl.exe` is produced. Then run `powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1` and report its verification output as-is.
