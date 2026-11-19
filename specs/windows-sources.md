# Studio SOURCE on Windows (port note)

Agreed 2026-11-19. A port of an existing feature, no new controls or screens, so no grill-me session: this note records the decisions only.

## Goal

Studio SOURCE (Internet Archive, Wikimedia Commons, radio, SomaFM, YouTube) works on Windows exactly as on Linux. Until now the Windows build had no SOURCE backend and showed `OFFLINE`.

## How

- **One codebase.** Windows builds the same `shell/linux/source-sampler.cpp` as Linux, with `#ifdef _WIN32` branches for the Linux-only parts: loading libcurl, starting yt-dlp and FFmpeg (`CreateProcess` in a job object, so a cancel also stops yt-dlp's own child), finding the helpers, the radio socket hook, and a few file calls (`_commit`, `_chsize_s`, `_fseeki64`). Android keeps its own Kotlin backend.
- **libcurl inside the exe:** curl 8.11.1 by FetchContent, static, HTTP/HTTPS only, Schannel TLS (Windows' own certificate store). No DLL; the release zip stays one exe plus `tools\`.
- **Helpers shipped in `tools\` beside the exe**, so the user never downloads anything:
  - `yt-dlp.exe`: the official 2026.10.26 build (the Linux version), pinned by SHA-256. 23.6 MB.
  - `qjs.exe`: QuickJS from the same source as Linux (`external/quickjs-2026-06-04.tar.xz`), the JS engine yt-dlp needs for YouTube. 1.2 MB.
  - `ffmpeg.exe`: a minimal FFmpeg 7.1 (LGPL) that can only copy the audio of a fragmented MP4 into a plain one, the one job SOURCE gives it. 1.5 MB. Its signed source is in `external/`.
  - `shell/windows/fetch-helpers.ps1` makes them (needs MSYS2 with UCRT64 gcc, make, diffutils); the build copies them beside the dev exe and `build-windows.ps1` puts them in the zip.
- **Licences:** notices in `THIRD-PARTY-NOTICES.md` and `HELPER-SOURCES.md`; the zip carries each licence text, and `build-windows.ps1` fails without them.

## Out of scope

- Recording on Windows (its own roadmap item); the recorder stays Linux/Android.
- Updating yt-dlp automatically. YouTube changes break old yt-dlp versions; a new pin is a one-line change in `fetch-helpers.ps1` and `build-windows.ps1`.
- Building the helpers in CI. CI builds and checks the app; the helpers are made on the release machine.

## Tests

- Built-in libcurl fetches an Internet Archive search over HTTPS (checked 2026-11-19).
- yt-dlp + qjs search YouTube and download audio; the minimal FFmpeg repacks it (checked 2026-11-19).
- In the app: each provider searches, previews and imports a sample (by hand on Windows).

## Help

No new controls or messages. The Windows README describes `tools\`. The manual has no SOURCE section yet (a gap for the help pass, not this port).
