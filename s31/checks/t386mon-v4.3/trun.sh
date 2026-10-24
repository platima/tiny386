#!/bin/bash
# trun.sh NAME BIN IMG MEM VGA KEYS VSECS [headless options]: T386MON in Windows 95 on my copy of
# the T386MON harness (~/v43/merge/ph; ~/perfapp/pfix/run.sh with my paths).  FDA: a copy of the
# package's vbe-reg.img; FDB: a copy of the package's t386mon.img (T386MON 1.2; FDB=path for
# another).  The disk image is copied by the harness's run.sh; afterwards C:\T386MON.LOG is read
# off the copy, and the copy deleted.
set -uo pipefail
P=~/v43/merge; H=$P/ph
NAME=$1 BIN=$2 IMG=$3 MEM=$4 VGA=$5 KEYS=$6 VSECS=$7; shift 7
K=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf
FDA_SRC=${FDA:-$K/v3/card/tiny386/vbe-reg.img}
FDB_SRC=${FDB:-$K/t386mon/t386mon.img}
unset FDA FDB
cd $H
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
python3 -I $P/keys/mkkeys.py $P/keys/$KEYS.txt $P/keys/$KEYS.keys > /dev/null
rm -f fl/$NAME-a.img fl/$NAME-b.img
[ "$FDA_SRC" != none ] && { cp $FDA_SRC fl/$NAME-a.img; chmod 644 fl/$NAME-a.img; export FDA=$H/fl/$NAME-a.img; }
[ "$FDB_SRC" != none ] && { cp $FDB_SRC fl/$NAME-b.img; chmod 644 fl/$NAME-b.img; export FDB=$H/fl/$NAME-b.img; }
IMG=$IMG KEEP=1 WALLMAX=1800 bash harness/run.sh bin/$BIN/headless runs/$NAME $MEM $VGA \
  -ips 2000000 -idle 16 -insns 4000 -vsecs $VSECS -dialogs 8 -keyfile $P/keys/$KEYS.keys "$@" > /dev/null 2>&1
R=$H/runs/$NAME
echo "$NAME: $(head -1 $R/outcome.txt)"
grep -a 'taskbar\|\] END\|stopped:\|Ctrl+Alt+F11' $R/log.txt | cut -c1-260 | sed 's/^/   /'
if [ -f $R/win95.img ]; then
  OFF=$(python3 -I -c "import struct;d=open('$R/win95.img','rb').read(512);print(struct.unpack('<I',d[454:458])[0]*512)")
  MTOOLS_SKIP_CHECK=1 mcopy -n -o -i "$R/win95.img@@$OFF" ::/T386MON.LOG $R/T386MON.LOG 2>&1 | head -2
  rm -f $R/win95.img
fi
rm -f fl/$NAME-a.img fl/$NAME-b.img
echo "   pictures: $(ls $R | grep -c 'snap.*png')"
[ -f $R/T386MON.LOG ] && wc -l $R/T386MON.LOG
true
