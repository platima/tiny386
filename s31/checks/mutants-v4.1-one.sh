#!/bin/bash
# mut-one.sh TREE HOW NAME: the tests for one mutant (mutants-v4.1.py)
#   vbe     test_vbe (Korvo-1), the reference loop from the unchanged tree,
#           then BVBE480 in the harness
#   guest   BVBE480 in the harness (Korvo-1, 1M)
#   windows Windows 95 at 800x480x16 against the unchanged tree's run
set -u
T=$1; HOW=$2; NAME=$3
ORIG=$HOME/v4-work/v41/tree
H=$HOME/v4-work/v41/h
G=$HOME/v4-work/v41/guest
O=$HOME/v4-work/v41/mut/out-$NAME
rm -rf "$O"; mkdir -p "$O"
export PATH=$PATH:/sbin:/usr/sbin
caught=""
if [ "$HOW" = vbe ]; then
    hdr=$T/esp/main/board_korvo1.h
    S=$T/tests/host/shim
    ren=""
    for s in vga_init vga_set_force_8dm vga_step vga_refresh vga_ioport_write vga_ioport_read \
             vbe_write vbe_read vga_mem_write vga_mem_read vga_mem_write16 vga_mem_write32 \
             vga_mem_write_string vga_pci_init vga_planar_fast vga_set_text_ops vga_dirty_map vga_dirty vga_dirty_blocks; do
        ren="$ren -D$s=ref_$s"
    done
    printf '#undef VGA_DIRTY_BLOCKS\n' > "$O/nd.h"
    gcc -O1 -g -c -I"$S" -I"$T" -include "$hdr" -o "$O/t.o" "$T/tests/host/test_vbe.c" &&
    gcc -O2 -g -w -c -I"$S" -I"$T" -include "$hdr" -o "$O/new.o" "$T/vga.c" &&
    gcc -O2 -g -w -c -I"$S" -I"$ORIG" -include "$ORIG/esp/main/board_korvo1.h" -include "$O/nd.h" $ren \
        -o "$O/ref.o" "$ORIG/vga.c" &&
    gcc -O1 -g -w -c -o "$O/font.o" "$T/vgafont.c" &&
    gcc -o "$O/test_vbe" "$O/t.o" "$O/new.o" "$O/ref.o" "$O/font.o" -lpthread || { echo "build failed"; exit 2; }
    "$O/test_vbe" > "$O/test_vbe.txt" 2>&1
    st=$?
    if [ $st -ge 128 ]; then caught="test_vbe (it crashed, status $st)";
    elif [ $st != 0 ]; then caught="test_vbe ($(grep -c FAIL "$O/test_vbe.txt") checks)"; fi
    if [ -z "$caught" ]; then
        # the same under AddressSanitizer: a store out of bounds that no
        # picture shows
        gcc -O1 -g -fsanitize=address -fno-omit-frame-pointer -c -I"$S" -I"$T" -include "$hdr" -o "$O/ta.o" "$T/tests/host/test_vbe.c" &&
        gcc -O1 -g -w -fsanitize=address -fno-omit-frame-pointer -c -I"$S" -I"$T" -include "$hdr" -o "$O/newa.o" "$T/vga.c" &&
        gcc -fsanitize=address -o "$O/test_vbe_asan" "$O/ta.o" "$O/newa.o" "$O/ref.o" "$O/font.o" -lpthread &&
        ASAN_OPTIONS=detect_leaks=0 setarch -R "$O/test_vbe_asan" > "$O/test_vbe_asan.txt" 2>&1 ||
            caught="test_vbe under AddressSanitizer ($(grep -o 'ERROR: AddressSanitizer: [a-z-]*' "$O/test_vbe_asan.txt" | head -1))"
    fi
fi
if [ "$HOW" = vbe ] || [ "$HOW" = guest ]; then
    BOARD=korvo1 bash $H/harness/build.sh "$T" esp "$O/bin" 64 > "$O/build.txt" 2>&1 || { echo "harness build failed"; exit 2; }
    ( cd $H && IMG=$G/fd-vbe480.img NODISKCHECK=1 GEN=4 FPU=0 WALLMAX=600 \
        VGABIOS=$HOME/v4-work/v41/h/img/vgabios.bin \
        bash harness/run.sh "$O/bin/headless" "$O/run" 4M 1M -vsecs 3000 > /dev/null 2>&1 )
    python3 -I $G/vbe480check.py "$O/run" 800 480 > "$O/check.txt" 2>&1
    if [ $? != 0 ]; then caught="${caught:+$caught, }BVBE480 ($(tail -n 1 "$O/check.txt" | grep -o '[0-9]* WRONG'))"; fi
fi
if [ "$HOW" = windows ]; then
    BOARD=korvo1 bash $H/harness/build.sh "$T" esp "$O/bin" 64 > "$O/build.txt" 2>&1 || { echo "harness build failed"; exit 2; }
    ( cd $H && PCR_IGNORE_MODE=1 IMG=$HOME/v4-work/vbe/w95/img/w16-800x480.img \
        VGABIOS=$HOME/v4-work/v41/h/img/vgabios.bin WALLMAX=1800 \
        bash harness/run.sh "$O/bin/headless" "$O/run" 14M 768K -ips 2000000 -idle 16 -insns 1500 \
        -vsecs 100000 -after-desktop 120 -dialogs 8 -snap 60 > /dev/null 2>&1 )
    a=$(grep "END" "$O/run/log.txt" | sed 's/ wall=[0-9.]* s//')
    b=$(grep "END" $H/runs/w41-n16-14M-768K/log.txt | sed 's/ wall=[0-9.]* s//')
    if [ "$a" != "$b" ] || ! cmp -s "$O/run/final.png" $H/runs/w41-n16-14M-768K/final.png; then
        caught="Windows 95 at 800x480x16 ($(head -1 "$O/run/outcome.txt"), END $( [ "$a" = "$b" ] && echo same || echo different))"
    fi
fi
if [ -n "$caught" ]; then echo "caught by $caught"; else echo "NOT CAUGHT"; fi
