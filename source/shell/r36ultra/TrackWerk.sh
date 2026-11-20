#!/bin/bash
# GPL-3.0-or-later. Standalone Anbernic R36 Ultra launcher for the Debian 13 arm64 package.
#
# Target: dArkOSRE-R36 (https://github.com/southoz/dArkOSRE-R36), Debian 13 trixie, aarch64,
# RK3326, 720x720 DSI panel, EmulationStation-fcamod as the frontend.
#
# dArkOSRE starts ports from EmulationStation's Ports list. ES gives the DRM/EGL screen up for the
# lifetime of the child, so this script never touches emulationstation.service: stopping that unit
# from inside its own cgroup would kill the port together with the frontend. Every other port on
# this image (Bully, GTA San Andreas, ...) relies on the same behaviour.
#
# Distribution: the official trackwerk_1.3.0-1_arm64.deb, which installs
# /usr/lib/trackwerk/trackwerk.bin. Nothing here is copied into the package tree.

PTE_SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
[ "$PTE_SCRIPT_DIR" != "${BASH_SOURCE[0]}" ] || PTE_SCRIPT_DIR=.
PTE_APP_DIR="$PTE_SCRIPT_DIR/trackwerk"
PTE_BIN=/usr/lib/trackwerk/trackwerk.bin

mkdir -p -- "$PTE_APP_DIR" 2>/dev/null
PTE_LOG="$PTE_APP_DIR/log.txt"

# One previous generation is enough for a bug report; the app writes a few kilobytes per run.
PTE_LOG_SIZE="$(wc -c < "$PTE_LOG" 2>/dev/null)"
if [[ "$PTE_LOG_SIZE" =~ ^[[:space:]]*[0-9]+[[:space:]]*$ ]] && [ "$PTE_LOG_SIZE" -gt 1048576 ]; then
    mv -f -- "$PTE_LOG" "$PTE_LOG.previous.txt" 2>/dev/null
fi
exec >> "$PTE_LOG" 2>&1

printf '\n=== TrackWerk 1.3.0 R36 Ultra / dArkOSRE: shell entered ===\n'

if [ ! -x "$PTE_BIN" ]; then
    printf 'TrackWerk: %s is missing.\n' "$PTE_BIN"
    printf 'Install the package first:\n'
    printf '  sudo env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin dpkg -i trackwerk_1.3.0-1_arm64.deb\n'
    exit 2
fi

# User data on the ROMs card, so projects and samples are reachable over USB.
# TRACKWERK_HOME is the package's own override; an empty value would fall back to $HOME.
export TRACKWERK_HOME="$PTE_APP_DIR/data"
mkdir -p -- "$TRACKWERK_HOME" 2>/dev/null

# Factory presets, once. The package's launcher seeds them itself, but its atomic publish needs
# renameat2(RENAME_NOREPLACE) or link(2), and exFAT rejects both with EINVAL. Copying here keeps
# the seeding inside the package a no-op: it skips files that already exist.
PTE_FACTORY=/usr/share/trackwerk/factory/Presets
if [ -d "$PTE_FACTORY" ]; then
    mkdir -p -- "$TRACKWERK_HOME/Presets" 2>/dev/null
    cp -rn -- "$PTE_FACTORY/." "$TRACKWERK_HOME/Presets/" 2>/dev/null
fi

# Sample Pack, once. The package's own launcher copies it too, but this script starts the program directly,
# so it does the same here: only when there is no Samples folder yet, and never over an existing one.
PTE_SAMPLES=/usr/share/trackwerk/samples
if [ -d "$PTE_SAMPLES" ] && [ ! -e "$TRACKWERK_HOME/Samples" ]; then
    printf 'copying the TrackWerk Sample Pack to %s/Samples (first start only)\n' "$TRACKWERK_HOME"
    cp -R -- "$PTE_SAMPLES" "$TRACKWERK_HOME/Samples" 2>/dev/null || printf 'WARNING: sample pack copy incomplete\n'
fi

# Video: KMS/DRM through the Mali EGL blob, exactly what emulationstation.service exports.
# Without SDL_VIDEO_EGL_DRIVER the KMSDRM backend fails with
# "Can't load EGL/GL library on window creation." - the Mesa EGL on this image is not usable
# for the scanout path.
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_EGL_DRIVER=libEGL.so
export SDL_AUDIODRIVER=alsa
export POCKETTRACKER_FULLSCREEN=1
export POCKETTRACKER_AUDIO_FRAMES="${POCKETTRACKER_AUDIO_FRAMES:-512}"

# Controller database. SDL's built-in mapping for this pad ("GO-Super Gamepad", GUID
# 1900bb3e4b4800000011000000010000) has no start/back at all and maps A/B the other way round, so
# START, SELECT and the printed face buttons only line up with a database that describes it.
# dArkOSRE ships a correct one for the frontend; read it, never edit it.
PTE_DB=""
for PTE_CANDIDATE in "$SDL_GAMECONTROLLERCONFIG_FILE" \
                      /opt/inttools/gamecontrollerdb.txt \
                      /opt/system/Tools/PortMaster/gamecontrollerdb.txt \
                      /tmp/gamecontrollerdb.txt; do
    if [ -n "$PTE_CANDIDATE" ] && [ -f "$PTE_CANDIDATE" ]; then PTE_DB="$PTE_CANDIDATE"; break; fi
done
if [ -n "$PTE_DB" ]; then
    export SDL_GAMECONTROLLERCONFIG_FILE="$PTE_DB"
    printf 'controller db: %s\n' "$PTE_DB"
else
    printf 'WARNING: no gamecontrollerdb.txt found; START and SELECT will not work.\n'
fi

printf 'data root: %s\n' "$TRACKWERK_HOME"
printf 'starting %s\n' "$PTE_BIN"
cd -- "$PTE_APP_DIR" 2>/dev/null || cd /tmp

"$PTE_BIN" "$@"
PTE_RC=$?
printf '=== TrackWerk exited: %s ===\n' "$PTE_RC"
exit "$PTE_RC"
