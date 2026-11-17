# TrackWerk 1.2.0

**Small machine. Serious music.** An eight-track music studio for Android, selected Linux handhelds and Windows, by **Kid Bunka**.

> **This fork adds a native Windows build.** TrackWerk 1.2.0 for Windows 10/11 x64: one self-contained `TrackWerk.exe`, nothing to install. [Download for Windows](https://github.com/sfn101/TrackWerk/releases/tag/v1.2.0-windows). It's an unofficial community build from this fork's source, not an official Kid Bunka release; see [Windows build](#windows-build-this-fork).

[Website](https://kidbee85-beep.github.io/TrackWerk/) · [Illustrated English guide](https://kidbee85-beep.github.io/TrackWerk/guide.html) · [Download 1.2.0](https://github.com/kidbee85-beep/TrackWerk/releases/tag/v1.2.0) · [New 1.2 update film](https://youtu.be/8DiovzmRM6w) · [YouTube Short](https://www.youtube.com/shorts/w2DIKKX_xjc) · [Instagram Reel](https://www.instagram.com/reel/Df0bTJlR86j/) · [Optional support](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&currency_code=CHF)

![TrackWerk / Hardware - Bunka Signal](docs/site/assets/screens/scene-39.png)

## New in 1.2.0

- Twelve-tab sound browser: 2,048 instrument presets, independent preview, favourites, recent sounds and protected replacement.
- Interactive First Beat tutorial and contextual help.
- Clearer source/provider/genre selection and progressive preview for supported MP3 downloads.
- Targeted MIDI Panic and USB output-selection reliability fixes. Direct iPhone capture still needs physical validation.
- Native Linux desktop packages for ARM64 and x86-64, with separate user data. Read the [1.2 guide and exact test limits](https://kidbee85-beep.github.io/TrackWerk/update-1.2.html).

## A demo party in your pocket

Write sixteen steps, join phrases into chains, then arrange them over eight tracks. Find a recording, chop a motif, perform a transition and resample the result. TrackWerk connects the tracker to a sampler and a synth laboratory, with a workflow built for buttons.

- **40 complete Scenes / 10 families.** Terminal, BBS, DOS, raster and vector looks meet Daylight and Hardware. Choose among 54 palettes and independent typefaces. Cursor glide, soft scrolling and animated controls are optional; Motion can be disabled.
- **Sampling with context.** Browse archive recordings on supported connected profiles, preview a source, set a range and open the exported result in the Sample Editor. Keep creator, source and licence information with your material.
- **Studio synthesis.** 123 models with sixteen variation positions, including SID Chip, DCO Poly, FM SysEx banks, spectral and granular models. Load DX7 voice banks and save the selected FM voice with the song.
- **Ideas you can edit.** Drumlab suggests rhythms from your sounds. Harmony supplies 48 progression templates, locks and phrase output; its preview uses loaded samples, SoundFonts or external MIDI.
- **Performance with a way home.** Eight Punch banks, internal mix recording, track mute/solo, effects, mastering, mix and stem export. Hold a momentary move, release it, then record the good accident.
- **A proper handbook.** Illustrated English workflows, all forty Scenes, detailed tracker commands and practical installation/update instructions. The new two-minute update film uses native audio and English captions; the illustrated core handbook remains available.

## Choose your device

| Device | Package / operating system | Notes |
| --- | --- | --- |
| TrimUI Brick | KNULLI | Native ARM64 profile |
| Miyoo Mini | Onion / offline | Local media and synthesis; Apps and Ports label TrackWerk 1.2.0 |
| Miyoo Mini Plus | Onion / Archive | Supported online sources; optional offline installation is labelled separately |
| Powkiddy RGB20S | AmberELEC | Games-card package, preserving projects and settings |
| Linux desktop | Debian 13 .deb / SDL 2.32.4+ | ARM64 and x86-64; final desktop UI acceptance remains open |
| Android | Signed APK / Android 8+ | ARM64, ARMv7 and x86_64; versionCode 102000; same app ID and signer as 1.0 |
| Windows | 10/11 x64, portable ZIP | [Unofficial build from this fork](https://github.com/sfn101/TrackWerk/releases/tag/v1.2.0-windows); keyboard, gamepad, WASAPI or ASIO audio |

Use First Install for a new card and Program Update for an existing installation. Back up the data folder and follow START-HERE inside the matching ZIP. The Miyoo updater recognises the previous public and known development binaries, using canonical or legacy folders without a rename. Complete the metadata step in START-HERE-1.2.0 so the menu/version labels match. Install the Android APK over the existing app to retain its data. Do not uninstall just to update.

The factory library remains the same 2,432-preset collection; its unchanged 1.0 archive is included in the 1.2 release. Five public demo projects use internal synth engines. Sample-based soundtrack compositions are mixed audio; commercial raw sample-library files are excluded.

Networking, MIDI and external audio inputs depend on the platform and hardware. YouTube is off by default and requires an explicit opt-in. Check the actual recording's reuse terms before releasing sampled music. There is no universal polyphony or CPU claim: the workload depends on the project and device.

## Windows build (this fork)

Download `TrackWerk-1.2.0-windows-x64.zip` from the [Windows release](https://github.com/sfn101/TrackWerk/releases/tag/v1.2.0-windows), unzip it anywhere and run `TrackWerk.exe`. Songs, samples and settings live in `Documents\TrackWerk`, and the session log is written to `Documents\TrackWerk\trackwerk-log.txt`.

- **Controls:** the arrows or WASD act as the D-pad, K/Enter is A, J/Escape is B, U/I are L/R, Space is START and F10 quits. A gamepad works too.
- **Audio:** SETTINGS > AUDIO OUT offers SYSTEM (WASAPI shared mode) or any installed ASIO driver.
- **Build from source:** use Visual Studio 2022 Build Tools (Desktop development with C++) and CMake, from `source/`:

  ```
  cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON
  cmake --build shell/build --config Release
  powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1
  ```

The MSVC build fixes are on the [`windows-msvc-build`](https://github.com/sfn101/TrackWerk/tree/windows-msvc-build) branch and were proposed upstream in [kidbee85-beep/TrackWerk#1](https://github.com/kidbee85-beep/TrackWerk/issues/1). The build has been launched and checked for audio and gamepad input; not every feature has been tested on Windows. Linux-only features (archive/radio sources, iPhone and sample recorders) aren't available.

## Source, builds and credits

TrackWerk is a GPL-3.0-or-later fork of [PocketTracker by conanizer and contributors](https://github.com/conanizer/pockettracker). The upstream tracker, sample engine and button workflow are its foundation. See [BUILD-TRACKWERK.txt](BUILD-TRACKWERK.txt), [source](source/) and the [credits](https://kidbee85-beep.github.io/TrackWerk/credits.html). The release provides complete corresponding application source, licences and the exact Miyoo SDL source archive.

Selected DSP work uses open Mutable Instruments code by Emilie Gillet and contributors, alongside the other components listed in source/CREDITS.md and source/docs/licenses. Interface captures are genuine native renders; the film's navigation is scripted and device frames are illustrations. Soundtrack production credits identify the recordings and sample libraries used.

## More music tools by Kid Bunka

- [Mutable Dreams](https://kidbee85-beep.github.io/mutable-dreams/) — generative instruments and a Web Studio for the M-VAVE FM-1.
- [Performance Garden](https://github.com/kidbee85-beep/mutable-dreams-performance-garden) — the Mutable Dreams instrument for VIBE FM.
- [bazillOS](https://kidbee85-beep.github.io/bazillOS/) — independent firmware for the Polyend Tracker Mini.

[Optional donations](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&currency_code=CHF) support firmware updates, new firmware and synthesizer software.
