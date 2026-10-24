# bvab.sh: why BVBE480's END lines on my T386MON harness copy (ph) differ from v4.2's: an A/B
#  (a) v4.2 (apply42's export of the tag) built in ph: BVBE480's 21 runs, END against v4.3's in ph
#  (b) v4.3 built in a copy of the v4.1 work's harness (h41: its build.sh taught perfport.c, nothing
#      else changed): the 21 runs, END against apply42's v4.2 runs (that harness unchanged)
set -u
P=~/v43/merge; W41=~/v4-work/v41; FX=~/flicker/apply42/h41
export PATH=$PATH:/sbin:/usr/sbin
# (b)'s harness
mkdir -p $P/h41/bin $P/h41/runs $P/h41/img $P/h41/lists
rm -rf $P/h41/harness; cp -r $W41/h/harness $P/h41/harness
cp $P/ph/harness/build.sh $P/h41/harness/build.sh
diff $W41/h/harness/build.sh $P/h41/harness/build.sh > $P/h41/harness-v43.diff
cat $P/h41/harness-v43.diff
diff -rq $W41/h/harness $P/h41/harness
ln -sf $W41/h/img/bios.bin $P/h41/img/bios.bin
ln -sf $W41/h/img/vgabios.bin $P/h41/img/vgabios.bin
ln -sf $W41/h/img/win95.pristine.img $P/h41/img/win95.pristine.img
cd $P/h41
for b in korvo1 dosboard; do BOARD=$b bash harness/build.sh $P/t-v4.3 esp bin/v43-$b 64 > bin/v43-$b.build.txt 2>&1 && echo "h41: built v43-$b" || { tail bin/v43-$b.build.txt; exit 1; }; done
grep -c TINY386_PERFPORT bin/v43-korvo1/commands.txt
cd $P/ph
for b in korvo1 dosboard; do BOARD=$b bash harness/build.sh ~/flicker/apply42/t-v4.2 esp bin/v42-$b 64 > bin/v42-$b.build.txt 2>&1 && echo "ph: built v42-$b" || { tail bin/v42-$b.build.txt; exit 1; }; done
grep -c TINY386_PERFPORT bin/v42-korvo1/commands.txt
bv() { local HH=$1 B=$2 board=$3 vga=$4; shift 4; local extra="$*"; local tag=$(echo "$extra" | tr -d ' -')
  local name=vbe480-$B-$board-$vga${tag:+-$tag}
  cd $HH
  IMG=$W41/guest/fd-vbe480.img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 \
    bash harness/run.sh bin/$B-$board/headless runs/$name 4M $vga -vsecs 5000 $extra > /dev/null 2>&1
  local sh=480; [ $board = dosboard ] && sh=600
  python3 -I $W41/guest/vbe480check.py runs/$name 800 $sh > runs/$name/check.txt 2>&1
  echo "$(tail -n 1 runs/$name/check.txt)   [$(grep -o 'stopped: [a-z-]*' runs/$name/log.txt)] $HH $name"; }
export -f bv; export W41
sed "s#^v43 #$P/ph v42 #" $P/ph/lists/bv-v43.txt > $P/ph/lists/bv-v42.txt
sed "s#^v43 #$P/h41 v43 #" $P/ph/lists/bv-v43.txt > $P/h41/lists/bv-v43.txt
head -2 $P/ph/lists/bv-v42.txt $P/h41/lists/bv-v43.txt
cat $P/ph/lists/bv-v42.txt $P/h41/lists/bv-v43.txt | xargs -P 14 -L 1 bash -c 'bv "$@"' _ | sort > $P/bvab.out
grep -vc "every one right" $P/bvab.out
endline() { grep -a "\] END" $1/log.txt | sed 's/ wall=[0-9.]* s//'; }
a=0; b=0; n=0
for d in $P/ph/runs/vbe480-v43-*; do r=${d#$P/ph/runs/vbe480-v43-}; n=$((n+1))
  [ "$(endline $d)" = "$(endline $P/ph/runs/vbe480-v42-$r)" ] && a=$((a+1)) || echo "  (a) DIFFERS: $r"
  [ "$(endline $P/h41/runs/vbe480-v43-$r)" = "$(endline $FX/runs/vbe480-v42-$r)" ] && b=$((b+1)) || echo "  (b) DIFFERS: $r"
done
echo "(a) the T386MON harness copy: v4.3's END line v4.2's in $a of $n runs"
echo "(b) the v4.1 work's harness: v4.3's END line apply42's v4.2 run's in $b of $n runs"
echo "every picture right: $(grep -c 'every one right' $P/bvab.out) of $(wc -l < $P/bvab.out) runs"
