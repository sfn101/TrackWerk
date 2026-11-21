# Anbernic R36 Ultra — dArkOSRE

TrackWerk 1.2.0 runs on the Anbernic R36 Ultra with [dArkOSRE-R36](https://github.com/southoz/dArkOSRE-R36).
No application source change is needed: the Debian 13 arm64 build is the right binary. What this image
needs is the frontend's video environment, a controller database, and a preset copy that survives an
exFAT ROMs card.

| | |
| --- | --- |
| Device | Anbernic R36 Ultra — RK3326, 4× Cortex-A35, 896 MB RAM, 720×720 DSI |
| Image | dArkOSRE-R36 — Debian 13 (trixie) aarch64, kernel 4.4.189, glibc 2.41, SDL2 2.32.10 |
| Frontend | EmulationStation-fcamod (`emulationstation.service`), PortMaster in `/opt/system/Tools/PortMaster` |
| Package | `trackwerk_1.2.0-1_arm64.deb` |
| Display | 720×720 fullscreen, 4:3 content integer-scaled to 720×540 at y=90 |
| Audio | ALSA `default`, 48 kHz stereo, 512-frame callbacks, `late 0` |
| Runtime | ~3.5–4.5 % CPU at 720×720, RSS ~78 MB |

## Install

### Option A — the package and the Ports launcher in this tree

`dpkg` is invoked with an explicit `PATH`: on this image `sudo` starts it without `/usr/sbin`, and it
then aborts with *"ldconfig/start-stop-daemon not found"*.

```sh
sudo env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    dpkg -i trackwerk_1.2.0-1_arm64.deb

cp TrackWerk.sh /roms/ports/TrackWerk.sh
chmod 755 /roms/ports/TrackWerk.sh
```

**TrackWerk** then appears in the Ports list, with data and log in `/roms/ports/trackwerk/`:

```
/roms/ports/trackwerk/log.txt              startup and session log (rotated at 1 MB)
/roms/ports/trackwerk/data/                TRACKWERK_HOME
/roms/ports/trackwerk/data/Presets/        2,432 factory presets, copied on first launch
/roms/ports/trackwerk/data/Projects/       songs
/roms/ports/trackwerk/data/Samples/        samples, reachable over USB
```

### Option B — PortMaster

The PortMaster port keeps its data in `/roms/ports/pockettracker/data`. It needed one fix, included in
this tree: the video environment below. PortMaster's `get_controls` already exports a controller
database that maps this pad correctly, so the port needs nothing else on this image.

## What the launcher has to supply

### Video

On KMSDRM the window cannot be created without the vendor EGL blob, and the failure happens before the
first frame:

```
startup: video SDL_CreateWindow begin; size=640x480 flags=0x00000024
SDL_CreateWindow failed: Can't load EGL/GL library on window creation.
```

```sh
export SDL_VIDEO_EGL_DRIVER=libEGL.so
```

`SDL_VIDEO_EGL_DRIVER=libEGL.so` is the same value `emulationstation.service` exports. ArkOS-family
images export it from the service that starts the frontend, so a port launched by EmulationStation does
not inherit it — hence the `CFW_NAME` guard in the PortMaster launcher. The Ports launcher also exports
`POCKETTRACKER_FULLSCREEN=1`; on KMSDRM the output is 720×720 with or without it, so the PortMaster
launcher does not need it. With the EGL variable set, the log reads:

```
layout:  FULL - no on-screen buttons  (touchCapable=0 physicalPad=1 output=720x720 landscape)
video:   scaling INTEGER -> FIT   frame=720x540 at 0,90
```

EmulationStation does **not** have to be stopped: it releases the DRM/EGL screen while a port runs, as
every other port on this image relies on. Stopping `emulationstation.service` from inside a port would
kill the port with its own cgroup.

### Controllers

SDL's built-in mapping for this pad has **no `start`, no `back`** and swaps A/B:

```
1900bb3e4b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,x:b2,y:b3,...   (no start, no back)
```

Both databases on the image describe the device correctly —
`/opt/inttools/gamecontrollerdb.txt` (frontend) and `/opt/system/Tools/PortMaster/gamecontrollerdb.txt`
— under the SDL-normalised key `190000004b4800000011000000010000`:

```
190000004b4800000011000000010000,GO-Super Gamepad,a:b1,b:b0,x:b2,y:b3,\
back:b12,start:b13,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,\
dpup:b8,dpdown:b9,dpleft:b10,dpright:b11,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux
```

Verified on hardware with `POCKETTRACKER_INPUT_TRACE=1`, one line per physical press:

| Physical | SDL event | TrackWerk button |
| --- | --- | --- |
| D-pad up / down / left / right | `dpup` / `dpdown` / `dpleft` / `dpright` | `DPAD_UP` / `DPAD_DOWN` / `DPAD_LEFT` / `DPAD_RIGHT` |
| A | `a` | `A` |
| B | `b` | `B` |
| X | `x` | `A` (alias by design) |
| Y | `y` | `B` (alias by design) |
| L1 / R1 | `leftshoulder` / `rightshoulder` | `L` / `R` |
| SELECT | `back` | `SELECT` |
| START | `start` | `START` |

The analogue sticks emit `PAD AXIS` and stay unmapped: no action listens to an axis.

### Audio

`~/.asoundrc` on this image routes `default` through `plug > dmix > hw`. dArkOSRE's `perfmax` removes
that file before a port starts; TrackWerk then opens `empty > plug > hw` at the same 48 kHz stereo with
512-frame callbacks and reports `late 0`, so no launcher restores it.

## Data on the ROMs card

`/roms` is exFAT. The package launcher publishes factory presets with
`renameat2(..., RENAME_NOREPLACE)`, falling back to `link(2)`; exFAT returns `EINVAL` for the first and
does not implement the second, so nothing was ever published and every launch re-copied the library
into a temporary file that was then discarded. Two things fix it:

- the Ports launcher copies `Presets` with `cp -rn` before starting the app, after which the package's
  own pass skips every file that already exists;
- `publish_new()` in `release-tools/1.2/linux/packaging/trackwerk` falls back to an exclusive create
  (`O_CREAT|O_EXCL`) and a copy, so a stock `trackwerk` invocation also works there.

## Operating notes

- Quit with **PROJECT > EXIT**. There is no F10 key on the device; the launcher exits with the app's
  status and EmulationStation takes the screen back.
- Keep `/opt/inttools/gamecontrollerdb.txt` and PortMaster's database in place. Without one of them,
  START and SELECT do nothing and A/B read inverted.

## Not tested on this device

MIDI in/out, USB audio interfaces, the microphone/line capture path, the online MP3 source, and the
iPhone helpers. Only the tracker, synthesis, sampling from the internal file browser and playback were
exercised.
