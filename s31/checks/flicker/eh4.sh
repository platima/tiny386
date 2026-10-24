# the share of the guest's time in pointer code by segment of the session: typing, scroll, move (mouse still)
set -u
F=~/flicker/fix; H=$F/h; cd $H
export PATH=$PATH:/sbin:/usr/sbin PCR_IGNORE_MODE=1
one() { local n=$1 img=$2 eh=$3; shift 3
  PCR_EIPHIST=$eh IMG=$img WALLMAX=1800 bash harness/run.sh bin/v42e-korvo1/headless runs/$n 14M 768K \
    -ips 2000000 -idle 16 -vsecs 1000 -after-desktop 205 -dialogs 1 -session-after 10 -skeys $F/sess/plat.keys -smouse $F/sess/plat.mouse "$@" > /dev/null 2>&1
  t=$(grep -a "eiphist: [0-9]* guest" runs/$n/log.txt | awk '{print $3}')
  p=$(grep -a "eiphist: cs" runs/$n/log.txt | awk -v T=$t '/cs 0028 eip c0fd7|cs 0028 eip c0fd8|cs 032f eip 0000/ {s+=$7} END {printf "%.1f", 100*s/T}')
  echo "$n ($eh): pointer code $p % of $t instructions"
}
# segments (session s): typing 110-134, scroll 135-154, move 155-174; desktop+10 = 82 (dib), 84 (as released)
one e-t-dib $H/img/n16-dib.img 192,216 &
one e-t-n16 ~/flicker/repro/h/img/base-n16.img 194,218 &
one e-s-dib $H/img/n16-dib.img 217,236 &
one e-s-n16 ~/flicker/repro/h/img/base-n16.img 219,238 &
one e-m-dib $H/img/n16-dib.img 237,256 &
one e-m-n16 ~/flicker/repro/h/img/base-n16.img 239,258 &
wait
