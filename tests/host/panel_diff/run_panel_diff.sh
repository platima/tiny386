#!/bin/bash
# The ESP-Mosaico panel's changed-block finder (esp/main/panel_diff.c) on the
# PC, and three planted faults it must catch:
#   bash tests/host/panel_diff/run_panel_diff.sh [OUT]
# Exit status 0 when it passes.
set -u
# GCC 12's AddressSanitizer loops for ever, printing DEADLYSIGNAL, on about
# one start in four under the address randomisation of newer kernels (WSL2
# 6.18, 2026-11-17: 102 of 400 starts of an empty program; one such loop
# filled the disk with an 88 GB log).  The tests do not need it, so they
# run without it.
if [ -z "${HOST_NORAND:-}" ] && setarch -R true 2>/dev/null; then
	HOST_NORAND=1 exec setarch -R bash "$0" "$@"
fi
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/../build}
CC=${CC:-gcc}
mkdir -p "$OUT" || exit 2
build() {   # name, then flags
	local n=$1; shift
	$CC -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$TREE/esp/main" "$@" \
	    -o "$OUT/$n" "$HERE/test_panel_diff.c" "$TREE/esp/main/panel_diff.c"
}
# about 11 s on the PC; the limit stops a run that never ends
build test_panel_diff || exit 1
timeout 300 "$OUT/test_panel_diff" || exit 1
for p in 1 2 3; do
	build test_panel_diff_plant$p -DPLANT=$p || exit 1
	# 124: the time limit, which is not a fault caught
	timeout 300 "$OUT/test_panel_diff_plant$p" > "$OUT/test_panel_diff_plant$p.log" 2>&1
	rc=$?
	if [ $rc = 0 ] || [ $rc = 124 ]; then
		echo "panel_diff: planted fault $p was not caught (status $rc)"
		exit 1
	fi
	echo "panel_diff: planted fault $p caught: $(head -1 "$OUT/test_panel_diff_plant$p.log")"
done
exit 0
