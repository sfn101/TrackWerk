#!/bin/bash
# GPL-3.0-or-later. Standalone native Anbernic H700 / KNULLI launcher (community test; the RG40XX V is
# the device it was written for, the other H700 devices run the same KNULLI image).
# KNULLI supplies SDL controller mappings; this script never sources PortMaster.
#
# The binary is the same aarch64 build as the TrimUI Brick's (Cortex-A53, glibc 2.31 or newer, the
# system SDL2). What differs is the display: KNULLI's H700 image (Mali-G31, fbdev, a patched SDL2 with
# its own video driver) is not the Brick's PowerVR image, and nobody has run TrackWerk on it yet. So
# the first start tries the GPU renderer and, if no first frame appears, tries the window-surface mode
# once; the mode that worked is remembered. POCKETTRACKER_VIDEO=renderer|surface forces one mode.

# Parameter expansion, cd, pwd and printf below are shell builtins. The first
# persistent marker must precede date, helper programs, preset work and native SDL.
PTE_SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
[ "$PTE_SCRIPT_DIR" != "${BASH_SOURCE[0]}" ] || PTE_SCRIPT_DIR=.
PTE_APP_DIR="$PTE_SCRIPT_DIR/trackwerk"
if [ ! -d "$PTE_APP_DIR" ]; then
    printf '%s\n' "TrackWerk: App-Ordner fehlt: $PTE_APP_DIR" >&2
    exit 2
fi
PTE_LOG="$PTE_APP_DIR/log.txt"
exec >> "$PTE_LOG" 2>&1 || exit 2
printf '\n=== TrackWerk 1.3.0-H700: shell entered ===\n'
# Persist before any command substitution or native graphics initialization.
# A forced power-off must not erase the only evidence of where startup stopped.
sync || printf '%s\n' 'WARNING: initial startup log sync failed'
cd -- "$PTE_APP_DIR" || exit 2
GAMEDIR="$PWD"
PTE_LOG="$GAMEDIR/log.txt"
pte_phase() { printf '[startup +%ss] %s\n' "$SECONDS" "$*"; }
export POCKETTRACKER_SYNC_STARTUP=1
# Video modes to try, in order. An explicit POCKETTRACKER_VIDEO is the only one tried; otherwise the
# mode that worked last time (video-mode.txt) goes first, then the other one.
PTE_MODE_FILE="$GAMEDIR/video-mode.txt"
PTE_MODES=()
if [ -n "${POCKETTRACKER_VIDEO:-}" ]; then
    PTE_FORCED_MODE="$POCKETTRACKER_VIDEO"
    PTE_MODES=("$POCKETTRACKER_VIDEO")
else
    PTE_LAST_MODE=""
    if [ -f "$PTE_MODE_FILE" ] && [ ! -L "$PTE_MODE_FILE" ]; then
        IFS= read -r -n 16 PTE_LAST_MODE < "$PTE_MODE_FILE" || true
    fi
    case "$PTE_LAST_MODE" in
        surface)  PTE_MODES=(surface renderer);;
        *)        PTE_MODES=(renderer surface);;
    esac
fi
pte_phase "Video modes to try: ${PTE_MODES[*]}; durable startup logging enabled"

# What this device is, for a bug report. Read-only, and every command is optional.
pte_device_summary() {
    local pte_model=""
    for pte_f in /proc/device-tree/model /sys/firmware/devicetree/base/model; do
        if [ -r "$pte_f" ]; then pte_model="$(tr -d '\0' < "$pte_f" 2>/dev/null)"; break; fi
    done
    printf 'device: model=[%s] kernel=%s arch=%s\n' "$pte_model" "$(uname -r 2>/dev/null)" "$(uname -m 2>/dev/null)"
    printf 'device: SDL_VIDEODRIVER=[%s] SDL_AUDIODRIVER=[%s] SDL_GAMECONTROLLERCONFIG_FILE=[%s]\n' \
        "${SDL_VIDEODRIVER-}" "${SDL_AUDIODRIVER-}" "${SDL_GAMECONTROLLERCONFIG_FILE-}"
    if command -v aplay >/dev/null 2>&1; then aplay -l 2>&1 | sed 's/^/device: aplay: /'; fi
    if [ -r /proc/bus/input/devices ]; then
        grep -E '^(N: Name|H: Handlers)' /proc/bus/input/devices 2>/dev/null | sed 's/^/device: input: /'
    fi
    if command -v ldd >/dev/null 2>&1; then ldd --version 2>&1 | sed -n 1p | sed 's/^/device: /'; fi
}
pte_device_summary

PTE_CHILD_PID=""
PTE_CHILD_STATUS=""
PTE_TEMPS=()
PTE_READY_FILE=""
pte_child_running() {
    [ -n "$PTE_CHILD_PID" ] || return 1
    # Only the shell's own, still-running native job qualifies. A completed and
    # reaped PID must never cause a signal to a subsequently unrelated process.
    case $'\n'"$(jobs -pr)"$'\n' in
        *$'\n'"$PTE_CHILD_PID"$'\n'*) return 0;;
    esac
    return 1
}
pte_stop_child() {
    [ -n "$PTE_CHILD_PID" ] || return
    local pte_deadline=$((SECONDS + 8))
    if pte_child_running; then
        pte_phase "Stopping native process $PTE_CHILD_PID"
        kill -TERM "$PTE_CHILD_PID" 2>/dev/null || true
        while pte_child_running; do
            if [ "$SECONDS" -ge "$pte_deadline" ]; then
                pte_phase "Native process did not stop within eight seconds; ending own child"
                kill -KILL "$PTE_CHILD_PID" 2>/dev/null || true
                break
            fi
            sleep 0.1
        done
    fi
    wait "$PTE_CHILD_PID"
    PTE_CHILD_STATUS=$?
    pte_phase "Native process stopped with status $PTE_CHILD_STATUS"
    PTE_CHILD_PID=""
}
pte_cleanup() {
    local pte_rc=$?
    trap - EXIT HUP INT TERM USR1
    pte_stop_child
    local pte_temp
    for pte_temp in "${PTE_TEMPS[@]}"; do rm -f -- "$pte_temp"; done
    if [ -n "$PTE_READY_FILE" ]; then rm -f -- "$PTE_READY_FILE"; fi
    printf '=== App beendet, Status %s ===\n' "$pte_rc"
    exit "$pte_rc"
}
pte_signal() {
    local pte_rc="$1"
    trap '' HUP INT TERM
    pte_stop_child
    exit "$pte_rc"
}
trap pte_cleanup EXIT
trap 'pte_signal 129' HUP
trap 'pte_signal 130' INT
trap 'pte_signal 143' TERM
pte_fail() { printf 'FEHLER: %s\n' "$*"; exit 2; }

# Rotate only after the first persistent entry has been written. Reopen stdout
# after moving the old file, otherwise subsequent phases would follow its inode.
PTE_LOG_SIZE="$(wc -c < "$PTE_LOG" 2>/dev/null)"
if [[ "$PTE_LOG_SIZE" =~ ^[[:space:]]*[0-9]+[[:space:]]*$ ]] && [ "$PTE_LOG_SIZE" -gt 1048576 ]; then
    mv -f -- "$PTE_LOG" "$GAMEDIR/log.previous.txt" || pte_fail "Startprotokoll kann nicht rotiert werden."
    exec >> "$PTE_LOG" 2>&1 || exit 2
    printf '\n=== TrackWerk 1.3.0-H700: shell entered (log rotated) ===\n'
fi
pte_phase "Standalone start; no PortMaster scripts or network checks"

# Keep the frontend's values, including an explicitly empty or custom value.
# Fallback databases are read only; no keyboard mapper or control helper runs.
if [ -z "${SDL_GAMECONTROLLERCONFIG_FILE+x}" ]; then
    for PTE_DATABASE in /tmp/gamecontrollerdb.txt "${HOME:-/userdata/system}/.config/gamecontrollerdb.txt"; do
        if [ -f "$PTE_DATABASE" ]; then
            export SDL_GAMECONTROLLERCONFIG_FILE="$PTE_DATABASE"
            break
        fi
    done
fi
pte_phase "Inherited SDL controller configuration retained"

export POCKETTRACKER_HOME="$GAMEDIR/data"
export TRACKWERK_HOME="$POCKETTRACKER_HOME"
mkdir -p -- "$POCKETTRACKER_HOME" || pte_fail "Datenordner kann nicht angelegt werden."
pte_phase "Data directory ready"

# The pack identifier is generated from factory paths+contents at packaging.
# The SD installer may preseed the same pack, avoiding 2304 checks on every boot.
PTE_PACK_ID=""; PTE_SEEDED_ID=""
if [ -f "$GAMEDIR/factory/presets-pack.id" ]; then
    IFS= read -r -n 65 PTE_PACK_ID < "$GAMEDIR/factory/presets-pack.id" || true
fi
if [ -f "$POCKETTRACKER_HOME/.factory-presets-pack.id" ]; then
    IFS= read -r -n 65 PTE_SEEDED_ID < "$POCKETTRACKER_HOME/.factory-presets-pack.id" || true
fi
if [[ "$PTE_PACK_ID" =~ ^[a-fA-F0-9]{64}$ ]] && [ "$PTE_PACK_ID" = "$PTE_SEEDED_ID" ] && [ -d "$POCKETTRACKER_HOME/Presets" ]; then
    pte_phase "Factory presets already prepared (matching pack identifier)"
elif [ -d "$GAMEDIR/factory/Presets" ]; then
    pte_phase "Preparing missing factory presets"
    PTE_DEST="$POCKETTRACKER_HOME/Presets"
    [ ! -L "$PTE_DEST" ] || pte_fail "Preset-Ordner ist eine Verknuepfung; automatische Einrichtung abgebrochen."
    mkdir -p -- "$PTE_DEST" || pte_fail "Preset-Ordner kann nicht angelegt werden."
    PTE_LIST="$(mktemp "$POCKETTRACKER_HOME/.pte-preset-list.XXXXXX")" || pte_fail "Preset-Dateiliste kann nicht angelegt werden."
    PTE_TEMPS+=("$PTE_LIST")
    find "$GAMEDIR/factory/Presets" -type d -name '.*' -prune -o -type f ! -name '.*' -print0 > "$PTE_LIST" || pte_fail "Factory-Presets koennen nicht gelesen werden."
    PTE_SEED_COUNT=0
    while IFS= read -r -d '' PTE_SOURCE; do
        PTE_RELATIVE="${PTE_SOURCE#"$GAMEDIR/factory/Presets/"}"
        PTE_TARGET="$PTE_DEST/$PTE_RELATIVE"
        # Edited presets and existing symbolic links are never replaced.
        if [ -e "$PTE_TARGET" ] || [ -L "$PTE_TARGET" ]; then continue; fi
        PTE_PARENT="$PTE_DEST"
        PTE_REST="$PTE_RELATIVE"
        while [[ "$PTE_REST" == */* ]]; do
            PTE_COMPONENT="${PTE_REST%%/*}"; PTE_REST="${PTE_REST#*/}"
            PTE_PARENT="$PTE_PARENT/$PTE_COMPONENT"
            [ ! -L "$PTE_PARENT" ] || pte_fail "Preset-Unterordner ist eine Verknuepfung."
            mkdir -p -- "$PTE_PARENT" || pte_fail "Preset-Unterordner kann nicht angelegt werden."
        done
        PTE_TEMP="$(mktemp "$PTE_PARENT/.pte-preset-copy.XXXXXX")" || pte_fail "Neue Preset-Datei kann nicht vorbereitet werden."
        PTE_TEMPS+=("$PTE_TEMP")
        if ! cp -- "$PTE_SOURCE" "$PTE_TEMP" || ! cmp -s -- "$PTE_SOURCE" "$PTE_TEMP"; then
            pte_fail "Presetpack kann nicht eingerichtet werden. Speicherplatz und SD-Karte pruefen."
        fi
        # -n preserves any user file created while the verified copy was made.
        mv -n -- "$PTE_TEMP" "$PTE_TARGET" || pte_fail "Neue Preset-Datei kann nicht gespeichert werden."
        if [ -e "$PTE_TEMP" ]; then rm -f -- "$PTE_TEMP"; fi
        PTE_TEMPS=("$PTE_LIST")
        PTE_SEED_COUNT=$((PTE_SEED_COUNT + 1))
        if [ $((PTE_SEED_COUNT % 256)) -eq 0 ]; then pte_phase "Factory presets prepared: $PTE_SEED_COUNT"; fi
    done < "$PTE_LIST"
    rm -f -- "$PTE_LIST"
    PTE_TEMPS=()
    if [[ "$PTE_PACK_ID" =~ ^[a-fA-F0-9]{64}$ ]]; then
        PTE_MARKER_TEMP="$(mktemp "$POCKETTRACKER_HOME/.pte-preset-marker.XXXXXX")" || pte_fail "Preset-Markierung kann nicht angelegt werden."
        PTE_TEMPS+=("$PTE_MARKER_TEMP")
        printf '%s\n' "$PTE_PACK_ID" > "$PTE_MARKER_TEMP" || pte_fail "Preset-Markierung kann nicht geschrieben werden."
        mv -f -- "$PTE_MARKER_TEMP" "$POCKETTRACKER_HOME/.factory-presets-pack.id" || pte_fail "Preset-Markierung kann nicht gespeichert werden."
    fi
    pte_phase "Factory preset preparation complete; new files: $PTE_SEED_COUNT"
else
    pte_phase "No factory preset directory present"
fi

export POCKETTRACKER_AUDIO_FRAMES="${POCKETTRACKER_AUDIO_FRAMES:-512}"
export POCKETTRACKER_FULLSCREEN="${POCKETTRACKER_FULLSCREEN:-1}"
export POCKETTRACKER_CACERT="${POCKETTRACKER_CACERT:-$GAMEDIR/tools/cacert.pem}"
export PATH="$GAMEDIR/tools:$PATH"
PTE_BINARY="$GAMEDIR/trackwerk.aarch64"
[ -f "$PTE_BINARY" ] || pte_fail "App-Datei trackwerk.aarch64 fehlt. Das Paket erneut kopieren."
chmod +x -- "$PTE_BINARY" 2>/dev/null || true
[ -x "$PTE_BINARY" ] || pte_fail "App-Datei ist nicht ausfuehrbar. SD-Karten-Zugriff pruefen."
for PTE_TOOL in curl ffmpeg ffprobe yt-dlp qjs source-worker pte-source-worker pte-iphone-usb pte-iphone-pair pte-usbmuxd; do
    if [ -f "$GAMEDIR/tools/$PTE_TOOL" ]; then chmod +x -- "$GAMEDIR/tools/$PTE_TOOL" 2>/dev/null || true; fi
done
if [ ! -f "$POCKETTRACKER_CACERT" ]; then printf '%s\n' "Hinweis: CA-Zertifikatsdatei fehlt; Online-Quellen koennen nicht sicher verbunden werden."; fi

PTE_STARTUP_TIMEOUT="${PTE_STARTUP_TIMEOUT_SECONDS:-25}"
[[ "$PTE_STARTUP_TIMEOUT" =~ ^[0-9]+$ ]] || pte_fail "Ungueltiges Start-Zeitlimit."
[ "${#PTE_STARTUP_TIMEOUT}" -le 3 ] && [ "$PTE_STARTUP_TIMEOUT" -ge 1 ] && [ "$PTE_STARTUP_TIMEOUT" -le 120 ] || pte_fail "Start-Zeitlimit muss 1 bis 120 Sekunden betragen."
PTE_STARTUP_TIMEOUT=$((10#$PTE_STARTUP_TIMEOUT))
PTE_READY_FILE="$GAMEDIR/.pte-startup-ready.$$.$RANDOM.$RANDOM"
[ ! -e "$PTE_READY_FILE" ] && [ ! -L "$PTE_READY_FILE" ] || pte_fail "Start-Markierung ist bereits vorhanden."
export PTE_STARTUP_READY_FILE="$PTE_READY_FILE"
# One start attempt in video mode $1. Returns 0 once a first frame was presented and the app has since
# ended normally or by the user (its status goes to PTE_EXIT_STATUS); 1 when no first frame appeared
# (timeout, or the process ended before one), so the next mode can be tried.
pte_attempt() {
    local pte_mode="$1" pte_result=1
    shift
    export POCKETTRACKER_VIDEO="$pte_mode"
    rm -f -- "$PTE_READY_FILE"
    pte_phase "Launching native app; video mode $pte_mode; first-frame deadline $PTE_STARTUP_TIMEOUT seconds"
    sync || true
    printf 'Start: %s\nDaten: %s\nAudio-Puffer: %s Frames\nVideo: %s\n' "$PTE_BINARY" "$POCKETTRACKER_HOME" "$POCKETTRACKER_AUDIO_FRAMES" "$pte_mode"
    "$PTE_BINARY" "$@" &
    PTE_CHILD_PID=$!
    # SECONDS has one-second resolution; round outward so a just-started native
    # process always receives the full configured interval before the first frame.
    local pte_deadline=$((SECONDS + PTE_STARTUP_TIMEOUT + 1))
    # The parent watches its own job until the first presented frame, then uses a
    # normal wait. There is no detached watchdog that could signal a reused PID.
    while pte_child_running; do
        if [ -f "$PTE_READY_FILE" ] && [ ! -L "$PTE_READY_FILE" ]; then
            local pte_state=""
            IFS= read -r -n 32 pte_state < "$PTE_READY_FILE" || true
            if [ "$pte_state" = "frame submitted" ]; then
                pte_phase "Native first frame submitted in video mode $pte_mode; startup watchdog complete"
                pte_result=0
                break
            fi
        fi
        if [ "$SECONDS" -ge "$pte_deadline" ]; then
            pte_phase "STARTUP TIMEOUT in video mode $pte_mode: no first presented frame"
            pte_stop_child
            PTE_EXIT_STATUS=124
            return 1
        fi
        sleep 0.1
    done
    if [ "$pte_result" -eq 0 ]; then
        pte_remember_mode "$pte_mode"
        wait "$PTE_CHILD_PID"
        PTE_EXIT_STATUS=$?
        PTE_CHILD_PID=""
        return 0
    fi
    # The process ended before presenting a frame (a crash or a refusal to start).
    wait "$PTE_CHILD_PID"
    PTE_EXIT_STATUS=$?
    PTE_CHILD_PID=""
    pte_phase "Native process ended with status $PTE_EXIT_STATUS before the first frame in video mode $pte_mode"
    # A clean exit is not a display failure.
    [ "$PTE_EXIT_STATUS" -eq 0 ] && return 0
    return 1
}

# The mode that worked is stored once, atomically, and only when the user did not force one.
pte_remember_mode() {
    [ -z "${PTE_FORCED_MODE:-}" ] || return 0
    local pte_current=""
    if [ -f "$PTE_MODE_FILE" ] && [ ! -L "$PTE_MODE_FILE" ]; then IFS= read -r -n 16 pte_current < "$PTE_MODE_FILE" || true; fi
    [ "$pte_current" != "$1" ] || return 0
    local pte_temp
    pte_temp="$(mktemp "$GAMEDIR/.pte-video-mode.XXXXXX")" || return 0
    if printf '%s\n' "$1" > "$pte_temp" && mv -f -- "$pte_temp" "$PTE_MODE_FILE"; then
        pte_phase "Video mode $1 remembered for the next start"
    else
        rm -f -- "$pte_temp"
    fi
}

PTE_EXIT_STATUS=124
for PTE_MODE in "${PTE_MODES[@]}"; do
    case "$PTE_MODE" in renderer|surface) ;; *) pte_phase "Unknown video mode '$PTE_MODE'; skipped"; continue;; esac
    if pte_attempt "$PTE_MODE" "$@"; then exit "$PTE_EXIT_STATUS"; fi
done
pte_phase "No video mode produced a first frame; returning to frontend (status $PTE_EXIT_STATUS)"
exit "$PTE_EXIT_STATUS"
