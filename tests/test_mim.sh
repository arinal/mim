# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# The mim binary end to end: exit codes, the 60 Hz pacing, and Ctrl-C.
# Usage: sh test_mim.sh <guest dir> <command that runs mim...>
# Every run is --headless: no window, and SDL is never started.

set -u
dir=$1
shift

fail() {
    echo "test_mim: $*" >&2
    exit 1
}

# quits after 3 ticks: exit 0, and 3 frames at 60 Hz take over 33 ms
start=$(date +%s%N)
out=$("$@" --headless "$dir/hello_rust.wasm" 2>&1) || fail "hello_rust: exit $?: $out"
ms=$((($(date +%s%N) - start) / 1000000))
case $out in
*"app: bye after 3 ticks"*) ;;
*) fail "hello_rust didn't finish: $out" ;;
esac
[ "$ms" -ge 33 ] || fail "3 frames at 60 Hz took only $ms ms"

# traps in app_tick: exit 1
"$@" --headless "$dir/evil_tick_trap.wasm" 2>/dev/null && fail "evil_tick_trap: exit 0"

# never quits by itself; Ctrl-C stops it cleanly. The signal goes after the first tick,
# when mim's handler is in place.
log=$(mktemp)
"$@" --headless "$dir/evil_log_injection.wasm" 2>"$log" &
pid=$!
tries=0
until grep -q 'app: tick' "$log"; do
    tries=$((tries + 1))
    [ "$tries" -lt 400 ] || { kill -KILL "$pid"; fail "evil_log_injection never ticked: $(cat "$log")"; }
    sleep 0.05
done
kill -INT "$pid"
wait "$pid" || fail "after Ctrl-C: exit $?: $(tail -n 5 "$log")"
rm -f "$log"
