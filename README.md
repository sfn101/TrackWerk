# TrackWerk 1.3.0

**Small machine. Serious music.** An eight-track music studio for Android, selected Linux handhelds and Windows, by **Kid Bunka**.

> **This fork** (sfn101/TrackWerk) is TrackWerk 1.3.0 plus its own features, among them Studio SOURCE on Windows. The official Windows download is in upstream's [1.3.0 release](https://github.com/kidbee85-beep/TrackWerk/releases/tag/v1.3.0). To try the fork's features on Windows, build it from source: see [Windows build (this fork)](#windows-build-this-fork).

[Website](https://kidbee85-beep.github.io/TrackWerk/) · [Illustrated English guide](https://kidbee85-beep.github.io/TrackWerk/guide.html) · [Download 1.3.0](https://github.com/kidbee85-beep/TrackWerk/releases/tag/v1.3.0) · [Previous 1.2 update film](https://youtu.be/8DiovzmRM6w) · [YouTube Short](https://www.youtube.com/shorts/w2DIKKX_xjc) · [Instagram Reel](https://www.instagram.com/reel/Df0bTJlR86j/) · [Optional support](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&currency_code=CHF)

![TrackWerk / Hardware - Bunka Signal](docs/site/assets/screens/scene-39.png)

## New in 1.3.0

- **Froggy sings the words you type.** Studio > Synth > Voices and Frogs > Froggy. Type the lyrics on the on-screen keyboard (Lyrics row, A + Right) or step through templates in `Presets/Lyrics` (A + Up/Down); every note sings the next syllable. Four voices, croak, vibrato, glide, a coda for the last consonants, and on Linux a **Mic pitch** source that sings the pitch you hum into the recorder. Spelling-to-sound rules and vowel data come from [Chimera Vocal Synth](https://github.com/jamesondh/chimera-vocal-synth) by Jameson Hodge (MIT); the voice itself is our own light formant engine ([credits](docs/THIRD-PARTY-CHIMERA.md)).
- **A frog stage.** Froggy and Croaker show a neon frog beside the controls: its mouth and throat follow the real sound, it hops with each note, flicks its tongue at the firefly (the XY pad's dot, moved with L + D-pad or a finger on touch screens) and shows the sung word karaoke-style. Motion can be switched off.
- **128 synth models.** New: Croaker, Acid Box, Reese Bass and the Dub Siren, sixteen sounds each.
- **Synth tabs sorted by purpose**: Voices and Frogs, Vintage Chips, Low End and Sirens, Plaits: Macro, Braids: Digital, Rings: Strings, Elements: Modal and more. Every family has its own control sheet (dials, an LCD, a bar graph), every new model a one-line description and every preset list its position.
- **TrackWerk Sample Pack 1.3.** 800 hand-picked one-shots and breaks by Kid Bunka (100 each of kicks, snares, hi-hats, claps, bongos and congas, extras, toms and cymbals, plus 100 breaks) and 728 classic drum-machine sounds as a separate Drum Machines folder. Included in the First Install packages; existing users download the two ZIPs separately.
- **Also new:** EXPORT > MIDI, a preset inbox, the PRB step-probability command, the last five versions of a project kept on SAVE, durable saves with atomic replacement and explicit disk flushes, an AUDIO DRIVERS overlay and input monitoring (by @ozkaoz), an Anbernic R36 Ultra launcher, a PortMaster package and a community test package for Anbernic H700 handhelds.

## A demo party in your pocket

Write sixteen steps, join phrases into chains, then arrange them over eight tracks. Find a recording, chop a motif, perform a transition and resample the result. TrackWerk connects the tracker to a sampler and a synth laboratory, with a workflow built for buttons.

- **40 complete Scenes / 10 families.** Terminal, BBS, DOS, raster and vector looks meet Daylight and Hardware. Choose among 54 palettes and independent typefaces. Cursor glide, soft scrolling and animated controls are optional; Motion can be disabled.
- **Sampling with context.** Browse archive recordings on supported connected profiles, preview a source, set a range and open the exported result in the Sample Editor. Keep creator, source and licence information with your material.
- **Studio synthesis.** 128 models with sixteen variation positions, including SID Chip, DCO Poly, FM SysEx banks, the Dub Siren, Croaker and Froggy (it sings the words you type), spectral and granular models. Load DX7 voice banks and save the selected FM voice with the song.
- **Ideas you can edit.** Drumlab suggests rhythms from your sounds. Harmony supplies 48 progression templates, locks and phrase output; its preview uses loaded samples, SoundFonts or external MIDI.
- **Performance with a way home.** Eight Punch banks, internal mix recording, track mute/solo, effects, mastering, mix and stem export. Hold a momentary move, release it, then record the good accident.
- **A proper handbook.** Illustrated English workflows, all forty Scenes, detailed tracker commands and practical installation/update instructions. The new two-minute update film uses native audio and English captions; the illustrated core handbook remains available.

## Choose your device

| Device | Package / operating system | Notes |
| --- | --- | --- |
| TrimUI Brick | KNULLI | Native ARM64 profile |
| Miyoo Mini | Onion / offline | Local media and synthesis; Apps and Ports label TrackWerk 1.3.0 |
| Miyoo Mini Plus | Onion / Archive | Supported online sources; optional offline installation is labelled separately |
| Powkiddy RGB20S | AmberELEC | Games-card package, preserving projects and settings |
| Anbernic R36 Ultra | dArkOSRE (Debian 13 arm64) | Community test: arm64 .deb plus Ports launcher; not tested on an R36 Ultra |
| Anbernic H700 | KNULLI | Community test; hardware acceptance open |
| R36S | 64-bit Linux | Community test; hardware acceptance open |
| macOS Apple Silicon | macOS 12+ | Local test candidate, ad-hoc signed and not notarised |
| Windows x64 | MSVC build required | Withheld pending successful build and startup test |
| Linux desktop | Debian 13 .deb / SDL 2.32.4+ | ARM64 and x86-64; final desktop UI acceptance remains open |
| Android | Signed APK / Android 8+ | ARM64, ARMv7 and x86_64; versionCode 103000; same app ID and signer as 1.0 |
| Windows | 10/11 x64, portable ZIP | [Official 1.3.0 release](https://github.com/kidbee85-beep/TrackWerk/releases/tag/v1.3.0); this fork's features: build from source |

Use First Install for a new card and Program Update for an existing installation. Back up the data folder and follow START-HERE inside the matching ZIP. The Miyoo updater recognises the previous public and known development binaries, using canonical or legacy folders without a rename. Complete the metadata step in START-HERE-1.3.0 so the menu/version labels match. Install the Android APK over the existing app to retain its data. Do not uninstall just to update.

The factory library remains the same 2,432-preset collection; its unchanged 1.0 archive is included in the release. The Sample Pack and the Drum Machines folder are separate ZIPs for existing installations (copy the folder into your Samples folder). Five public demo projects use internal synth engines. Sample-based soundtrack compositions are mixed audio; commercial raw sample-library files are excluded.

Networking, MIDI and external audio inputs depend on the platform and hardware. YouTube is off by default and requires an explicit opt-in. Check the actual recording's reuse terms before releasing sampled music. There is no universal polyphony or CPU claim: the workload depends on the project and device.

## Windows build (this fork)

Build the fork from `source/` (below), then run `shell/build/Release/pockettracker-sdl.exe`. Songs, samples and settings live in `Documents\TrackWerk`, and the session log is written to `Documents\TrackWerk	rackwerk-log.txt`. For Studio SOURCE's YouTube, run `powershell -File shell/windows/fetch-helpers.ps1` once before configuring, so `yt-dlp`, QuickJS and FFmpeg sit beside the exe.

- **Controls:** the arrows or WASD act as the D-pad, K/Enter is A, J/Escape is B, U/I are L/R, Space is START and F10 quits. A gamepad works too.
- **Audio:** SETTINGS > AUDIO OUT offers SYSTEM (WASAPI shared mode) or any installed ASIO driver.
- **Build from source:** use Visual Studio 2022 Build Tools (Desktop development with C++) and CMake, from `source/`:

  ```
  cmake -S shell -B shell/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON
  cmake --build shell/build --config Release
  powershell -ExecutionPolicy Bypass -File shell/build-windows.ps1
  ```

The MSVC build fix from this fork is part of upstream 1.3.0 (commit [`b34487b`](https://github.com/kidbee85-beep/TrackWerk/commit/b34487bf4a6a5b6f00d98e176fec2550e009af04)). Linux-only features (the iPhone and sample recorders) aren't available on Windows yet.

## Source, builds and credits

TrackWerk is a GPL-3.0-or-later fork of [PocketTracker by conanizer and contributors](https://github.com/conanizer/pockettracker). The upstream tracker, sample engine and button workflow are its foundation. See [BUILD-TRACKWERK.txt](BUILD-TRACKWERK.txt), [source](source/) and the [credits](https://kidbee85-beep.github.io/TrackWerk/credits.html). The release provides complete corresponding application source, licences and the exact Miyoo SDL source archive.

Selected DSP work uses open Mutable Instruments code by Emilie Gillet and contributors, alongside the other components listed in source/CREDITS.md and source/docs/licenses. Interface captures are genuine native renders; the film's navigation is scripted and device frames are illustrations. Soundtrack production credits identify the recordings and sample libraries used.

## More music tools by Kid Bunka

- [Mutable Dreams](https://kidbee85-beep.github.io/mutable-dreams/) — generative instruments and a Web Studio for the M-VAVE FM-1.
- [Performance Garden](https://github.com/kidbee85-beep/mutable-dreams-performance-garden) — the Mutable Dreams instrument for VIBE FM.
- [bazillOS](https://kidbee85-beep.github.io/bazillOS/) — independent firmware for the Polyend Tracker Mini.

[Optional donations](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&currency_code=CHF) support firmware updates, new firmware and synthesizer software.

## Request another port

Want TrackWerk on another handheld or operating system? Please [open an issue](https://github.com/kidbee85-beep/TrackWerk/issues/new) with the exact device model, firmware or OS version, processor, and any controls, MIDI or audio hardware you can help test. Community testers and contributors are welcome. Optional donations support continued development; reports and music made with TrackWerk help too.
