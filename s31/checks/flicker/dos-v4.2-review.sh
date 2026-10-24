# MODE13X and BMODES under FreeDOS in my harness copy (-board), v4.1, v4.2 and two planted
# faults in v4.2's vga.c that make black inside the picture (the new count must see them)
set -u
export MTOOLS_SKIP_CHECK=1 PATH=$PATH:/sbin:/usr/sbin
A=~/flicker/apply42; H=$A/h; M=$A/m13
cd $A
# the planted faults, in copies of the v4.2 export
rm -rf t-m1 t-m2; cp -r t-v4.2 t-m1; cp -r t-v4.2 t-m2
# m1: every row made black whole, the picture's rows too
python3 -I - t-m1/vga.c <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
o="        if (r < y || r >= y + h) {\n            vga_black(d, fw);"
assert s.count(o)==1; s=s.replace(o,"        if (1) {\n            vga_black(d, fw);"); open(p,"w").write(s)
PY
# m2: the black to the picture's right starts 8 pixels inside it
python3 -I - t-m2/vga.c <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
o="            vga_black(d + x + w, fw - x - w);"
assert s.count(o)==1; s=s.replace(o,"            vga_black(d + x + w - (w > 8 ? 8 : 0), fw - x - w + (w > 8 ? 8 : 0));"); open(p,"w").write(s)
PY
diff -r t-v4.2 t-m1; diff -r t-v4.2 t-m2
cd $H
for m in m1 m2; do BOARD=korvo1 bash harness/build.sh $A/t-$m esp bin/r$m 64 > bin/r$m.build.txt 2>&1 && echo "built r$m" || { tail bin/r$m.build.txt; exit 1; }; done
one() { # name bin img vga
  local n=$1 b=$2 img=$3 vga=$4; shift 4
  IMG=$M/$img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 bash harness/run.sh bin/$b/headless runs/$n 4M $vga \
    -ips 2000000 -idle 16 -vsecs 400 -board "$@" > /dev/null 2>&1
}
for b in r41 r42 rm1 rm2; do
  one dos-m13x-$b-256K $b fd-mode13x.img 256K &
  one dos-m13x-$b-768K $b fd-mode13x.img 768K &
  one dos-bmodes-$b-256K $b fd-bmodes.img 256K &
done
wait
endline() { grep -a "END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
for r in m13x-%-256K m13x-%-768K bmodes-%-256K; do
  a=runs/dos-${r/\%/r41}; echo "== ${r/-%/}"
  for b in r42 rm1 rm2; do
    c=runs/dos-${r/\%/$b}
    e=same; [ "$(endline $a)" = "$(endline $c)" ] || e=DIFFERENT
    s=0; d=0; for f in $a/shot-*.png; do s=$((s+1)); cmp -s $f $c/$(basename $f) || d=$((d+1)); done
    echo "   $b against r41: END lines $e; $s shots, $d differ"
  done
  for b in r41 r42 rm1 rm2; do
    c=runs/dos-${r/\%/$b}
    echo "   $b: $(grep -a 'board: [0-9]* pictures' $c/log.txt | sed 's/.*board: //' | cut -c1-110)"
    echo "       $(grep -a 'board: clears' $c/log.txt | sed 's/.*board: //')"
    echo "       $(grep -a 'board: black between' $c/log.txt | sed 's/.*board: //')"
  done
done
# the shots of the original runs, for comparison with the fix agent's
for r in m13x-v42-256K m13x-v42-768K bmodes-v42-256K; do
  o=~/flicker/fix/h/runs/dos-$r; n=runs/dos-${r/v42/r42}
  [ -d $o ] || continue
  e=same; [ "$(endline $o)" = "$(endline $n)" ] || e=DIFFERENT
  echo "fix agent's dos-$r against mine: END $e"
done
