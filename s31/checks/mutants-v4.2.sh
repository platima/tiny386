# mutants of v4.2's two vga.c changes against test_vbe / test_clear (korvo1)
set -u
T=~/flicker/fix/tree; H=$T/tests/host; O=~/flicker/fix/mt; rm -rf $O; mkdir -p $O
CF="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
ren() { for s in vga_init vga_set_force_8dm vga_step vga_refresh vga_ioport_write vga_ioport_read vbe_write vbe_read vga_mem_write vga_mem_read vga_mem_write16 vga_mem_write32 vga_mem_write_string vga_pci_init vga_planar_fast vga_set_text_ops vga_dirty_map vga_dirty vga_dirty_blocks vga_set_dirty_blocks; do printf ' -D%s=%s%s' $s $1 $s; done; }
hdr=$T/esp/main/board_korvo1.h
gcc -O1 -g -w -c -o $O/vgafont.o $T/vgafont.c
printf '#undef VGA_DIRTY_BLOCKS\n' > $O/nodb.h
gcc $CF -c -I$H/shim -I$T -include $hdr -o $O/tv.o $H/test_vbe.c
gcc $CF -c -I$H/shim -I$T -include $hdr -o $O/tc.o $H/test_clear.c
gcc -O2 -g -w -c -I$H/shim -I$T -include $hdr -include $O/nodb.h $(ren ref_) -o $O/vref.o $T/vga.c
gcc -O2 -g -w -c -I$H/shim -I$T -include $hdr -DVGA_NO_CLEAR_AROUND $(ren ref_) -o $O/cref.o $T/vga.c
run() { # name sed-expr
  mkdir -p $O/$1; cp $T/vga.h $O/$1/; sed -e "$2" $T/vga.c > $O/$1/vga.c
  if cmp -s $O/$1/vga.c $T/vga.c; then echo "$1: SED DID NOTHING"; return; fi
  gcc -O2 -g -w -c -I$O/$1 -I$H/shim -I$T -include $hdr -o $O/$1/n.o $O/$1/vga.c || { echo "$1: build failed"; return; }
  gcc -o $O/$1/tv $O/tv.o $O/$1/n.o $O/vref.o $O/vgafont.o -lpthread
  gcc -o $O/$1/tc $O/tc.o $O/$1/n.o $O/cref.o $O/vgafont.o
  $O/$1/tv > $O/$1/tv.log 2>&1; a=$?
  $O/$1/tc > $O/$1/tc.log 2>&1; b=$?
  echo "$1: test_vbe exit $a ($(grep -c FAIL $O/$1/tv.log) FAIL), test_clear exit $b ($(grep -c FAIL $O/$1/tc.log) FAIL)"
}
run m1-no-change-count 's/            s->vbe_whole_left = 2;/            s->vbe_whole_left = 0;/'
run m2-one-whole-picture 's/            s->vbe_whole_left = 2;/            s->vbe_whole_left = 1;/'
run m3-set-not-counted 's/    __atomic_add_fetch(&vga_dirty_blocks_changes, 1, __ATOMIC_RELEASE);//'
run m4-around-not-black 's/            d\[i\] = 0;/            d[i] = d[i];/'
run m5-around-none-at-end 's/^    vga_clear_around(s, 0, 0, 0, 0);$/    (void) 0;/'
run m6-text-no-full 's/^        full_update = 1;\n    }/XX/; /vga_clear_around(s, x1, y1, width1, height1);/{n;s/full_update = 1;/(void) 0;/}'
run m7-graphic-rect-wrong 's#vga_clear_around(s, (i0 % fb_dev->stride) / (BPP / 8), i0 / fb_dev->stride, w, h);#vga_clear_around(s, 0, 0, w, h);#'
run m8-shrunk-rect-wrong 's/    vga_clear_around(s, ox, oy, sw, sh);/    vga_clear_around(s, 0, 0, sw, sh);/'
run m9-no-flush-after 's/^    if (s->fb_all_dirty) {$/    if (0) {/'
run m10-clear-not-noted 's/^    s->clear_around = true;$/    (void) 0;/'
