# asan43.sh: the sanitizer runs of v4.2 (apply42's tests.sh part 3, ~/flicker/fix/asan.sh) on my
# export of tag v4.3, with vga_get_info among the renamed symbols; and the port's three tests
# (test_perfport, test_vga_info, test_port_blocks) under AddressSanitizer and UBSan too
set -u
A=~/v43/merge
TT=$A/t-v4.3
O=$A/asan; rm -rf $O; mkdir -p $O
S=$TT/tests/host/shim
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
CF="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
ren=""
for s in vga_init vga_set_force_8dm vga_step vga_refresh vga_ioport_write vga_ioport_read vbe_write vbe_read vga_mem_write vga_mem_read vga_mem_write16 vga_mem_write32 vga_mem_write_string vga_pci_init vga_planar_fast vga_set_text_ops vga_dirty_map vga_dirty vga_dirty_blocks vga_set_dirty_blocks vga_get_info; do ren="$ren -D$s=ref_$s"; done
printf '#undef VGA_DIRTY_BLOCKS\n' > $O/nd.h
echo "== start $(date '+%F %T'), tree $(cd $TT && cat esp/version.txt)"
gcc -O1 -g -w -c -o $O/font.o $TT/vgafont.c
gcc -O1 -g -w $SAN -c -o $O/font-san.o $TT/vgafont.c
for b in korvo1 dosboard; do
  hdr=$TT/esp/main/board_$b.h
  gcc -O1 -g $SAN -c -I$S -I$TT -include $hdr -o $O/t-$b.o $TT/tests/host/test_vbe.c
  gcc -O1 -g -w $SAN -c -I$S -I$TT -include $hdr -o $O/new-$b.o $TT/vga.c
  gcc -O1 -g -w $SAN -c -I$S -I$TT -include $hdr -include $O/nd.h $ren -o $O/ref-$b.o $TT/vga.c
  gcc $SAN -o $O/test_vbe-$b $O/t-$b.o $O/new-$b.o $O/ref-$b.o $O/font.o -lpthread
  ASAN_OPTIONS=detect_leaks=0 timeout 1800 setarch -R $O/test_vbe-$b > $O/run-$b.txt 2>&1
  echo "test_vbe $b: exit $?: $(tail -n 1 $O/run-$b.txt); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' $O/run-$b.txt)"
  gcc -O1 -g $SAN -c -I$S -I$TT -include $hdr -o $O/tc-$b.o $TT/tests/host/test_clear.c
  gcc -O1 -g -w $SAN -c -I$S -I$TT -include $hdr -DVGA_NO_CLEAR_AROUND $ren -o $O/cref-$b.o $TT/vga.c
  gcc $SAN -o $O/test_clear-$b $O/tc-$b.o $O/new-$b.o $O/cref-$b.o $O/font.o
  ASAN_OPTIONS=detect_leaks=0:handle_segv=0:allow_user_segv_handler=1 timeout 1800 setarch -R $O/test_clear-$b > $O/runc-$b.txt 2>&1
  echo "test_clear $b: exit $?: $(tail -n 1 $O/runc-$b.txt); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' $O/runc-$b.txt)"
  gcc $CF $SAN -I$S -I$TT -include $hdr -o $O/test_vga_info-$b $TT/tests/host/test_vga_info.c $O/new-$b.o $O/font.o
  ASAN_OPTIONS=detect_leaks=0 timeout 600 setarch -R $O/test_vga_info-$b > $O/runi-$b.txt 2>&1
  echo "test_vga_info $b: exit $?: $(tail -n 1 $O/runi-$b.txt); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' $O/runi-$b.txt)"
  gcc $CF $SAN -I$S -I$TT -include $hdr -o $O/test_port_blocks-$b $TT/tests/host/test_port_blocks.c $O/new-$b.o $O/font.o
  ASAN_OPTIONS=detect_leaks=0 timeout 600 setarch -R $O/test_port_blocks-$b > $O/runp-$b.txt 2>&1
  echo "test_port_blocks $b: exit $?: $(tail -n 1 $O/runp-$b.txt); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' $O/runp-$b.txt)"
done
NPPI=$(grep -c '^#define PP_I_' $TT/perfport.h)
gcc $CF $SAN -I$TT -o $O/test_perfport $TT/tests/host/test_perfport.c -lpthread
ASAN_OPTIONS=detect_leaks=0 timeout 600 setarch -R $O/test_perfport $NPPI > $O/runpp.txt 2>&1
echo "test_perfport: exit $?: $(tail -n 1 $O/runpp.txt); sanitizer reports: $(grep -c 'runtime error\|AddressSanitizer' $O/runpp.txt)"
echo "== asan done $(date '+%F %T')"
gcc -O1 -g -fsanitize=thread -c -I$S -I$TT -include $TT/esp/main/board_korvo1.h -o $O/tt.o $TT/tests/host/test_vbe.c 2> $O/tsan-build.txt
gcc -O1 -g -w -fsanitize=thread -c -I$S -I$TT -include $TT/esp/main/board_korvo1.h -o $O/tn.o $TT/vga.c
gcc -O1 -g -w -fsanitize=thread -c -I$S -I$TT -include $TT/esp/main/board_korvo1.h -include $O/nd.h $ren -o $O/tr.o $TT/vga.c
gcc -fsanitize=thread -o $O/test_vbe-tsan $O/tt.o $O/tn.o $O/tr.o $O/font.o -lpthread
s=$(date +%s)
timeout 7200 setarch -R $O/test_vbe-tsan > $O/run-tsan.txt 2>&1
echo "tsan: exit $?: $(tail -n 1 $O/run-tsan.txt); reports: $(grep -c 'WARNING: ThreadSanitizer' $O/run-tsan.txt); $(( $(date +%s) - s )) s"
grep "^SUMMARY" $O/run-tsan.txt | sed "s#$TT/##" | sort | uniq -c
echo "== all done $(date '+%F %T')"
