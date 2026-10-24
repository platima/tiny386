# cmp43.sh: std43.sh's runs of v4.3 against apply42's v4.2 runs (FX) and v4.1's own (O): the
# emulator's END line (host time taken out; the port's own END line is not in it) and the final
# picture; the port's END line read as a guest reads it, in each v4.3 run
set -u
P=~/v43/merge; H=$P/ph; W41=~/v4-work/v41; FX=~/flicker/apply42/h41; O=$W41/h
cd $H
endline() { grep -a "\] END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
oc() { head -1 $1/outcome.txt | cut -c10-; }
aud() { grep -a -o 'audit: [0-9]* pictures held against a full redraw, [0-9]* with a difference' $1/log.txt; }
same() { [ -n "$(endline $1)" ] && [ "$(endline $1)" = "$(endline $2)" ] && cmp -s $1/final.png $2/final.png; }
port() { grep -a "perfport END" $1/log.txt | grep -o 'magic [0-9a-f]* version [0-9a-f]*' ; }
pp=0; ppn=0
countport() { ppn=$((ppn+1)); [ "$(port $1)" = "magic 36383354 version 00010001" ] && pp=$((pp+1)); }
echo "==== 1. the 27 standard configurations"
echo "-- $(grep -c 'desktop (' runs-std-v43/summary.txt) of 27 at the desktop"
n=0; s=0; f=0
for d in runs-std-v43/S-v43-*; do c=${d#runs-std-v43/S-v43-}; n=$((n+1)); countport $d
  if same $d $O/runs-std-w41/S-w41-$c; then s=$((s+1)); else echo "  DIFFERENT from v4.1: $c"; fi
  if same $d $FX/runs-std-v42/S-v42-$c; then f=$((f+1)); else echo "  DIFFERENT from v4.2: $c"; fi
done
echo "-- $s of $n with v4.1's END line and final picture; $f of $n with v4.2's (apply42's runs)"
echo "==== 2. the 11 native configurations"
for d in runs/v43-n* runs/v43-dos*; do r=${d#runs/v43-}; countport $d
  a=same; same $d $O/runs/w41-$r || a=DIFFERENT
  b=same; same $d $FX/runs/v42-$r || b=DIFFERENT
  printf '  %-26s %-40s v4.1 %-9s v4.2 %-9s %s\n' $r "$(oc $d)" $a $b "$(aud $d)"
done
echo "==== 3. BVBE480"
awk '{ $NF=""; print }' $P/bv-v43.out
for d in runs/vbe480-v43-*; do r=${d#runs/vbe480-v43-}; countport $d
  a=same; [ "$(endline $d)" = "$(endline $O/runs/vbe480-w41-$r)" ] || a=DIFFERS
  b=same; [ "$(endline $d)" = "$(endline $FX/runs/vbe480-v42-$r)" ] || b=DIFFERS
  echo "  $r: END against v4.1 $a, against v4.2 $b"
done
echo "==== 4. BENCH"
countport runs-bench/bench-v43
paste <(grep -a -o 'mark [0-9a-f]* at insns=[0-9]*' runs-bench/bench-v43/log.txt) <(grep -a -o 'mark [0-9a-f]* at insns=[0-9]*' $FX/runs-bench/bench-v42/log.txt) | awk '{ if ($1" "$2" "$4 == $5" "$6" "$8) s++; n++ } END { print "  " s+0 " of " n " marks at v4.2'"'"'s instruction count" }'
same runs-bench/bench-v43 $O/runs-bench/bench-w41 && echo "  END and final picture v4.1's" || echo "  DIFFERENT from v4.1's"
same runs-bench/bench-v43 $FX/runs-bench/bench-v42 && echo "  END and final picture v4.2's" || echo "  DIFFERENT from v4.2's"
echo "==== 5. the install through the dialogs (gi18)"
countport runs/gi18-v43
echo "  v4.3: $(oc runs/gi18-v43); $(aud runs/gi18-v43)"
same runs/gi18-v43 $O/runs/gi18-w41 && echo "  END and final picture v4.1's" || echo "  DIFFERENT from v4.1's"
same runs/gi18-v43 $FX/runs/gi18-v42 && echo "  END and final picture v4.2's" || echo "  DIFFERENT from v4.2's"
echo "==== 6. the port's END line, read as a guest reads it at each run's end: found (magic 36383354, version 1.1) in $pp of $ppn runs"
grep -a "perfport END" runs/v43-n16-14M-768K/log.txt | cut -c1-400
echo "==== done $(date '+%F %T')"
