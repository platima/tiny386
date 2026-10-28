#!/bin/bash
# tests/host/profile/standin.sh: the PC repro's headless harness built from
# this tree as the board's core is built (its esp flavour), then linked
# again with the profiler's stand-in (standin.c, prof_hist.c): OUT/headless
# is the harness as it is, OUT/headless-standin samples itself and writes
# PROFILE.TXT (s31/PROFILE.md 6).  Build only.
#
#   bash tests/host/profile/standin.sh OUT [64|32|rv]      (default rv)
#
# HARNESS: the harness (default ~/org2/pcm/canary/harness, the copy that
# tests/lockstep uses; it is not in this tree).  OUT/standin-sha256.txt has
# the stand-in's SHA-256, for PROF_ELF_SHA when it runs:
#
#   PROF_OUT=$PWD/PROFILE.TXT PROF_ELF_SHA=$(cat OUT/standin-sha256.txt) \
#       bash $HARNESS/run.sh OUT/headless-standin RUNDIR 14M 320K [options]
#   python3 s31/profile/profile_report.py PROFILE.TXT OUT/headless-standin
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: standin.sh OUT [64|32|rv]}
ARCH=${2:-rv}
H=${HARNESS:-$HOME/org2/pcm/canary/harness}
W=$(cd "$H/.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
bash "$H/build.sh" "$TREE" esp "$OUT" "$ARCH"
case "$ARCH" in
64) MARCH="-m64" ;;
32) MARCH="-m32 -I$W/inc32" ;;
rv) MARCH="-m32 -I$W/inc32 -funsigned-char" ;;
esac
# standin.c with headless.c's own flags (the board header, the shims)
line=$(grep -F -- "-c $H/headless.c -o $OUT/obj/headless.o" "$OUT/commands.txt")
line=${line/-c $H\/headless.c -o $OUT\/obj\/headless.o/-I$TREE/esp/main -c $HERE/standin.c -o $OUT/standin.o}
sh -c "$line"
gcc $MARCH -O2 -g -Wall -Wextra -DPROF_HOST -I"$TREE/esp/main" -c "$TREE/esp/main/prof_hist.c" \
    -o "$OUT/prof_hist.o"
# no PIE: the PCs a signal sees are the ELF's own addresses
gcc $MARCH -no-pie -o "$OUT/headless-standin" "$OUT"/obj/*.o "$OUT/standin.o" "$OUT/prof_hist.o" \
    -Wl,--wrap=pc_new -Wl,--wrap=pc_step -lm -lpthread -lrt
sha256sum "$OUT/headless-standin" | cut -d' ' -f1 > "$OUT/standin-sha256.txt"
echo "built $OUT/headless-standin ($(cut -c1-16 "$OUT/standin-sha256.txt"))"
