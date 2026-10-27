#!/bin/bash
# tests/lockstep: show that the tool finds what it is for.  Each file in
# bugs/ is a one-line sed script that plants a small bug in a copy of one
# of the core's files (never in the tree); the tool runs the tree against
# itself with the bug in the candidate only, on the workload the file
# names, and must name the first instruction where the bug shows.  The
# same workloads with no bug must show no difference.
#
#   bash tests/lockstep/selftest.sh TREE OUTDIR [bug ...]
#
# TREE      the tree to test with (normally this one)
# bug       names in bugs/ (default: all of them)
#
# A bug file starts with comment lines: "# file: i386" (or pc, vga ...: the
# harness's SED_ variable it goes in; i386_fast for i386_fast.h, v5.0),
# "# workload: b1" (where it shows), optionally "# needs: fast" or "# needs:
# tiny386" (v5.0: a bug only the fast interpreter, or only tiny386's own
# interpreter, ever runs), then what it is.  WORKLOAD=name runs every bug
# on that workload instead; a bug that a workload never reaches then shows
# no difference, which is right, and is reported as not found.
#
# FAST=0 builds both sides with the fast interpreter off
# (-DI386_FAST_DEFAULT=0); FAST=1, the default, as the tree builds it (on,
# in a v5.0 tree).  A tree without i386_fast.h is FAST=0.  The bugs that
# need the other are skipped, and said so.
#
# Leaves OUTDIR/<bug>/report.txt for each, and OUTDIR/summary.txt.
# Exit status 0 when every bug was found and the runs without one agreed.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 2 ] || { echo "usage: selftest.sh TREE OUTDIR [bug ...]"; exit 2; }
TREE=$(cd "$1" && pwd)
mkdir -p "$2"
OUT=$(cd "$2" && pwd)
shift 2
if [ $# -gt 0 ]; then BUGS=("$@"); else
    BUGS=()
    for f in "$HERE"/bugs/*.sed; do BUGS+=("$(basename "$f" .sed)"); done
fi
hdr() { sed -n "s/^# $2: //p" "$HERE/bugs/$1.sed" | head -1; }
desc() { grep '^# ' "$HERE/bugs/$1.sed" | grep -v '^# file: \|^# workload: \|^# needs: ' | sed 's/^# //'; }
FAST=${FAST:-1}
[ -f "$TREE/i386_fast.h" ] || FAST=0
[ "$FAST" = 0 ] && export EXTRA_CFLAGS="${EXTRA_CFLAGS:-} -DI386_FAST_DEFAULT=0"
skip() {  # why a bug is skipped with this FAST, or nothing
    case "$(hdr "$1" needs)" in
    fast) [ "$FAST" = 0 ] && echo "skipped: it needs the fast interpreter (FAST=1)" ;;
    tiny386) [ "$FAST" = 1 ] && echo "skipped: only tiny386's own interpreter runs it (FAST=0)" ;;
    esac
}
bash "$HERE/build.sh" "$TREE" "$OUT/build-ref" > "$OUT/build-ref.log" 2>&1 || {
    cat "$OUT/build-ref.log"; exit 2; }

declare -A WLS
RUN=()
for b in "${BUGS[@]}"; do
    [ -f "$HERE/bugs/$b.sed" ] || { echo "no bug $b"; exit 2; }
    [ -n "$(skip "$b")" ] && continue
    RUN+=("$b")
    WLS[${WORKLOAD:-$(hdr "$b" workload)}]=1
done
pids=()
for w in "${!WLS[@]}"; do
    REF_BUILD=$OUT/build-ref CAND_BUILD=$OUT/build-ref PASSES=1 \
        bash "$HERE/lockstep.sh" "$TREE" "$TREE" "$w" "$OUT/none-$w" > "$OUT/none-$w.out" 2>&1 &
    pids+=($!)
done
for b in "${RUN[@]}"; do
    file=$(hdr "$b" file)
    w=${WORKLOAD:-$(hdr "$b" workload)}
    env REF_BUILD="$OUT/build-ref" "CAND_SED_${file^^}=$(grep -v '^#' "$HERE/bugs/$b.sed")" \
        bash "$HERE/lockstep.sh" "$TREE" "$TREE" "$w" "$OUT/$b" > "$OUT/$b.out" 2>&1 &
    pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

ok=1
{
    echo "selftest: tree $TREE, FAST=$FAST"
    for w in "${!WLS[@]}"; do
        if grep -q "^RESULT: no difference" "$OUT/none-$w.out"; then
            echo "no bug, $w: no difference (as it must be)"
        else
            echo "no bug, $w: FAILED, a difference was found ($OUT/none-$w)"; ok=0
        fi
    done
    for b in "${BUGS[@]}"; do
        echo ""
        f=$(hdr "$b" file).c
        [ "$f" = i386_fast.c ] && f=i386_fast.h
        echo "bug $b ($f, workload ${WORKLOAD:-$(hdr "$b" workload)}): $(desc "$b")"
        if [ -n "$(skip "$b")" ]; then
            echo "  $(skip "$b")"
        elif grep -q "first instruction whose result differs\|the state at the start of the step already differs" "$OUT/$b.out"; then
            sed -n '/^pass 3/,/^RESULT/p' "$OUT/$b.out" | grep -v "^RESULT\|^$\|host time"
        elif grep -q "^RESULT: .* differs" "$OUT/$b.out"; then
            echo "  found, but not to one instruction:"
            grep "first record that differs\|first interrupt\|fields that differ\|SAME\|RESULT" "$OUT/$b.out" | sed 's/^/  /'
            ok=0
        else
            echo "  NOT FOUND (no difference: this workload never reached the planted code in a way the guest could see)"
            ok=0
        fi
    done
    echo ""
    [ $ok = 1 ] && echo "SELFTEST PASSED" || echo "SELFTEST FAILED"
} | tee "$OUT/summary.txt"
[ $ok = 1 ]
