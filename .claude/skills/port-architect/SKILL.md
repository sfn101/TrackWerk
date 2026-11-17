---
name: port-architect
description: Audits the source tree for Windows (MSVC) build blockers and writes a build plan.
---
When invoked, perform a static audit without modifying source files:
1. **Build System & Dependencies**: Identify the CMake files and any existing Windows/MSVC support (`if(WIN32)`, `if(MSVC)`, `.rc` files, packaging scripts). List every third-party library and say how MSVC gets it: vendored source, CMake `FetchContent`, or the Windows SDK.
2. **OS & Syscall Blockers**: Scan for POSIX-only headers and calls (`<unistd.h>`, `<sys/socket.h>`, `<pthread.h>`, `<dlfcn.h>`, `epoll`, `fork()`, `mmap`, `setenv`, `localtime_r`, `fseeko`). Report only the ones NOT already behind `_WIN32` / `__linux__` checks or excluded from the Windows build by CMake.
3. **Compiler differences**: Flag GCC/Clang-only constructs MSVC rejects (`__attribute__`, `__builtin_*`, VLAs, GCC-only flags on targets without an `if(MSVC)` arm).
4. **Filesystem & Pathing**: Flag hardcoded `/proc`, `/dev`, `/tmp`, case-sensitive paths, and `.so` extensions in code the Windows build compiles.
5. **Output**: Write `PORT_PLAN.md` with the toolchain to install (Visual Studio Build Tools + Windows SDK, CMake), the exact configure/build commands, and a prioritized list of files needing `#ifdef _WIN32` / `_MSC_VER` patches or CMake changes.
