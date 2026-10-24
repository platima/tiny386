# copies of the offline-installed images with QEMUMINI.DRV's pointer byte changed (WSL only)
set -eu
export MTOOLS_SKIP_CHECK=1 PATH=$PATH:/sbin:/usr/sbin
F=~/flicker/fix; I=$F/h/img; O=$((63*512))
ls -la ~/v4-work/vbe/w95/img/ ~/v4-work/v41/w95/img/ 2>&1 | grep img
mk() { # src name
  [ -f $I/$2 ] && return 0
  cp $1 $I/$2; chmod 644 $I/$2
  h=$(mcopy -n -i $I/$2@@$O ::/WINDOWS/SYSTEM/QEMUMINI.DRV - | sha256sum | cut -c1-16)
  mcopy -o -i $I/$2@@$O $F/drv/dib.drv ::/WINDOWS/SYSTEM/QEMUMINI.DRV
  n=$(mcopy -n -i $I/$2@@$O ::/WINDOWS/SYSTEM/QEMUMINI.DRV - | sha256sum | cut -c1-16)
  chmod 444 $I/$2
  echo "$2: QEMUMINI.DRV $h -> $n"
}
W=~/v4-work/vbe/w95/img
mk $W/w8-800x480.img off8-dib.img
mk $W/w16-800x480.img off16-dib.img
mk $W/w24-800x480.img off24-dib.img
mk $W/w32-800x480.img off32-dib.img
mk $W/w16-800x600.img dos16-dib.img
mk ~/flicker/repro/h/img/off-v640x16.img v640x16-dib.img
mk ~/flicker/repro/h/img/base-n16.img n16-dib.img
