# AGENTS.md — TrackWerk fork notes (R36 Ultra / external audio)

This fork (`ozkaoz/TrackWerk`) carries two audio features developed against an Anbernic R36 Ultra
running dArkOSRE. Both are meant to be verified on the console before any PR to
`kidbee85-beep/TrackWerk`. Nothing here replaces the upstream docs; it records what was tried, what
works, and what the hardware will not do without an OS-side change.

Target device: Anbernic R36 Ultra, RK3326, aarch64, Debian 13 (trixie), kernel 4.4.189, 896 MB RAM,
720×720 screen, SDL2 2.32.10, exFAT `/roms` card.
The device was reached over SSH on the local network during development (address and login are not recorded here).

---

## Track B — global AUDIO DRIVERS overlay (working, app-side only)

**Goal.** From any screen, `L1 + R1` opens a modal that lists the outputs `SETTINGS > AUDIO OUT`
already switches between (built-in jack/speaker, USB DAC, USB interface, SP404MKII, …) and switches
the live output without leaving the screen.

**How it is wired.**

| Piece | File |
| --- | --- |
| State + drawing | `source/native/ui/modules/audio_drivers.h` / `.cpp` |
| Input layer | `source/native/ui/dispatch/layers/audio_drivers.cpp` |
| Chord + list sync | `source/shell/app.cpp` (`poll_audio_drivers_chord`, `sync_audio_output`) |
| Layer stack / backdrop | `source/native/ui/dispatch/route.cpp`, `ui/app_state.h` |

- The overlay never opens a device itself. `A` only writes `settings.audioOutIndex`; the frame loop's
  `sync_audio_output` performs the switch through `AudioOutputSelector`, exactly like the SETTINGS
  row. This keeps one switch path, and success or failure is reported in the overlay footer.
- The list is refreshed every frame from `AudioOutputSelector::names()`; the row playing now is
  marked `*` and `NOW`, independent of the cursor.
- The chord needs a one-second hold (`SHOULDER_CHORD_HOLD_MS` in `source/shell/app.cpp`), so a slow
  but deliberate `L + R` (leave selection, clear the clip buffer) is never taken for it. A plain `L + R` still fires CANCEL on release. Any third press
  during the hold (`L+R+A`, `L+R+START`, …) cancels the opening, so no existing combo is stolen.
- The overlay is ignored over the splash, attract mode and show mode, and it does not open under an
  existing modal layer. It is hidden when `caps.audioOutputs` is false (no switchable output).
- `L_R` is consumed by the layer while the chord is owned, so the release cannot also run CANCEL.

**Test on the console.** Launch TrackWerk from Ports, hold `L1 + R1` on any screen, check the list,
pick a USB output with `A`, confirm the footer says `PLAYING: <name>` and that audio really moves
(headphones / DAC). Then pick the built-in output again. Repeat inside Studio (RECORD tab) and with
a project playing.

**Verified on the R36 Ultra.** A short `L + R` still fires CANCEL; holding both shoulders for ~500 ms (the hold time at that
test; it is one second now) opens the overlay and logs `audio: AUDIO DRIVERS overlay (L1+R1)`. `UP/DOWN` move, `A` writes the
selection and the shell switches the output, `B` closes, and a second hold reopens it.

---

## Track A — external input capture and monitoring

The user-facing goal: with an instrument `Type = External`, record what arrives on USB OTG (PC,
phone, SP404MKII) and hear it while recording.

### A1 — app-side capture + monitoring (implemented; pending a real USB input)

- `Type = External` keeps working as MIDI; its `RECORD >` row opens Studio's RECORD page and the
  finished take is imported as `SAMPLER`, as before.
- Studio RECORD page (`studioTab == 4`) has two extra rows:
  - `A  MONITOR: ON/OFF` — mixes the captured input into the already-open output.
  - `A  REFRESH MIC / USB INPUTS` — rescans ALSA inputs.
- Monitoring is a lock-free SPSC ring in `SdlSampleRecorder::Monitor` (4096 stereo float frames):
  the capture worker is the only producer, the output callback the only consumer. The callback
  drains it in `SdlAudioEngine::set_monitor_audio`, mixed after the existing external-audio hook, so
  the DSP, the sequencer and the source preview are untouched.
- Capture opens ALSA with `samples = 512` (≈10.7 ms at 48 kHz) instead of 1024 to keep monitoring
  latency down. Linear interpolation resamples when the input rate differs from the output rate
  (e.g. 44.1 kHz interface into a 48 kHz stream).
- A backlog older than half the ring is dropped, so monitoring stays in the present instead of
  replaying a delayed chunk. The ring is flushed when monitoring is enabled and when a take ends.
  Recording stop, closing the page and reopening it all force monitoring off.
- Monitoring is unavailable for the internal-master source (it would feed back) and for the iPhone
  helper backend (it owns its own device).

**Test on the console.**

1. Connect a USB audio interface / SP404MKII / phone (USB-C OTG, host mode) with something playing.
2. `INSTRUMENT > Type = External`, open `RECORD >`, select the USB input, `A` on
   `MONITOR: ON`.
3. Confirm the meters move, the input is audible with low latency, `A` on `START RECORDING` records,
   and the take appears in `USE TAKE > EDIT & SLICE` as a `SAMPLER`.

The internal RK817 mic is also a valid input (`Main Mic` must be selected in ALSA; it ships as
`MIC OFF` and records silence until then). It is useful to check the capture path without a USB
interface, but it does not exercise OTG.

**Verified on the R36 Ultra (internal RK817 mic).** Studio RECORD captured 2-channel PCM16 at 48 kHz
with `AUDIODEV=hw:0,0`, `MONITOR: ON` logged `capture: monitor on`, the take was imported into
`SAMPLE EDITOR` (`Recordings/…wav` and `Samples/Recordings/…wav` identical), and the app held
~5 % CPU with `late 0` (no missed audio deadlines). A standalone probe measured the monitor ring at
RMS 0.012 / peak 0.021 while recording 145 408 frames in ~3 s. A real USB interface / SP404MKII was
not available, so OTG capture and perceived latency are still to be confirmed.

### A2 — PC/phone → console as a USB sound card (blocked, OS-side)

The R36 Ultra USB-C port is wired as a **host** (`dr_mode = host` in the DT node `usb@ff300000`).
For the console to appear as an audio interface to a PC or phone, the OS must:

1. Put the controller in peripheral/OTG mode (DTB `dr_mode = "otg"` and a role switch, or a
   peripheral-only build), and
2. expose a UAC1/UAC2 gadget.

The stock kernel does **not** provide this:

- `snd-usb-audio` is built in, so USB audio interfaces work as a host input/output.
- Only `u_ether` and the ECM/EEM/NCM/RNDIS functions are present. There is no `u_audio`,
  `usb_f_uac1` or `usb_f_uac2`.
- `libcomposite.ko` exists, but `modprobe libcomposite` returns `Operation not permitted`;
  `/sys/class/udc` is empty.
- `/lib/modules/4.4.189/build` points to a path that does not exist on the device, so out-of-tree
  modules cannot be built there.

**Do not promise “PC → console by USB-C” as an app feature.** It needs a kernel/DTB change (build
`u_audio` + `usb_f_uac1/uac2`, switch the port role), which is a separate OS-side project and is
out of scope for this branch. The app can only capture from USB devices the console hosts.

---

## Build (cross, aarch64) and deploy

Cross-compiled from WSL Ubuntu 24.04 because the console has `gcc` but no `g++`.

```bash
# SDL2 link SDK copied from the console (headers + libSDL2-2.0.so.0.3200.10)
#   ~/r36-sdk/sdl2/include, ~/r36-sdk/sdl2/lib, ~/r36-sdk/sdl2/cmake/SDL2Config.cmake
# Toolchain: ~/pr/toolchain/aarch64-r36.cmake

cmake -S source/shell -B ~/pr/build-r36 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=~/pr/toolchain/aarch64-r36.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DPT_HANDHELD=ON \
  -DSDL2_DIR=~/r36-sdk/sdl2/cmake \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined"
cmake --build ~/pr/build-r36 -j"$(nproc)"
```

`--allow-shlib-undefined` is for the link step only: the device's `libSDL2` needs
`libsamplerate.so.0`, which the console has and the cross sysroot does not. It does not change the
binary's runtime dependencies.

Deploy for a hardware test:

```bash
scp ~/pr/build-r36/pockettracker-sdl <user>@<device-address>:/tmp/trackwerk.bin
ssh <user>@<device-address> \
  'sudo install -m 755 /tmp/trackwerk.bin /usr/lib/trackwerk/trackwerk.bin'
```

Launch from the Ports list (`/roms/ports/TrackWerk.sh`). The launcher exports
`SDL_VIDEODRIVER=kmsdrm`, `SDL_VIDEO_EGL_DRIVER=libEGL.so`, `SDL_AUDIODRIVER=alsa`,
`POCKETTRACKER_FULLSCREEN=1`, and keeps user data in `/roms/ports/trackwerk/data`
(`TRACKWERK_HOME`). It also copies the factory presets once, because exFAT has no
`renameat2(RENAME_NOREPLACE)` and no hardlinks.

Never stop `emulationstation.service` from the port: ES releases DRM/EGL for the port, and stopping
the unit would kill the child process.

## Verification checklist before a PR

- [x] Cross build succeeds with the command above (no warnings added in the new files).
- [x] `L1 + R1` opens AUDIO DRIVERS (verified on SONG and STUDIO; the layer is global).
- [x] `UP/DOWN` moves, `A` switches, `B` closes, a plain `L + R` still cancels.
- [ ] Switching to USB output and back works with a song playing, no dropouts.
- [ ] External input: meters move, `MONITOR: ON` is audible with acceptable latency, take imports
      as `SAMPLER`. (Capture and import verified with the internal mic; a USB input is pending.)
- [x] Monitoring is off after stop / after leaving RECORD; no feedback loop with the internal source.
- [x] No regression in `SETTINGS > AUDIO OUT` (same list, same selection).
- [x] Document A2's OS-side requirement in the PR description; do not claim PC→console audio.
