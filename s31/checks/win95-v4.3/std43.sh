# std43.sh: the standard Windows 95 runs of v4.2 (apply42's std42.sh, from the v4.1 work's lists) on
# my T386MON harness copy (ph: the v4.1 work's harness with the port's host part, my -f11 unused),
# with v4.3 built with the port as the board builds it: the 27 standard configurations, the 11
# native ones, BVBE480, BENCH and the install through the dialogs.  Compared with apply42's v4.2
# runs (~/flicker/apply42/h41) and v4.1's own (~/v4-work/v41/h): the END line (the emulator's, not
# the port's) and the final picture.  The port's END line is read as a guest reads it.
set -u
P=~/v43/merge; H=$P/ph; W41=~/v4-work/v41; FX=~/flicker/apply42/h41
export PATH=$PATH:/sbin:/usr/sbin
cd $H
mkdir -p lists gi runs runs-bench
ln -sf $W41/h/img/win95.pristine.img img/win95.pristine.img
sha256sum img/win95.pristine.img img/bios.bin img/vgabios.bin $W41/h/img/bios.bin $W41/h/img/vgabios.bin | cut -c1-16,66-
diff -q $W41/h/harness/matrix.sh harness/matrix.sh && diff -q $W41/h/harness/run.sh harness/run.sh && echo "matrix.sh and run.sh: the v4.1 work's"
sed 's#/home/owner/v4-work/v41/h/bin/w41-korvo1/headless#'$H'/bin/v43-korvo1/headless#; s#S-w41-#S-v43-#' $W41/h/lists/std-w41.lst > lists/std-v43.lst
grep -c "v43-korvo1" lists/std-v43.lst
grep "^w41" $W41/h/lists/nat.lst | sed 's/^w41-/v43-/; s/ w41 / v43 /' > lists/nat-v43.lst
start=$(date +%s)
rm -rf runs-std-v43
bash harness/matrix.sh lists/std-v43.lst runs-std-v43 12 > /dev/null 2>&1
echo "std27 done, $(( $(date +%s) - start )) s"
nat() { local name=$1 bin=$2 board=$3 img=$4 mem=$5 vga=$6; shift 6
  PCR_IGNORE_MODE=1 IMG=$HOME/v4-work/vbe/w95/img/$img.img WALLMAX=1800 bash harness/run.sh bin/$bin-$board/headless runs/$name $mem $vga \
    -ips 2000000 -idle 16 -insns 1500 -vsecs 100000 -after-desktop 120 -dialogs 8 -snap 60 "$@" > /dev/null 2>&1; }
while read -r l; do nat $l & while [ "$(jobs -rp | wc -l)" -ge 12 ]; do wait -n; done; done < lists/nat-v43.lst
wait
echo "nat done, $(( $(date +%s) - start )) s"
bv() { local B=$1 board=$2 vga=$3; shift 3; local extra="$*"; local tag=$(echo "$extra" | tr -d ' -')
  local name=vbe480-$B-$board-$vga${tag:+-$tag}
  IMG=$W41/guest/fd-vbe480.img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 \
    bash harness/run.sh bin/$B-$board/headless runs/$name 4M $vga -vsecs 5000 $extra > /dev/null 2>&1
  local sh=480; [ $board = dosboard ] && sh=600
  python3 -I $W41/guest/vbe480check.py runs/$name 800 $sh > runs/$name/check.txt 2>&1
  echo "$(tail -n 1 runs/$name/check.txt)   [$(grep -o 'stopped: [a-z-]*' runs/$name/log.txt)] $name"; }
export -f bv; export W41
sed 's/^w41 /v43 /' $W41/bv-list.txt | grep "^v43" > lists/bv-v43.txt
xargs -P 12 -L 1 bash -c 'bv "$@"' _ < lists/bv-v43.txt | sort -k 9 > $P/bv-v43.out
echo "bvbe480 done, $(( $(date +%s) - start )) s"
B=~/v4-work/merge/pc/img-bench/freedos-bench.img
IMG=$B GEN=4 FPU=0 NODISKCHECK=1 bash harness/run.sh bin/v43-korvo1/headless runs-bench/bench-v43 4M 256K -ips 2000000 -idle 16 -insns 250 -vsecs 100000 -snap 1000 -marks > /dev/null 2>&1 &
cp ~/v4-work/vbe/dl-vmdisp/vmdisp9x-1.2025.0.119b-driver-2d.img gi/vmd-gi18-v43.img; chmod 644 gi/vmd-gi18-v43.img
cp /mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/card/tiny386/vbe-reg.img gi/reg-gi18-v43.img; chmod 644 gi/reg-gi18-v43.img
PCR_IGNORE_MODE=1 FDA=$H/gi/reg-gi18-v43.img FDB=$H/gi/vmd-gi18-v43.img WALLMAX=1800 \
  bash harness/run.sh bin/v43-korvo1/headless runs/gi18-v43 14M 768K -ips 2000000 -idle 16 -vsecs 1080 -snap 10 -keyfile $W41/gi/gi18.keys -audit 1 > /dev/null 2>&1 &
wait
echo "bench and install done, $(( $(date +%s) - start )) s"
bash $P/cmp43.sh > $P/cmp43.out 2>&1
cat $P/cmp43.out
