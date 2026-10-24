# t43.sh: T386MON 1.2 in Windows 95 on the v4.3 harness core (ph/bin/v43-*: tag v4.3 with the port,
# as the board builds it), and the same on the perfport branch's harness core (pp41: a copy of
# ~/perfapp/pfix/h/bin/pp-korvo1, branch perfport 6e9a6f1) with the same floppies, for an A/B;
# then PPTEST.COM under FreeDOS and in a Windows DOS box, as ~/perfapp/pfix/r4.sh.
#   640x480 in 16 colours (the standard VGA driver): the pristine image's read-only copy
#   800x480 in 16 bits: the test step's install, as released and with the one-byte pointer change
set -u
P=~/v43/merge; H=$P/ph
PR=~/perfapp/test/h/img/win95.pristine.img
W16=~/perfapp/test/w95/w16-800x480.img
PTR=~/perfapp/pfix/w95/w16-800x480-ptr.img
cd $H
mkdir -p bin/pp41-korvo1
cp ~/perfapp/pfix/h/bin/pp-korvo1/headless bin/pp41-korvo1/headless
cp ~/perfapp/pfix/h/bin/pp-korvo1/build.txt bin/pp41-korvo1/build.txt
cp $P/f1.txt $P/keys/f1.txt
sha256sum $PR $W16 $PTR /mnt/d/Repos/esp32-s31-linux-release/tiny386/win95/win95.img | cut -c1-16,66-
rm -rf runs/t43-* runs/t41p-* runs/pp43-* runs/pp41-*
T=$P/trun.sh
bash $T t43-vga v43-korvo1 $PR 14M 320K m1 650 -snap 5 -beat 1 > $P/to1.out 2>&1 &
bash $T t41p-vga pp41-korvo1 $PR 14M 320K m1 650 -snap 5 -beat 1 > $P/to2.out 2>&1 &
bash $T t43-n16 v43-korvo1 $W16 14M 768K m1 650 -snap 5 -beat 1 > $P/to3.out 2>&1 &
bash $T t41p-n16 pp41-korvo1 $W16 14M 768K m1 650 -snap 5 -beat 1 > $P/to4.out 2>&1 &
bash $T t43-p16 v43-korvo1 $PTR 14M 768K m1 650 -snap 5 -beat 1 > $P/to5.out 2>&1 &
bash $T t41p-p16 pp41-korvo1 $PTR 14M 768K m1 650 -snap 5 -beat 1 > $P/to6.out 2>&1 &
bash $T t43-p16b v43-korvo1 $PTR 14M 768K m2 470 -snap 10 > $P/to7.out 2>&1 &
bash $T t41p-p16b pp41-korvo1 $PTR 14M 768K m2 470 -snap 10 > $P/to8.out 2>&1 &
bash $T t43-vgab v43-korvo1 $PR 14M 320K m2 470 -snap 10 > $P/to9.out 2>&1 &
bash $T t43-f11 v43-korvo1 $PTR 14M 768K f1 600 -snap 5 -f11 400,430,460 > $P/to10.out 2>&1 &
bash $T t43-f11vga v43-korvo1 $PR 14M 320K f1 600 -snap 5 -f11 400,430,460 > $P/to11.out 2>&1 &
wait
# PPTEST under FreeDOS, and in a Windows 95 DOS box at 800x480x16, as ~/perfapp/pfix/r4.sh
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
for b in v43 pp41; do
  IMG=~/perfapp/port/guest/fd-pptest.img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 bash harness/run.sh bin/$b-korvo1/headless runs/pp-dos-$b 4M 256K -vsecs 300 > /dev/null 2>&1 &
  mkdir -p fl; cp ~/perfapp/port/guest/pp-floppy.img fl/ppw-$b-a.img; cp ~/perfapp/port/guest/pp-floppy.img fl/ppw-$b-b.img; chmod 644 fl/ppw-$b-?.img
  FDA=$H/fl/ppw-$b-a.img FDB=$H/fl/ppw-$b-b.img IMG=$W16 WALLMAX=1800 bash harness/run.sh bin/$b-korvo1/headless runs/ppw-n16-$b 14M 768K \
    -ips 2000000 -idle 16 -insns 1500 -vsecs 700 -dialogs 8 -snap 60 -keyfile ~/perfapp/port/guest/dosbox.keys > /dev/null 2>&1 &
done
wait
rm -f runs/pp-dos-*/win95.img runs/ppw-n16-*/win95.img fl/ppw-*.img
for i in 1 2 3 4 5 6 7 8 9 10 11; do cat $P/to$i.out; done
for b in v43 pp41; do
  python3 -I /mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/t386mon/port-test/ppcheck.py runs/pp-dos-$b text
  python3 -I /mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/t386mon/port-test/ppcheck.py runs/ppw-n16-$b vbe
done
echo "t43 done"
