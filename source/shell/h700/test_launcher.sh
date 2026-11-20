#!/bin/bash
# GPL-3.0-or-later. Launcher test for the Anbernic H700 package: the video-mode fallback, with a fake
# "native binary" in place of trackwerk.aarch64. Needs only bash. Usage: test_launcher.sh <new empty dir>
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:?usage: test_launcher.sh <new empty directory>}"
mkdir -p "$ROOT" || exit 2
[ -z "$(ls -A "$ROOT")" ] || { echo "directory must be empty: $ROOT"; exit 2; }
FAILS=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected [$3], got [$2]"; FAILS=$((FAILS+1)); fi; }

# A card layout: <case>/TrackWerk.sh and <case>/trackwerk/trackwerk.aarch64 (the fake).
new_case() {
    local dir="$ROOT/$1"
    mkdir -p "$dir/trackwerk/tools" && cp "$HERE/TrackWerk.sh" "$dir/TrackWerk.sh"
    cat > "$dir/trackwerk/trackwerk.aarch64" <<'FAKE'
#!/bin/bash
# FAKE_BEHAVIOUR is "<mode>:<what>,<mode>:<what>": hang (no frame), crash3 (exit 3 before a frame),
# ok<N> (present a frame, run briefly, exit N).
mode="${POCKETTRACKER_VIDEO:-?}"
what=""
for pair in ${FAKE_BEHAVIOUR//,/ }; do [ "${pair%%:*}" = "$mode" ] && what="${pair#*:}"; done
echo "fake: mode=$mode behaviour=$what" >> "$FAKE_TRACE"
case "$what" in
    hang)  exec sleep 30;;
    crash3) exit 3;;
    ok*)   printf 'frame submitted' > "$PTE_STARTUP_READY_FILE"; sleep 0.3; exit "${what#ok}";;
    *)     exit 99;;
esac
FAKE
    chmod +x "$dir/trackwerk/trackwerk.aarch64" "$dir/TrackWerk.sh"
    echo "$dir"
}
run() {  # run <dir> <behaviour> [extra env assignment]
    local dir="$1"; shift
    FAKE_TRACE="$dir/trace.txt" FAKE_BEHAVIOUR="$1" PTE_STARTUP_TIMEOUT_SECONDS=1 env ${2:+"$2"} bash "$dir/TrackWerk.sh" >/dev/null 2>&1
    echo $?
}
trace() { tr '\n' ' ' < "$1/trace.txt" | sed 's/fake: //g; s/ $//'; }
mode_file() { [ -f "$1/trackwerk/video-mode.txt" ] && tr -d '\n' < "$1/trackwerk/video-mode.txt" || echo none; }

d=$(new_case renderer-ok);        rc=$(run "$d" "renderer:ok5,surface:hang")
check "renderer works: exit status of the app is passed on" "$rc" 5
check "renderer works: only renderer started" "$(trace "$d")" "mode=renderer behaviour=ok5"
check "renderer works: remembered" "$(mode_file "$d")" renderer

d=$(new_case fallback);           rc=$(run "$d" "renderer:hang,surface:ok0")
check "renderer hangs: falls back to surface, exit 0" "$rc" 0
check "renderer hangs: both tried in order" "$(trace "$d")" "mode=renderer behaviour=hang mode=surface behaviour=ok0"
check "renderer hangs: surface remembered" "$(mode_file "$d")" surface
rc=$(run "$d" "renderer:hang,surface:ok7"); : > /dev/null
check "second start: remembered surface first, no timeout, status passed on" "$rc" 7
check "second start: only surface started" "$(tail -n 1 "$d/trace.txt")" "fake: mode=surface behaviour=ok7"

d=$(new_case crash);              rc=$(run "$d" "renderer:crash3,surface:ok0")
check "renderer crashes before a frame: surface is tried" "$rc" 0
check "renderer crashes: both tried" "$(trace "$d")" "mode=renderer behaviour=crash3 mode=surface behaviour=ok0"

d=$(new_case both-fail);          rc=$(run "$d" "renderer:hang,surface:hang")
check "no mode works: exit 124, nothing remembered" "$rc" 124
check "no mode works: both tried once" "$(trace "$d")" "mode=renderer behaviour=hang mode=surface behaviour=hang"
check "no mode works: no mode file" "$(mode_file "$d")" none

d=$(new_case forced);             rc=$(run "$d" "renderer:hang,surface:ok0" "POCKETTRACKER_VIDEO=renderer")
check "forced mode is the only one tried" "$(trace "$d")" "mode=renderer behaviour=hang"
check "forced mode failing: exit 124" "$rc" 124
check "forced mode: nothing remembered" "$(mode_file "$d")" none

d=$(new_case forced-ok);          rc=$(run "$d" "renderer:ok0,surface:ok0" "POCKETTRACKER_VIDEO=surface")
check "forced surface starts surface only" "$(trace "$d")" "mode=surface behaviour=ok0"
check "forced mode that works is not stored either" "$(mode_file "$d")" none

d=$(new_case early-exit-0);       rc=$(run "$d" "renderer:ok0,surface:ok0")
check "clean exit status 0" "$rc" 0
echo
[ "$FAILS" -eq 0 ] && { echo "H700_LAUNCHER_PASS"; exit 0; }
echo "H700_LAUNCHER_FAIL: $FAILS"; exit 1
