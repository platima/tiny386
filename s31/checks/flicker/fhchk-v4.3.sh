# fhchk.sh: the flicker runs of v4.3 against apply42's of v4.2, beyond the END line:
#  1. every "board:" line of each run (the pictures, the reference, the clears, the black, the
#     guest's updates, the blips, the strips) against the v4.2 run's, the host-time parts taken out
#  2. the analysis table's rows (per segment: pictures, mid-update count, share and rate, the pointer
#     whole/part/none, updates, clears over a picture, blank pictures) against apply42's tables
#  3. the threaded runs' checks with the guest paused
#  4. MODE13X and BMODES under FreeDOS (-board), v4.3 against apply42's v4.2 runs: END lines, shots,
#     the reference and the count of black
set -u
A=~/v43/merge; H=$A/fh; X=~/flicker/apply42/h
norm() { grep -a "board:" $1/log.txt | sed 's/[0-9.]* host s//g; s/wall=[0-9.]* s//'; }
echo "==== 1. the board lines against v4.2's run"
n=0; s=0
while read -r l; do
  case "$l" in ''|'#'*) continue ;; esac
  r=${l%% *}
  case $r in *thread*) continue ;; esac
  o=$X/runs/$(echo $r | sed 's/^a43-/a42-/; s/^d43-/d42-/; s/^c43-/c42-/; s/^e43-/e42-/')
  n=$((n+1))
  if diff <(norm $H/runs/$r) <(norm $o) > /dev/null; then s=$((s+1)); else echo "  DIFFERENT: $r"; diff <(norm $H/runs/$r) <(norm $o) | head -6; fi
done < $A/l1-43.txt
echo "  $s of $n runs (those not threaded) with every board line the v4.2 run's"
echo "==== 2. the analysis tables"
tab() { sed -n '/^run  /,$p' $1 | tail -n +2; }
tab $H/summary-l1.txt | sed 's/^a43-/a42-/; s/^d43-/d42-/; s/^c43-/c42-/; s/^e43-/e42-/' > $A/tab-mine.txt
{ tab $X/summary-l1.txt; tab $X/summary-l8.txt; } > $A/tab-theirs.txt
n=0; same=0
while read -r l; do
  set -- $l
  case $1 in *thread*) continue ;; esac
  t=$(awk -v r=$1 -v s=$2 '$1==r && $2==s' $A/tab-theirs.txt | head -1)
  [ -z "$t" ] && { echo "  no earlier row: $1 $2"; continue; }
  n=$((n+1))
  a=$(echo "$l" | awk '{print $1,$2,$3,$4,$5,$6,$7,$8,$9,$13,$14}')
  b=$(echo "$t" | awk '{print $1,$2,$3,$4,$5,$6,$7,$8,$9,$13,$14}')
  if [ "$a" = "$b" ]; then same=$((same+1)); else echo "  DIFF: $a"; echo "        $b"; fi
done < $A/tab-mine.txt
echo "  $n segment rows (the runs not threaded) against apply42's: $same the same"
rm -f $A/tab-mine.txt $A/tab-theirs.txt
echo "==== 3. the threaded runs (the drawing in its own thread; the guest paused every 10th picture)"
for r in a43-n16-thread a43-n16dib-thread d43-ps1-thread; do
  echo "-- $r"; grep -a "vga thread\|paused\|checks with the guest" $H/runs/$r/log.txt | sed 's/^\[harness\] /   /' | cut -c1-220
done
echo "==== 4. MODE13X and BMODES"
export MTOOLS_SKIP_CHECK=1 PATH=$PATH:/sbin:/usr/sbin
cd $H
one() { local n=$1 b=$2 img=$3 vga=$4
  IMG=$A/m13/$img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 bash harness/run.sh bin/$b/headless runs/$n 4M $vga \
    -ips 2000000 -idle 16 -vsecs 400 -board > /dev/null 2>&1; }
one dos-m13x-r43-256K r43 fd-mode13x.img 256K &
one dos-m13x-r43-768K r43 fd-mode13x.img 768K &
one dos-bmodes-r43-256K r43 fd-bmodes.img 256K &
wait
endline() { grep -a "\] END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
for r in m13x-%-256K m13x-%-768K bmodes-%-256K; do
  a=$X/runs/dos-${r/\%/r42}; c=runs/dos-${r/\%/r43}
  e=same; [ "$(endline $a)" = "$(endline $c)" ] || e=DIFFERENT
  s=0; d=0; for f in $a/shot-*.png; do s=$((s+1)); cmp -s $f $c/$(basename $f) || d=$((d+1)); done
  echo "== ${r/-%/}: v4.3 against v4.2: END $e; $s shots, $d differ"
  echo "   $(grep -a 'board: [0-9]* pictures' $c/log.txt | sed 's/.*board: //' | cut -c1-120)"
  echo "   $(grep -a 'board: black between' $c/log.txt | sed 's/.*board: //')"
  diff <(norm $c) <(norm $a) > /dev/null && echo "   every board line v4.2's" || echo "   board lines DIFFER from v4.2's"
done
