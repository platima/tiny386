# mt43.sh: faults put one at a time into copies of perfport.c and vga.c (the v43 worktree's),
# each built into test_port_blocks (Korvo-1 header); a fault is caught when the test fails
set -u
T=~/v43/merge/tree
W=~/v43/merge/mt; rm -rf $W; mkdir -p $W
CFLAGS="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
gcc -O1 -g -w -c -o $W/vgafont.o $T/vgafont.c
hdr=$T/esp/main/board_korvo1.h
one() { # name file sed-expression
  local n=$1 f=$2 e=$3 d=$W/$1
  mkdir -p $d/tests/host; cp $T/*.c $T/*.h $d/ 2>/dev/null; cp -r $T/tests/host/shim $d/tests/host/; cp $T/tests/host/test_port_blocks.c $d/tests/host/
  mkdir -p $d/esp/main; cp $T/esp/main/board_korvo1.h $d/esp/main/
  if [ "$f" != none ]; then
    sed -i -e "$e" $d/$f
    cmp -s $T/$f $d/$f && { echo "== $n: the sed changed nothing"; return; }
  fi
  gcc -O2 -g -w -c -I$d/tests/host/shim -I$d -include $hdr -o $d/vbe.o $d/vga.c || { echo "== $n: vga.c did not build"; return; }
  gcc $CFLAGS -I$d/tests/host/shim -I$d -include $hdr -o $d/t $d/tests/host/test_port_blocks.c $d/vbe.o $W/vgafont.o 2> $d/build.txt || { echo "== $n: test did not build"; head $d/build.txt; return; }
  $d/t > $d/out.txt; local rc=$?
  echo "== $n ($f): exit $rc, $(tail -n 1 $d/out.txt)"
  grep "^FAIL" $d/out.txt | head -3
}
one unchanged none ''
one port-reads-a-constant perfport.c 's/PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, vga_dirty_blocks);/PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, 1);/'
one port-reads-the-first-value perfport.c 's/PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, vga_dirty_blocks);/static int pp_first_blocks = -1; if (pp_first_blocks < 0) pp_first_blocks = vga_dirty_blocks; PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, pp_first_blocks);/'
one set-does-not-store vga.c 's/    __atomic_store_n(&vga_dirty_blocks, on ? 1 : 0, __ATOMIC_RELAXED);/    (void) on;/'
one no-whole-after-change vga.c 's/            s->vbe_whole_left = 2;/            s->vbe_whole_left = 0;/'
one one-whole-after-change vga.c 's/            s->vbe_whole_left = 2;/            s->vbe_whole_left = 1;/'
one whole-not-counted vga.c 's/    s->n_vbe_whole += all;$/    (void) 0;/'
one change-not-counted vga.c 's/    __atomic_add_fetch(&vga_dirty_blocks_changes, 1, __ATOMIC_RELEASE);/    (void) 0;/'
