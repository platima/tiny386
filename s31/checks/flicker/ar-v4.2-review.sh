#!/bin/bash
# ar.sh: my flicker runs (a copy of ~/flicker/fix/fr.sh and ~/flicker/review/rr.sh with my paths,
# the harness copy with the apply42 change (harness-apply42.diff), and runs with no session)
#   bash ar.sh LISTFILE SUMMARYNAME
# LISTFILE: NAME BIN IMG MEM VGA VGABIOS SESSION AFTER DIALOGS VSECS [@keep] [@fda=F] [@fdb=F] [options]
#   SESSION - : no session (keys at fixed virtual seconds with -keyfile in the options instead)
#   @keep: the disk image is kept in runs/NAME; @fda/@fdb: a copy of F as floppy A or B
set -u
LISTF=$(readlink -f "$1")
SUMNAME=$2
A=$HOME/flicker/apply42
H=$A/h
cd $H || exit 1
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
JOBS=${JOBS:-8}
for s in $A/sess/*.txt; do
  case "$s" in *-segments.txt) continue ;; esac
  python3 -I harness/session.py $s ${s%.txt} > /dev/null
done
mkdir -p $A/fd
one() {
  local name=$1 bin=$2 img=$3 mem=$4 vga=$5 vb=$6 s=$7 aft=$8 dl=$9 vs=${10}; shift 10
  local keep= fda= fdb=
  while [ $# -gt 0 ]; do
    case "$1" in
      @keep) keep=1; shift ;;
      @fda=*) fda=$A/fd/$name-a.img; cp "${1#@fda=}" $fda; chmod 644 $fda; shift ;;
      @fdb=*) fdb=$A/fd/$name-b.img; cp "${1#@fdb=}" $fdb; chmod 644 $fdb; shift ;;
      *) break ;;
    esac
  done
  local sargs=()
  [ "$s" = - ] || sargs=(-after-desktop $aft -dialogs $dl -session-after 10 -skeys $A/sess/$s.keys -smouse $A/sess/$s.mouse)
  case "$img" in /*) ;; *) img=$H/img/$img ;; esac
  KEEP=$keep FDA=$fda FDB=$fdb IMG=$img VGABIOS=$H/img/$vb WALLMAX=2400 bash harness/run.sh bin/$bin/headless runs/$name $mem $vga \
    -ips 2000000 -idle 16 -vsecs $vs -snap 30 -board "${sargs[@]}" -strips 30 "$@" > /dev/null 2>&1
  local seg=$A/sess/$s-segments.txt
  if [ -f $seg ]; then
    python3 -I harness/analyse.py runs/$name --segments $seg > runs/$name/analysis.txt 2>&1
    python3 -I harness/analyse.py runs/$name --segments $seg --tsv > runs/$name/analysis.tsv 2>&1
  fi
  python3 -I harness/strips.py runs/$name > runs/$name/strips.txt 2>&1
  echo "$name: $(head -1 runs/$name/outcome.txt)"
}
LIST=()
while read -r l; do
  case "$l" in ''|'#'*) continue ;; esac
  LIST+=("$l")
done < "$LISTF"
start=$(date +%s)
for l in "${LIST[@]}"; do
  set -- $l
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
  one "$@" &
done
wait
{
  echo "apply42 flicker runs, $(date "+%F %T"), list $LISTF, $(( $(date +%s) - start )) s"
  NAMES=()
  for l in "${LIST[@]}"; do
    n=${l%% *}
    NAMES+=("$n")
    echo
    echo "== $n: $(head -1 runs/$n/outcome.txt)"
    echo "   $(echo $l | cut -d' ' -f2-)"
    grep -a "board:\|END" runs/$n/log.txt | sed 's/^\[harness\] /   /'
  done
  echo
  python3 -I harness/table.py runs "${NAMES[@]}"
} > $H/summary-$SUMNAME.txt
echo "summary in $H/summary-$SUMNAME.txt"
