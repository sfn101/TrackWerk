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
- **SOURCE helpers:** Studio SOURCE's YouTube needs `yt-dlp.exe`, `qjs.exe` and `ffmpeg.exe` in `shell/windows/helpers/` (git-ignored). In every new checkout or worktree, run `powershell -File shell/windows/fetch-helpers.ps1` once, or copy that folder from another checkout, then configure again. Otherwise YouTube shows greyed out.
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
- **Imported formats are read-only.** M8 (`.m8s`) and any later import (LSDJ…) are read into a copy; the app never writes, overwrites or changes those files. Saving always writes TrackWerk's own `.ptp`, so the originals stay usable on their own hardware.
- **Song files are untrusted input.** Range-check every index, size and count read from `.ptp`, WAV, SF2 or presets before using it.
- **Vendored code:** prefer a CMake flag or a shim (as `native/studio/msvc_compat.h` does). If a vendor file must change, guard the change and mark it with a `// TrackWerk:` comment so it survives a re-vendor.
- **Licence notices:** a new third-party component needs its notice in `docs/licenses/THIRD-PARTY-NOTICES.md`. `build-windows.ps1` fails if it's missing.

## Roadmap and specs

`specs/ROADMAP.md` is the order of work. When asked to "start" or "continue" without a named task, take the first item not marked Done, read its spec, and build from it. When an item is finished (built, tested on Windows, CI green), mark it Done in the roadmap in the same branch.

New features start with a `grill-me` session. Once the user confirms the design, write it to `specs/<feature-name>.md`: the goal, the controls and screens, file-format and platform effects, and what's out of scope. Then build from that spec. Bug fixes from `AUDIT.md` skip this step.

## Help and info text

Every new feature ships with its help in the same branch: its SELECT help entry in `native/ui/help_text.h` (or `fx_helper.h` for an effect command, `studio_help.cpp` for Studio), its info text in any box it adds, and its section in `source/docs/manual-en.md`. A change that alters existing behaviour corrects the help that describes it. Each spec has a **Help** section listing the entries it adds or changes; a feature isn't Done until they are written.

## Known bugs

`AUDIT.md` (repo root) lists 20 audited findings with file:line and suggested fixes, starting with the song-file crashes and the lost-work paths in LOAD and SAVE. Work through it before adding risky features near those paths. Its findings 3–6 were reported upstream as kidbee85-beep/TrackWerk#2.

## Git workflow

- **Remotes:** `fork` is `sfn101/TrackWerk`, where all work is pushed; local `main` tracks `fork/main`. `origin` is upstream, `kidbee85-beep/TrackWerk`.
- **Day-to-day work:** branch from `main`, commit and push to `fork`.
- **Pull requests:** never open one, on the fork or upstream, unless the user asks for that PR. To run CI on a branch without a PR: `gh workflow run build.yml -R sfn101/TrackWerk --ref <branch>`.
- **Releases:** publish Windows releases on the fork as unofficial builds, tagged at the exact commit the exe was built from.

## Working with upstream

The aim is that the fork can always take every upstream release, and that upstream can take any single feature of ours on its own. **Check these rules whenever upstream, a PR, an upstream issue or a merge comes up, and remind the user if a step is being skipped,** even when they ask to skip it, then do what they decide.

### Pulling upstream into the fork

- **Merge each upstream release soon after it appears:** `git fetch origin --tags`, then merge `origin/main` into `main` on a branch. Merge, never rebase: rebasing `main` would rewrite the fork's history.
- **Their code is the base.** Where both sides built the same thing, keep upstream's version and add back only what ours does extra. Nothing we built or planned may be lost, and the result must keep performance, stability and quality. If it can't, stop and ask.
- After a merge: build and test on Windows, run CI, and update the roadmap for what upstream now covers.
- **Keep song files compatible** with upstream:
  - New phrase-command codes and format version numbers must not collide with upstream's. Before using a new code, `git fetch origin` and check `songcore/effects.h` and the format numbers on `origin/main`, then take a code upstream doesn't use. Right after the command is built, tell the maintainer with a PR for that command, as promised in the message to upstream. The maintainer never has to coordinate codes for us.
  - Never raise a format number alone without telling the user what it means for files exchanged with upstream.
- Keep fork-only material in fork-only files (`specs/`, `AUDIT.md`, `CLAUDE.md`, `.claude/`), and touch upstream files no more than a change needs.

### Drafts are private

Every draft of a message, issue, comment or PR text to someone else stays on this machine only, in `drafts/` at the repo root. It's ignored through `.git/info/exclude`, so never commit, push or publish it. Only the final text goes out, and only after the user has approved it.

### Sending a feature upstream

Steps, in order. Don't skip one without the user's explicit OK:

1. **Ask first.** A new feature goes only after the maintainer has said they want it, in an issue or a reply. Bug fixes may go directly. Never post an issue, comment or PR text before the user has read and approved the exact text. Write it in the user's voice: plain, human, short, no jargon.
2. **Test on upstream's newest release.** `git fetch origin --tags` first. The PR branch starts from the newest `origin/main`: `git switch -c upstream/<name> origin/main`. Never branch from our `main`.
3. **Fully self-contained.** The branch has only that feature. It must not need anything upstream doesn't have: none of our unsent features, no fork-only files.
   - Copy the change with `git cherry-pick`, or rebuild it by hand if its commits mix in other work.
   - If the feature normally builds on another of ours (wide mode on the VI visualizer or sharp scaling, for example), it must work without it. If it can't yet, stop and tell the user.
4. **Check it.** Run `git diff origin/main --stat`: every file must belong to the feature. Never include `CLAUDE.md`, `AUDIT.md`, `specs/`, `.claude/` or roadmap commits. Then build that branch, test it by hand on Windows, and run CI on it (`gh workflow run build.yml -R sfn101/TrackWerk --ref upstream/<name>`).
   - **Hand test by the user, when one is needed.** If the feature has anything a person must try in the running app (screens, buttons, sound, files), stop after the build. Tell the user it's ready, give the exact exe to run, and list numbered steps: what to press and what they should see or hear. Then wait for their report before pushing or opening anything. Skip this pause only when nothing can be checked by hand (a pure build or test fix), and say so.
5. **Pace and order.** Send PRs the way the maintainer asks (one at a time, or all at once to pick from); until they've answered, one at a time. Follow the order in the message to upstream (`drafts/upstream-message.md`, local): the built features first, then wide mode once it fits upstream's newest screens and stands alone.
6. **Open it only when the user says so.** Push the branch to `fork` and open the PR to `kidbee85-beep/TrackWerk` `main`. The description says what it does for the user, what changed and how it was tested, written from the change itself, not from fork-only notes.
7. **Keep the branch.** Review fixes go on the same branch. Don't rebase or force-push it unless the maintainer asks. If upstream releases again before the PR is merged, merge their release into the branch and retest it.

### History

- PR #3 (Windows build fix) was taken by the maintainer as commit `b34487b` with our authorship, and shipped in 1.3.0.
- Issue #2's file-validation findings are all fixed upstream.
- The message offering our features was posted on 2026-11-20 as kidbee85-beep/TrackWerk#7 (local copy: `drafts/upstream-message.md`). It asks whether PRs should come one at a time or all at once. **No feature PR goes up before the maintainer answers there.**
- Open upstream PR: kidbee85-beep/TrackWerk#8, the Archive search crash fix (branch `upstream/archive-utf8-crash`, one commit, 3 files), sent on 2026-11-21 as a bug fix, separate from #7.
- Ready, not sent: `upstream/refuse-newer-files` (feature 1 of #7), built on upstream 1.3.0 with its own check `ptnewer-files-check`. It waits for the user's hand test and the maintainer's answer on #7.
- Our audit batches 1-2 are not to be sent: upstream fixed those itself.
