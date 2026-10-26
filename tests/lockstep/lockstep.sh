#!/bin/bash
# tests/lockstep: run a reference tree and a candidate tree on the same
# workload, side by side, and find the first instruction where the guest
# sees something different.
#
#   bash tests/lockstep/lockstep.sh REFTREE CANDTREE WORKLOAD OUTDIR
#
# REFTREE, CANDTREE  two tiny386 trees (the reference is normally the
#           release the candidate starts from, s31-v4.6 = 35a201c)
# WORKLOAD  b1, restart, msdos, bench or vbe (workloads.sh)
# OUTDIR    builds, runs and the report (OUTDIR/report.txt)
#
# Pass 1 runs both to the end with a record every EVERY instructions and
# every interrupt logged.  If they differ, pass 2 runs both again (the runs
# are deterministic) with a record after every step from the last record
# that agreed to the first that did not, and every port access logged;
# pass 3 runs both again to the start of the first step that differs and
# there forks one child for each prefix of the step (1, 2, 3 ...
# instructions), which names the first instruction whose result differs.
#
# Environment:
#   EVERY        instructions between records in pass 1 (1000000)
#   PASSES       1 to stop after pass 1 (3)
#   REF_BUILD, CAND_BUILD   use these build.sh outputs instead of building
#   REF_SED_I386, CAND_SED_I386   the harness's SED_I386 for one side only
#                (selftest.sh plants its bugs this way); likewise _SED_PC,
#                _SED_VGA, _SED_IDE, _SED_I8042, _SED_I8254, _SED_MISC
#   IGNORE       fields compare.py leaves out, with commas (dirty, for a
#                candidate that marks video memory in other blocks on
#                purpose)
#   HARNESS      the PC harness (build.sh)
# Exit status: 0 the same, 1 a difference found, 2 an error.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -eq 4 ] || { echo "usage: lockstep.sh REFTREE CANDTREE WORKLOAD OUTDIR"; exit 2; }
REF=$1
CAND=$2
WL=$3
mkdir -p "$4"
OUT=$(cd "$4" && pwd)
EVERY=${EVERY:-1000000}
PASSES=${PASSES:-3}
CMP="python3 -I $HERE/compare.py"
REPORT=$OUT/report.txt
: > "$REPORT"
say() { echo "$*" | tee -a "$REPORT"; }
# a value from a compare.py JSON file: jget FILE first.anchor_cycle (empty if none)
jget() {
    python3 -I -c '
import json, sys
d = json.load(open(sys.argv[1]))
for k in sys.argv[2].split("."):
    d = d.get(k) if isinstance(d, dict) else None
print("" if d is None else d)' "$@"
}

say "lockstep: workload $WL"
say "  ref  $REF ($(git -C "$REF" rev-parse --short HEAD 2>/dev/null || echo '?'))"
say "  cand $CAND ($(git -C "$CAND" rev-parse --short HEAD 2>/dev/null || echo '?'))"

# ---- builds
RB=${REF_BUILD:-$OUT/build-ref}
CB=${CAND_BUILD:-$OUT/build-cand}
pids=()
seds() {  # the harness's SED_ variables for one side (REF or CAND)
    local v out=()
    for v in I386 PC VGA IDE I8042 I8254 MISC; do
        local n=${1}_SED_$v
        out+=("SED_$v=${!n:-}")
    done
    printf '%s\0' "${out[@]}"
}
if [ -z "${REF_BUILD:-}" ]; then
    mapfile -d '' rs < <(seds REF)
    env "${rs[@]}" bash "$HERE/build.sh" "$REF" "$RB" > "$OUT/build-ref.log" 2>&1 &
    pids+=($!)
fi
if [ -z "${CAND_BUILD:-}" ]; then
    mapfile -d '' cs < <(seds CAND)
    env "${cs[@]}" bash "$HERE/build.sh" "$CAND" "$CB" > "$OUT/build-cand.log" 2>&1 &
    pids+=($!)
fi
for p in "${pids[@]}"; do wait "$p" || { say "a build failed: $OUT/build-*.log"; exit 2; }; done
[ -x "$RB/headless" ] && [ -x "$CB/headless" ] || { say "no builds"; exit 2; }
for side in REF CAND; do
    for v in I386 PC VGA IDE I8042 I8254 MISC; do
        n=${side}_SED_$v
        [ -n "${!n:-}" ] && say "  ${side,,} built with SED_$v: ${!n}"
    done
done
IGN=()
[ -n "${IGNORE:-}" ] && IGN=(--ignore "$IGNORE") && say "  not compared: $IGNORE"

both() {  # pass-name, then LS_ variables for both runs
    local name=$1
    shift
    env "$@" bash "$HERE/run.sh" "$RB" "$WL" "$OUT/$name-ref" > /dev/null 2>&1 &
    local a=$!
    env "$@" bash "$HERE/run.sh" "$CB" "$WL" "$OUT/$name-cand" > /dev/null 2>&1 &
    local b=$!
    wait $a
    wait $b
    if [ -f "$OUT/$name-ref/disk-start.sha256" ] &&
       ! cmp -s "$OUT/$name-ref/disk-start.sha256" "$OUT/$name-cand/disk-start.sha256"; then
        say "  the two runs did not start from the same disk image: the comparison means nothing"
        exit 2
    fi
    say "  host time: ref $(cat "$OUT/$name-ref/time.txt") s, cand $(cat "$OUT/$name-cand/time.txt") s;" \
        "exit status ref $(cat "$OUT/$name-ref/status.txt"), cand $(cat "$OUT/$name-cand/status.txt")"
}

# ---- pass 1
say ""
say "pass 1: a record every $EVERY instructions, every interrupt"
both p1 LS_EVERY="$EVERY"
for s in ref cand; do
    grep -h "END t=" "$OUT/p1-$s/log.txt" | sed "s/^/  $s: /" | cut -c1-110 | tee -a "$REPORT"
done
$CMP records "$OUT/p1-ref/ls" "$OUT/p1-cand/ls" --json "$OUT/p1.json" "${IGN[@]}" > "$OUT/p1.txt"
rc=$?
sed 's/^/  /' "$OUT/p1.txt" | tee -a "$REPORT"
da=$(cat "$OUT/p1-ref/disk.sha256")
db=$(cat "$OUT/p1-cand/disk.sha256")
if [ "$da" = "$db" ]; then
    say "  disk image after the run: the same (sha256 ${da:0:16})"
else
    say "  disk image after the run: DIFFERS (ref ${da:0:16}, cand ${db:0:16})"
    [ $rc = 0 ] && rc=1
fi
if [ $rc = 0 ]; then
    say ""
    say "RESULT: no difference in $WL"
    exit 0
fi
[ $rc = 2 ] && { say "RESULT: error in pass 1"; exit 2; }
if [ "$PASSES" -lt 2 ] || [ -z "$(jget "$OUT/p1.json" first.anchor_cycle)" ]; then
    say ""
    say "RESULT: $WL differs (pass 1 above)"
    exit 1
fi

# ---- pass 2
A=$(jget "$OUT/p1.json" first.anchor_cycle)
B=$(jget "$OUT/p1.json" first.end_cycle)
say ""
say "pass 2: a record after every step from cycle $A to $B, every port access there"
both p2 LS_EVERY=0 LS_DENSE_FROM="$A" LS_DENSE_TO="$B" LS_EXIT_CYCLE="$B"
$CMP records "$OUT/p2-ref/ls" "$OUT/p2-cand/ls" --json "$OUT/p2.json" "${IGN[@]}" > "$OUT/p2.txt"
rc=$?
sed 's/^/  /' "$OUT/p2.txt" | tee -a "$REPORT"
python3 -I - "$OUT/p2-ref/ls/io.txt" "$OUT/p2-cand/ls/io.txt" <<'PY' | sed 's/^/  /' | tee -a "$REPORT"
import sys
try:
    a = open(sys.argv[1]).read().splitlines()
    b = open(sys.argv[2]).read().splitlines()
except OSError:
    sys.exit(0)
print('port accesses in the window: ref %d, cand %d' % (len(a), len(b)))
for i in range(min(len(a), len(b))):
    if a[i] != b[i]:
        print('first port access that differs:')
        print('  ref  %s' % a[i])
        print('  cand %s' % b[i])
        break
else:
    if len(a) != len(b):
        print('one run has more port accesses than the other in the window')
    else:
        print('every port access in the window agrees')
PY
if [ $rc != 1 ] || [ "$PASSES" -lt 3 ]; then
    say ""
    say "RESULT: $WL differs; pass 2 $( [ $rc = 0 ] && echo 'found no difference (look at pass 1)' || echo 'above')"
    exit 1
fi
S=$(jget "$OUT/p2.json" first.anchor_step)
SC=$(jget "$OUT/p2.json" first.anchor_cycle)
if [ "$S" -lt 0 ]; then
    # the window's first record already differs: start from pass 1's anchor
    S=$(jget "$OUT/p1.json" first.anchor_step)
    SC=$A
    [ "$S" -lt 0 ] && S=0
fi

# ---- pass 3
say ""
say "pass 3: step $((S + 1)) (it starts at cycle $SC) split into its prefixes"
both p3 LS_EVERY=0 LS_SPLIT_STEP="$S" LS_DENSE_FROM="$SC" LS_DENSE_TO="$((SC + 4096))" \
    LS_EXIT_STEP="$((S + 2))"
$CMP children "$OUT/p3-ref/ls" "$OUT/p3-cand/ls" --json "$OUT/p3.json" "${IGN[@]}" > "$OUT/p3.txt"
sed 's/^/  /' "$OUT/p3.txt" | tee -a "$REPORT"
say ""
say "RESULT: $WL differs; the first instruction that differs is in pass 3 above"
exit 1
