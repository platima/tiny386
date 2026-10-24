# p43.sh: the drawing's host time, one run at a time, two rounds: v4.3 (with the port, as the
# board builds it) and v4.2, both built in my copy of the v4.1 work's harness (h41: its build.sh
# taught perfport.c, nothing else), the same images and options as v4.2's p.sh
set -u
H=~/v43/merge/h41
W=~/v4-work/vbe/w95/img
D=~/flicker/fix/h/img
cd $H
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
for b in korvo1 dosboard; do
  [ -x bin/v42-$b/headless ] || BOARD=$b bash harness/build.sh ~/flicker/apply42/t-v4.2 esp bin/v42-$b 64 > bin/v42-$b.build.txt 2>&1
done
OPTS="-ips 2000000 -idle 16 -insns 1500 -vsecs 100000 -after-desktop 120 -dialogs 8 -snap 60"
run() {
  local name=$1 bin=$2 board=$3 img=$4 mem=$5 vga=$6; shift 6
  IMG=$img WALLMAX=1800 bash harness/run.sh bin/$bin-$board/headless runs/$name $mem $vga $OPTS "$@" > /dev/null 2>&1
  printf '%-24s %s | %s\n' $name "$(head -1 runs/$name/outcome.txt | cut -c10-)" "$(grep -o 'drawing: .*' runs/$name/log.txt)"
}
for round in 1 2; do
for bin in v43 v42; do
  run P$round-$bin-n16 $bin korvo1 $W/w16-800x480.img 14M 768K
  run P$round-$bin-n16-mouse $bin korvo1 $W/w16-800x480.img 14M 768K -mouse 50
  run P$round-$bin-n16dib-mouse $bin korvo1 $D/off16-dib.img 14M 768K -mouse 50
  run P$round-$bin-n32-mouse $bin korvo1 $W/w32-800x480.img 13M 1536K -mouse 50
  run P$round-$bin-dos16-mouse $bin dosboard $W/w16-800x600.img 13M 1M -mouse 50
  run P$round-$bin-vga $bin korvo1 $H/img/win95.pristine.img 14M 320K
  run P$round-$bin-vga-mouse $bin korvo1 $H/img/win95.pristine.img 14M 320K -mouse 50
done
done
endline() { grep -a "\] END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
s=0; n=0
for d in runs/P1-v43-*; do r=${d#runs/P1-v43-}; for k in 1 2; do n=$((n+1)); [ "$(endline runs/P$k-v43-$r)" = "$(endline runs/P$k-v42-$r)" ] && s=$((s+1)); done; done
echo "END lines: v4.3's the same as v4.2's in $s of $n pairs"
