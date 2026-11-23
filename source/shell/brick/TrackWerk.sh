#!/bin/bash
# GPL-3.0-or-later. Standalone native TrimUI Brick / KNULLI launcher.
# KNULLI supplies SDL controller mappings; this script never sources PortMaster.

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
printf '\n=== TrackWerk 1.3.0-Brick: shell entered ===\n'
# Persist before any command substitution or native graphics initialization.
# A forced power-off must not erase the only evidence of where startup stopped.
sync || printf '%s\n' 'WARNING: initial startup log sync failed'
cd -- "$PTE_APP_DIR" || exit 2
GAMEDIR="$PWD"
PTE_LOG="$GAMEDIR/log.txt"
pte_phase() { printf '[startup +%ss] %s\n' "$SECONDS" "$*"; }
export POCKETTRACKER_SYNC_STARTUP=1
export POCKETTRACKER_VIDEO="${POCKETTRACKER_VIDEO:-surface}"
pte_phase "Video mode: $POCKETTRACKER_VIDEO; durable startup logging enabled"
pte_phase "Shell flags: $-"

PTE_CHILD_PID=""
PTE_CHILD_STATUS=""
PTE_PRESET_PID=""
PTE_TEMPS=()
PTE_READY_FILE=""
pte_job_running() {
    local pte_pid="$1"
    [ -n "$pte_pid" ] || return 1
    # Only the shell's own, still-running native job qualifies. A completed and
    # reaped PID must never cause a signal to a subsequently unrelated process.
    case $'\n'"$(jobs -pr)"$'\n' in
        *$'\n'"$pte_pid"$'\n'*) return 0;;
    esac
    return 1
}
pte_child_running() { pte_job_running "$PTE_CHILD_PID"; }
pte_stop_child() {
    [ -n "$PTE_CHILD_PID" ] || return 0
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
    if wait "$PTE_CHILD_PID"; then PTE_CHILD_STATUS=0; else PTE_CHILD_STATUS=$?; fi
    pte_phase "Native process stopped with status $PTE_CHILD_STATUS"
    PTE_CHILD_PID=""
}
# The preparer is a single Python process, never a shell with detached helpers.
# Closing TrackWerk stops only this launcher's own job. Never wait indefinitely
# for optional library I/O: a blocked card must not keep the frontend waiting.
pte_stop_presets() {
    [ -n "$PTE_PRESET_PID" ] || return 0
    local pte_deadline=$((SECONDS + 2))
    if pte_job_running "$PTE_PRESET_PID"; then
        pte_phase "Stopping background preset preparation; missing files resume next launch"
        kill -TERM "$PTE_PRESET_PID" 2>/dev/null || true
        while pte_job_running "$PTE_PRESET_PID" && [ "$SECONDS" -lt "$pte_deadline" ]; do sleep 0.1; done
        if pte_job_running "$PTE_PRESET_PID"; then
            kill -KILL "$PTE_PRESET_PID" 2>/dev/null || true
            pte_deadline=$((SECONDS + 1))
            while pte_job_running "$PTE_PRESET_PID" && [ "$SECONDS" -lt "$pte_deadline" ]; do sleep 0.1; done
        fi
    fi
    if pte_job_running "$PTE_PRESET_PID"; then
        pte_phase "Preset I/O remains blocked after cancellation; returning to frontend"
        disown "$PTE_PRESET_PID" 2>/dev/null || true
    else
        local pte_status
        if wait "$PTE_PRESET_PID"; then pte_status=0; else pte_status=$?; fi
        pte_phase "Background preset preparation exited with status $pte_status"
    fi
    PTE_PRESET_PID=""
}

pte_start_presets() {
    [ -d "$GAMEDIR/factory/Presets" ] || return 0
    local pte_pack="" pte_seeded=""
    if [ -f "$GAMEDIR/factory/presets-pack.id" ]; then IFS= read -r -n 65 pte_pack < "$GAMEDIR/factory/presets-pack.id" || true; fi
    if [ -f "$POCKETTRACKER_HOME/.factory-presets-pack.id" ]; then IFS= read -r -n 65 pte_seeded < "$POCKETTRACKER_HOME/.factory-presets-pack.id" || true; fi
    if [[ "$pte_pack" =~ ^[a-fA-F0-9]{64}$ ]] && [ "$pte_pack" = "$pte_seeded" ] && [ -d "$POCKETTRACKER_HOME/Presets" ]; then
        pte_phase "Factory presets already prepared (matching pack identifier)"
        return 0
    fi
    if [ ! -f "$GAMEDIR/tools/prepare_presets.py" ] || ! command -v python3 >/dev/null 2>&1; then
        pte_phase "WARNING: factory library preparer or system python3 is missing; app remains usable"
        return 0
    fi
    # KNULLI provides Python 3. Verification/copying has no place in the path to
    # the first frame. A matching completed marker also preserves intentional
    # pruning and rearrangement of the user's existing preset library.
    pte_phase "Preparing factory library in background after first frame"
    python3 -I -u "$GAMEDIR/tools/prepare_presets.py" --factory "$GAMEDIR/factory/Presets" --data "$POCKETTRACKER_HOME" --background --library &
    PTE_PRESET_PID=$!
}

pte_cleanup() {
    local pte_rc=$?
    set +e
    trap - EXIT USR1
    trap '' HUP INT TERM
    pte_stop_child
    pte_stop_presets
    local pte_temp
    for pte_temp in "${PTE_TEMPS[@]}"; do rm -f -- "$pte_temp"; done
    if [ -n "$PTE_READY_FILE" ]; then rm -f -- "$PTE_READY_FILE"; fi
    printf '=== App beendet, Status %s ===\n' "$pte_rc" || true
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
    printf '\n=== TrackWerk 1.3.0-Brick: shell entered (log rotated) ===\n'
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

# Factory presets are optional library files. The native synths can start
# immediately; the verified library worker is launched only after a first frame.
pte_phase "Factory preset preparation deferred until the app is visible"

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
pte_phase "Launching native app; first-frame deadline $PTE_STARTUP_TIMEOUT seconds"
sync || true
printf 'Start: %s\nDaten: %s\nAudio-Puffer: %s Frames\n' "$PTE_BINARY" "$POCKETTRACKER_HOME" "$POCKETTRACKER_AUDIO_FRAMES"
"$PTE_BINARY" "$@" &
PTE_CHILD_PID=$!
# SECONDS has one-second resolution; round outward so a just-started native
# process always receives the full configured interval before the first frame.
PTE_STARTUP_DEADLINE=$((SECONDS + PTE_STARTUP_TIMEOUT + 1))
PTE_FIRST_FRAME=0
# The parent watches its own job until the first presented frame, then uses a
# normal wait. There is no detached watchdog that could signal a reused PID.
while pte_child_running; do
    if [ -f "$PTE_READY_FILE" ] && [ ! -L "$PTE_READY_FILE" ]; then
        PTE_READY_STATE=""
        IFS= read -r -n 32 PTE_READY_STATE < "$PTE_READY_FILE" || true
        if [ "$PTE_READY_STATE" = "frame submitted" ]; then
            pte_phase "Native first frame submitted; startup watchdog complete"
            PTE_FIRST_FRAME=1
            break
        fi
    fi
    if [ "$SECONDS" -ge "$PTE_STARTUP_DEADLINE" ]; then
        pte_phase "STARTUP TIMEOUT: no first presented frame; returning to frontend"
        pte_stop_child
        exit 124
    fi
    sleep 0.1
done
if [ "$PTE_FIRST_FRAME" = 1 ] && pte_child_running; then pte_start_presets; fi
if wait "$PTE_CHILD_PID"; then PTE_EXIT_STATUS=0; else PTE_EXIT_STATUS=$?; fi
PTE_CHILD_PID=""
exit "$PTE_EXIT_STATUS"
