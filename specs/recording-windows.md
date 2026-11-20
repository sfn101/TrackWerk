# Recording on Windows

Agreed 2026-11-19, detailed 2026-11-20 after a grill-me session.

## Goal

The Windows build has no recorder. Linux and Android implement the shared `CaptureBackend` (`native/ui/audio_capture.h`).
Windows gets one, so mic, line-in and USB interfaces record into samples there too.

## How

- **One codebase, the same approach as Studio SOURCE (windows-sources.md).** Windows builds the Linux SDL recorder,
  `shell/linux/sample-recorder.cpp` and `capture-router.cpp`. The Linux-only parts are under `#ifdef _WIN32` guards:
  - `unistd.h`;
  - `usb-audio-diagnosis.h`, the USB diagnosis, which is skipped on Windows;
  - the iPhone USB recorder (`iphone-recorder.cpp`), which stays Linux-only. On Windows, the router offers SDL inputs
    only.
- **`shell/main.cpp`:** the recorder guards become `#if !defined(PT_MIYOO_PLUS_ARCHIVE)` again for Windows, but without
  the iPhone part.
- **`shell/CMakeLists.txt`:** the `if(WIN32)` block adds these sources.
- **Audio system:** SDL capture, which is WASAPI shared mode. Recording latency doesn't matter: the take is trimmed in the
  sample editor and not played live.

## What the user gets

The Studio's Input Recorder lists the system default, mics, line-ins and USB interfaces. Recording, the take, the peak
meters and the import work exactly as on Linux.

## Errors

- If Windows blocks desktop apps from the microphone (Settings > Privacy > Microphone), the device fails to open or
  delivers only silence from a blocked endpoint. The recorder then shows
  `WINDOWS BLOCKS THE MIC: SETTINGS > PRIVACY > MICROPHONE` instead of recording silence.
- Other open failures use the existing recorder messages.

## ASIO

- No ASIO input in this item.
- The spec's test records while ASIO output (SETTINGS > AUDIO OUT) is active. If an interface locks its inputs then,
  "ASIO input" goes on the roadmap as a later idea.

## Tests

- **Host check:** the device list, and a start and stop against SDL's dummy capture driver.
- **By hand on Windows:** the built-in mic, a USB interface, ASIO output on, and the privacy block turned on.

## Out of scope

- ASIO input.
- Live input on a track (its own roadmap item).
- The iPhone USB recorder on Windows.

## Help

- No new controls.
- The Windows README and the manual's recording section say recording works on Windows, and name the privacy setting.
