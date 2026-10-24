# t43chk.sh: the T386MON and PPTEST runs of t43.sh, checked
#  1. v4.3 against the perfport branch's core (pp41), the same images, floppies and keys: the END
#     line, the final picture, every picture of the run, and T386MON's log
#  2. v4.3 against the T386MON step's own runs of the perfport branch (~/perfapp/pfix/h/runs, their
#     floppy's README.TXT one line older): the END line and the log
#  3. the port's CHANGED against the harness's own count of pictures that sent a row; PICTURES
#     against the harness's pictures
#  4. Ctrl+Alt+F11 (-f11): the harness's port lines at each change, and T386MON's Log now blocks
#  5. the tests' summaries
set -u
P=~/v43/merge; H=$P/ph; R=$H/runs; F=~/perfapp/pfix/h/runs
endline() { grep -a "\] END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
echo "==== 1. v4.3 against the perfport branch's core, the same inputs"
for r in vga n16 p16 p16b; do
  a=$R/t43-$r; b=$R/t41p-$r
  e=DIFFERENT; [ "$(endline $a)" = "$(endline $b)" ] && e=same
  fp=DIFFERENT; cmp -s $a/final.png $b/final.png && fp=same
  n=0; d=0; for f in $a/snap*.png; do n=$((n+1)); cmp -s $f $b/$(basename $f) || d=$((d+1)); done
  lg=DIFFERENT; cmp -s $a/T386MON.LOG $b/T386MON.LOG && lg="the same bytes"
  echo "  t43-$r: END $e; final picture $fp; $n pictures, $d differ; T386MON.LOG $lg ($(wc -l < $a/T386MON.LOG) lines)"
done
for r in pp-dos ppw-n16; do
  a=$R/$r-v43; b=$R/$r-pp41
  e=DIFFERENT; [ "$(endline $a)" = "$(endline $b)" ] && e=same
  pe=DIFFERENT; [ "$(grep -a 'perfport END' $a/log.txt | sed 's/ io [0-9]* snap [0-9]* us//; s/ blocks_on [0-9]*//')" = "$(grep -a 'perfport END' $b/log.txt | sed 's/ io [0-9]* snap [0-9]* us//; s/ blocks_on [0-9]*//')" ] && pe=same
  echo "  $r: END $e; the port's END line $pe (the harness's own port line has blocks_on in it on v4.3: compared without)"
done
echo "==== 2. v4.3 against the T386MON step's own runs (perfport 6e9a6f1)"
for p in "t43-vga a-vga" "t43-n16 a-n16" "t43-p16b p-n16" "pp-dos-v43 pp-dos" "ppw-n16-v43 ppw-n16"; do
  set -- $p; a=$R/$1; b=$F/$2
  e=DIFFERENT; [ "$(endline $a)" = "$(endline $b)" ] && e=same
  lg=-; [ -f $a/T386MON.LOG ] && { lg=DIFFERENT; cmp -s $a/T386MON.LOG $b/T386MON.LOG && lg="the same bytes"; }
  echo "  $1 against $2: END $e; T386MON.LOG $lg"
done
echo "==== 3. the port's counts against the harness's own"
for d in $R/t43-* $R/t41p-* $R/pp-dos-* $R/ppw-n16-*; do r=$(basename $d)
  c=$(grep -a "perfport END" $d/log.txt | grep -o "changed [0-9]*" | cut -d' ' -f2)
  pc=$(grep -a "perfport END" $d/log.txt | grep -o "pictures [0-9]*" | cut -d' ' -f2)
  l=$(grep -a "rows changed a picture" $d/log.txt)
  p=$(echo "$l" | grep -o "picture: [0-9]* pictures" | grep -o "[0-9]*"); n=$(echo "$l" | grep -o "none [0-9]*" | cut -d' ' -f2)
  printf '  %-14s CHANGED %6s, harness %6s - %6s = %6s %-4s  PICTURES %6s, harness %6s %s\n' $r $c $p $n $((p - n)) \
    $([ "$c" = $((p - n)) ] && echo same || echo DIFF) $pc $p $([ "$pc" = "$p" ] && echo same || echo DIFF)
done
echo "==== 4. Ctrl+Alt+F11"
for r in t43-f11 t43-f11vga; do
  echo "-- $r: the harness's lines"
  grep -a "Ctrl+Alt+F11\|perfport f11\|perfport END" $R/$r/log.txt | sed 's/ disk r .*//' | cut -c1-250
  echo "-- $r: T386MON's log, the Log now blocks' VBE drawing and Mode lines, and the lines every 5 s"
  tr -d '\r' < $R/$r/T386MON.LOG | grep -a "Log now\|VBE drawing\|#   Mode\|^# T386MON\|information port" | head -30
done
echo "==== 5. the tests"
for r in t43-vga t43-n16 t43-p16 t43-p16b t43-vgab; do
  echo "-- $r: $(tr -d '\r' < $R/$r/T386MON.LOG | grep -a '^# summary' | head -1)"
  tr -d '\r' < $R/$r/T386MON.LOG | grep -a '^# tests at\|^# T386MON 1.2 started\|information port' | head -3
done
