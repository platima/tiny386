#!/bin/bash
# fr.sh: the flicker reproduction (research\FLICKER-REPRO.md, ~/flicker/repro/repro.sh)
# run from ~/flicker/fix with a list of runs.
#   bash fr.sh LISTFILE SUMMARYNAME
# LISTFILE: one run a line: NAME BIN(tree path or built name) IMG MEM VGA VGABIOS SESSION [options]
#   SESSION plat (the session, after-desktop 205, dialogs 1) or sweep (after-desktop 50, dialogs 6)
set -u
LISTF=$(readlink -f "$1")
SUMNAME=$2
F=$HOME/flicker/fix
H=$F/h
cd $H || exit 1
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
JOBS=${JOBS:-6}
python3 -I harness/session.py $F/sess/plat.txt $F/sess/plat > /dev/null
python3 -I harness/session.py $F/sess/sweep.txt $F/sess/sweep > /dev/null
python3 -I harness/session.py $F/sess/dosfix.txt $F/sess/dosfix > /dev/null
build() {   # TREE BINNAME
  [ -x bin/$2/headless ] && [ -z "${REBUILD:-}" ] && return 0
  BOARD=${BOARDNAME:-korvo1} bash harness/build.sh "$1" esp bin/$2 64 > bin/$2.build.txt 2>&1 || {
    cat bin/$2.build.txt; echo "build of $1 failed"; exit 1; }
  grep -h "^instr" bin/$2/instr.txt | sed "s/^/  $2: /"
}
one() {     # NAME BIN IMG MEM VGA VGABIOS SESSION [options]
  local name=$1 bin=$2 img=$3 mem=$4 vga=$5 vb=$6 s=$7; shift 7
  local aft=205 dl=1 seg=plat-segments.txt
  if [ "$s" = sweep ]; then aft=50; dl=6; seg=sweep-segments.txt; fi
  if [ "$s" = dosfix ]; then aft=320; seg=dosfix-segments.txt; export KEEP=1; fi
  case "$img" in /*) ;; *) img=$H/img/$img ;; esac
  IMG=$img VGABIOS=$H/img/$vb WALLMAX=1800 bash harness/run.sh bin/$bin/headless runs/$name $mem $vga \
    -ips 2000000 -idle 16 -vsecs 1000 -after-desktop $aft -dialogs $dl -snap 30 \
    -board -session-after 10 -skeys $F/sess/$s.keys -smouse $F/sess/$s.mouse -strips 30 "$@" > /dev/null 2>&1
  python3 -I harness/analyse.py runs/$name --segments $F/sess/$seg > runs/$name/analysis.txt 2>&1
  python3 -I harness/analyse.py runs/$name --segments $F/sess/$seg --tsv > runs/$name/analysis.tsv 2>&1
  python3 -I harness/strips.py runs/$name > runs/$name/strips.txt 2>&1
  echo "$name: $(head -1 runs/$name/outcome.txt)"
}
LIST=()
while read -r l; do
  case "$l" in ''|'#'*) continue ;; esac
  LIST+=("$l")
done < "$LISTF"
# builds: BIN field "name=tree"
for l in "${LIST[@]}"; do
  b=$(echo $l | awk '{print $2}')
  case "$b" in *=*) build "${b#*=}" "${b%%=*}" ;; esac
done
for l in "${LIST[@]}"; do
  set -- $l
  n=$1; b=${2%%=*}; shift 2
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
  one $n $b "$@" &
done
wait
{
  echo "flicker fix runs, $(date "+%F %T"), list $LISTF"
  NAMES=()
  for l in "${LIST[@]}"; do
    n=${l%% *}
    NAMES+=("$n")
    echo
    echo "== $n: $(head -1 runs/$n/outcome.txt)"
    echo "   $(echo $l | cut -d' ' -f2-)"
    grep -a "board:" runs/$n/log.txt | sed 's/^\[harness\] /   /'
  done
  echo
  python3 -I harness/table.py runs "${NAMES[@]}"
} > $F/h/summary-$SUMNAME.txt
cat $F/h/summary-$SUMNAME.txt
