# setup43.sh [COMMIT]: my own copies of the harnesses (the earlier steps' folders are only read),
# the tree exported, and the harness builds of v4.3
#   fh: the flicker harness (~/flicker/apply42/h/harness: the board's display model, the count of
#       black), its build.sh taught to compile perfport.c with TINY386_PERFPORT as the board does
#   ph: the T386MON harness (~/perfapp/pfix/h/harness: the v4.1 work's harness with the port's
#       host part), with -f11 (Ctrl+Alt+F11 as speed.c takes it) and vbe_blocks in its port line
set -eu
A=~/v43/merge
T=~/tiny386-idf-scratch/v3/tiny386
K=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf
C=${1:-v4.3}
cd $A
rm -rf t-v4.3; mkdir t-v4.3
git -C $T archive $C | tar -x -C t-v4.3
echo "t-v4.3: $(git -C $T rev-parse --short $C^{commit}), tree $(git -C $T rev-parse $C^{tree} | cut -c1-8); version $(cat t-v4.3/esp/version.txt)"
# ---- fh
mkdir -p fh/bin fh/runs fh/img sess m13 fd
rm -rf fh/harness; cp -r ~/flicker/apply42/h/harness fh/harness
python3 -I - fh/harness/build.sh <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = r'''    [ -f "$TREE/esp/main/kbd_input.c" ] && V3DEFS="-DPS2_KBD_HOOKS -DNO_WIFI"
'''
new = old + r'''    # v4.3 (perfport.c in the tree): main/CMakeLists.txt adds perfport.c and
    # TINY386_PERFPORT, so pc_vga_step() draws through perfport_redraw() as on
    # the board (the port's host part is not filled here; NO_PERFPORT=1 leaves
    # the port out)
    if [ -f "$TREE/perfport.c" ] && [ -z "${NO_PERFPORT:-}" ]; then
        SRCS="$SRCS perfport.c"
        V3DEFS="$V3DEFS -DTINY386_PERFPORT"
    fi
'''
assert s.count(old) == 1
s = s.replace(old, new)
open(p, "w").write(s)
PY
diff ~/flicker/apply42/h/harness/build.sh fh/harness/build.sh > fh/harness-v43.diff || true
cat fh/harness-v43.diff
for f in bios.bin vgabios.bin vgabios-v3.9.bin; do cp ~/flicker/apply42/h/img/$f fh/img/$f; done
cp ~/flicker/apply42/sess/* sess/
rm -f m13/fd-mode13x.img m13/fd-bmodes.img
cp ~/flicker/apply42/m13/fd-mode13x.img ~/flicker/apply42/m13/fd-bmodes.img m13/
sha256sum fh/img/*.bin $K/v3/card/tiny386/bios.bin $K/v3/card/tiny386/vgabios.bin | cut -c1-16,65-
# ---- ph
mkdir -p ph/bin ph/runs ph/img ph/fl keys
rm -rf ph/harness; cp -r ~/perfapp/pfix/h/harness ph/harness
python3 -I - ph/harness/headless.c <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
rep(r'''static double opt_perfport = 0;         /* perfport: a line of the port's values every S virtual s */
''', r'''static double opt_perfport = 0;         /* perfport: a line of the port's values every S virtual s */
/* v4.3: -f11 T,T,...: Ctrl+Alt+F11 at these virtual seconds, as the board's speed.c takes it
   (vga_set_dirty_blocks(!vga_dirty_blocks), between two pictures), each followed by a
   perfport line read as a guest reads the port */
static double opt_f11[16];
static int n_f11;
extern void vga_set_dirty_blocks(int on) __attribute__((weak));
''')
rep(r'''"flushes %u rows %u vbe %u whole %u blocks %u rows %u mode %u %ux%ux%u "''',
    r'''"flushes %u rows %u vbe %u whole %u blocks %u rows %u blocks_on %u mode %u %ux%ux%u "''')
rep(r'''v[PP_I_VBE_ROWS], v[PP_I_MODE_KIND],''', r'''v[PP_I_VBE_ROWS], v[PP_I_VBE_BLOCKS_ON], v[PP_I_MODE_KIND],''')
rep(r'''			rows_picture_done();
			next_vga = t + vga_period;
''', r'''			rows_picture_done();
			next_vga = t + vga_period;
#ifdef TINY386_PERFPORT
			while (f11_next < n_f11 && t >= (uint64_t) (opt_f11[f11_next] * 1e6)) {
				f11_next++;
				if (&vga_set_dirty_blocks && &vga_dirty_blocks) {
					vga_set_dirty_blocks(!vga_dirty_blocks);
					fprintf(hout, "[harness] Ctrl+Alt+F11 at t=%.3f: vbe_blocks = %d now\n",
						t / 1e6, vga_dirty_blocks);
					pp_line("f11");
				}
			}
#endif
''')
rep(r'''	uint64_t next_pp = opt_perfport > 0 ? (uint64_t) (opt_perfport * 1e6) : UINT64_MAX;
''', r'''	uint64_t next_pp = opt_perfport > 0 ? (uint64_t) (opt_perfport * 1e6) : UINT64_MAX;
	int f11_next = 0;
''')
rep(r'''		else if (!strcmp(a, "-perfport")) opt_perfport = atof(ARG());
''', r'''		else if (!strcmp(a, "-perfport")) opt_perfport = atof(ARG());
		else if (!strcmp(a, "-f11")) {
			char *q = ARG();
			while (q && *q && n_f11 < 16) {
				opt_f11[n_f11++] = strtod(q, &q);
				if (*q == ',')
					q++;
				else
					break;
			}
		}
''')
rep(r'''		"  -perfport S   the information port's values, read as a guest reads them,\n"''',
    r'''		"  -f11 T,T...   Ctrl+Alt+F11 (vbe_blocks the other way) at these virtual s, as\n"
		"                the board's speed.c does it between two pictures (v4.2 on)\n"
		"  -perfport S   the information port's values, read as a guest reads them,\n"''')
open(p, "w").write(s)
PY
diff -u ~/perfapp/pfix/h/harness/headless.c ph/harness/headless.c > ph/harness-v43.diff || true
wc -l ph/harness-v43.diff
cp $K/v3/card/tiny386/bios.bin $K/v3/card/tiny386/vgabios.bin ph/img/
chmod 444 ph/img/bios.bin ph/img/vgabios.bin
cp ~/perfapp/pfix/keys/*.txt ~/perfapp/pfix/keys/mkkeys.py keys/
# ---- the builds
export PATH=$PATH:/sbin:/usr/sbin
cd $A/fh
BOARD=korvo1 bash harness/build.sh $A/t-v4.3 esp bin/r43 64 > bin/r43.build.txt 2>&1 && echo "built fh r43" || { tail bin/r43.build.txt; exit 1; }
BOARD=dosboard bash harness/build.sh $A/t-v4.3 esp bin/r43d 64 > bin/r43d.build.txt 2>&1 && echo "built fh r43d" || { tail bin/r43d.build.txt; exit 1; }
grep -c perfport bin/r43/commands.txt bin/r43d/commands.txt
grep -h "warnings\|instr" bin/r43/build.txt bin/r43d/build.txt
cd $A/ph
for b in korvo1 dosboard; do
  BOARD=$b bash harness/build.sh $A/t-v4.3 esp bin/v43-$b 64 > bin/v43-$b.build.txt 2>&1 && echo "built ph v43-$b" || { tail bin/v43-$b.build.txt; exit 1; }
done
grep -c perfport bin/v43-korvo1/commands.txt
grep -h "warnings" bin/*/build.txt
