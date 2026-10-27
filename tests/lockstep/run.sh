#!/bin/bash
# tests/lockstep: one run of one build on one workload, from a fresh copy of
# the workload's disk image.
#
#   bash tests/lockstep/run.sh BUILD WORKLOAD RUNDIR [headless options...]
#
# BUILD     a directory build.sh made (BUILD/headless)
# WORKLOAD  b1, restart, msdos, bench, vbe or t386mon (workloads.sh)
# RUNDIR    made afresh; the hook's files go in RUNDIR/ls
#
# The LS_* variables in the environment go to the hook as they are
# (README.md); LS_OUT is set to RUNDIR/ls unless NOHOOK=1, which runs the
# program with the hook off (for checking that the hook changes nothing).
# The disk image copy is hashed after the run (RUNDIR/disk.sha256) and
# deleted, and so are the harness's pictures, unless KEEP=1.
#
# Leaves RUNDIR/log.txt (the harness's log), cmd.txt, time.txt (host
# seconds), status.txt (exit status), disk.sha256 and ls/.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 3 ] || { echo "usage: run.sh BUILD WORKLOAD RUNDIR [options]"; exit 2; }
BUILD=$(cd "$1" && pwd)
BIN=$BUILD/headless
[ -x "$BIN" ] || { echo "no $BIN"; exit 2; }
. "$HERE/workloads.sh"
workload "$2" || exit 2
RUN=$3
shift 3
case "$RUN" in /*) ;; *) RUN=$PWD/$RUN ;; esac
HARNESS=${HARNESS:-$HOME/org2/pcm/canary/harness}
ROMS=${ROMS:-$(cd "$HARNESS/.." && pwd)/img}
rm -rf "$RUN"
mkdir -p "$RUN"
cp "$W_IMG" "$RUN/disk.img"
chmod 644 "$RUN/disk.img"
if [ -n "$W_PREP" ]; then
    "$W_PREP" "$RUN/disk.img" "$RUN" > "$RUN/prep.txt" 2>&1 || {
        echo "preparing the image for $W_NAME failed: $RUN/prep.txt"; rm -f "$RUN/disk.img"; exit 2; }
    # what the run starts from, for lockstep.sh to check that both sides
    # had the same bytes
    sha256sum "$RUN/disk.img" | cut -d' ' -f1 > "$RUN/disk-start.sha256"
fi
cat > "$RUN/run.ini" <<EOF
[pc]
bios = $ROMS/bios.bin
vga_bios = $ROMS/vgabios.bin
mem_size = $W_MEM
vga_mem_size = $W_VGA
hda = $RUN/disk.img
fill_cmos = 1

[display]
width = 800
height = 480

[cpu]
gen = $W_GEN
fpu = $W_FPU
EOF
if [ -z "${NOHOOK:-}" ]; then
    export LS_OUT=$RUN/ls
    [ -n "$W_STOP" ] && [ -z "${LS_STOP_BYTE:-}" ] && export LS_STOP_BYTE=$W_STOP
else
    unset LS_OUT
fi
{
    echo "$BIN -out $RUN ${W_OPTS[*]} $* $RUN/run.ini"
    echo "workload $W_NAME image $W_IMG mem $W_MEM vga $W_VGA gen $W_GEN fpu $W_FPU"
    env | grep '^LS_' | sort
    cat "$BUILD/lockstep.txt" 2>/dev/null
} > "$RUN/cmd.txt"
t0=$(date +%s.%N)
(cd "$RUN" && exec "$BIN" -out "$RUN" "${W_OPTS[@]}" "$@" "$RUN/run.ini") > "$RUN/log.txt" 2>&1
rc=$?
t1=$(date +%s.%N)
echo "$rc" > "$RUN/status.txt"
python3 -c "print('%.1f' % ($t1 - $t0))" > "$RUN/time.txt"
sha256sum "$RUN/disk.img" | cut -d' ' -f1 > "$RUN/disk.sha256"
if [ -z "${KEEP:-}" ]; then
    rm -f "$RUN/disk.img" "$RUN"/*.ppm "$RUN"/snap*.txt "$RUN"/dialog*.txt "$RUN"/desktop.txt \
        "$RUN"/final.txt
fi
exit 0
