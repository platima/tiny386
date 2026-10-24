#!/usr/bin/env python3
# patch_harness.py HARNESS_DIR: the review of v4.2's finding 1, in a copy of the flicker
# harness (~/flicker/apply42/h/harness).  The harness counted a clear only after v4.1's
# memset(), which v4.2's board build (VGA_CLEAR_AROUND) compiles out, so "no clear over a
# picture" was true by construction.  This adds:
#   * instr_vga.py: pcr_black(d, n) at the top of vga_black() (each pixel it is about to make
#     black), and pcr_ev(PCR_EV_AROUND) where vga_clear_around() takes a clear;
#   * headless.c: every pixel made black while a picture is drawn (by a clear's memset() or by
#     vga_black()) is noted; once the picture is done, a noted pixel that is not black in it
#     was black on its way from the old picture to the new one (it must be 0 for v4.2).
import sys

d = sys.argv[1]


def sub(text, old, new, what, count=1):
    n = text.count(old)
    if n != count:
        sys.exit("patch_harness.py: %s: %d matches, wanted %d" % (what, n, count))
    return text.replace(old, new)


# ---- instr_vga.py
p = d + "/instr_vga.py"
s = open(p).read()
s = sub(s, "       PCR_EV_ASK_WHOLE, PCR_EV_VGA_W, PCR_EV_VBE_W, PCR_EV_3DA };\n",
        "       PCR_EV_ASK_WHOLE, PCR_EV_VGA_W, PCR_EV_VBE_W, PCR_EV_3DA, PCR_EV_AROUND };\n"
        "void pcr_black(const uint16_t *d, int n);\n", "instr enum")
s = sub(s, "# every call of vga_clear_screen(s);\n",
        """# v4.2's VGA_CLEAR_AROUND (apply42): what vga_black() makes black, and each clear that
# vga_clear_around() takes (v4.1's vga.c has neither; nothing is added then)
n_black = 0
m = re.search(r'static void vga_black\\(uint16_t \\*d, int n\\)\\n\\{\\n', src)
if m:
    src = src[:m.end()] + '    pcr_black(d, n);\\n' + src[m.end():]
    m = re.search(r'static void vga_clear_around\\(VGAState \\*s, int x, int y, int w, int h\\)\\n\\{\\n', src)
    must(m, "vga_black() but no vga_clear_around()")
    k = src.index('    s->clear_around = false;\\n', m.end()) + len('    s->clear_around = false;\\n')
    src = src[:k] + '    pcr_ev(PCR_EV_AROUND, x, y, (long) w * 65536 + h);\\n' + src[k:]
    n_black = 1

# every call of vga_clear_screen(s);
""", "instr black hook")
s = sub(s, 'print("instr_vga.py: %d clear sites, %d requests, %d window stores hooked" % (n_sites, n_ask, n_mw))',
        'print("instr_vga.py: %d clear sites, %d requests, %d window stores hooked, vga_black() %s"\n'
        '      % (n_sites, n_ask, n_mw, "hooked" if n_black else "not there"))', "instr print")
open(p, "w").write(s)

# ---- headless.c
p = d + "/headless.c"
s = open(p).read()
s = sub(s, """enum { PCR_EV_CLEARED = 1, PCR_EV_CLEAR_SITE, PCR_EV_ASK_FORCE, PCR_EV_ASK_CRT,
       PCR_EV_ASK_WHOLE, PCR_EV_VGA_W, PCR_EV_VBE_W, PCR_EV_3DA };
static const char *const ev_name[] = { "?", "CLEARED", "CLEAR_SITE", "ASK_FORCE",
	"ASK_CRT", "ASK_WHOLE", "VGA", "VBE", "3DA" };""",
        """enum { PCR_EV_CLEARED = 1, PCR_EV_CLEAR_SITE, PCR_EV_ASK_FORCE, PCR_EV_ASK_CRT,
       PCR_EV_ASK_WHOLE, PCR_EV_VGA_W, PCR_EV_VBE_W, PCR_EV_3DA, PCR_EV_AROUND };
static const char *const ev_name[] = { "?", "CLEARED", "CLEAR_SITE", "ASK_FORCE",
	"ASK_CRT", "ASK_WHOLE", "VGA", "VBE", "3DA", "AROUND" };""", "headless enum")
s = sub(s, "static int in_board_step, in_reference;\n",
        "static int in_board_step, in_reference;\n"
        "static void bk_done(void);     /* apply42: see pcr_black() */\n", "bk_done prototype")
s = sub(s, """	long flash;             /* panel pixels a clear turned black */
};
static struct pcr_acc acc;
""", """	long flash;             /* panel pixels a clear turned black */
	/* apply42: clears vga_clear_around() took; pixels vga_black() made black, and
	   of those, and of the memset()'s, the ones not black once the picture was done */
	unsigned around;
	long vb_px, vb_mid, mem_mid;
};
static struct pcr_acc acc;

/* apply42 (the review of v4.2, finding 1): each pixel made black while a
   picture is drawn, taken to reach the panel at once (the worst case: the
   cache writes it back before the picture is done).  A clear's memset()
   (v4.1, or VGA_NO_CLEAR_AROUND) notes the panel pixels it turns black
   (pcr_ev(), PCR_EV_CLEARED); VGA_CLEAR_AROUND's vga_black() (v4.2) the
   pixels it is about to make black (pcr_black()).  When the picture is
   done (bk_done(), straight after the drawing), a noted pixel that is not
   black in it was black between the old picture and the new one. */
#define BK_MEMSET 0x80000000u
static uint32_t *bk_idx;
static size_t bk_n, bk_cap;
static void bk_push(uint32_t v)         /* the caller holds ev_lock */
{
	if (bk_n == bk_cap) {
		bk_cap = bk_cap ? bk_cap * 2 : 65536;
		bk_idx = realloc(bk_idx, bk_cap * sizeof(*bk_idx));
		if (!bk_idx) {
			fprintf(stderr, "bk_push: out of memory\\n");
			exit(2);
		}
	}
	bk_idx[bk_n++] = v;
}
void pcr_black(const uint16_t *d, int n)
{
	if (!opt_board || in_reference || in_snapshot || !in_board_step || n <= 0)
		return;
	const uint16_t *f = (const uint16_t *) fb;
	if (d < f || d + n > f + (size_t) fb_w * fb_h)
		return;
	pthread_mutex_lock(&ev_lock);
	for (int i = 0; i < n; i++)
		if (d[i]) {
			bk_push((uint32_t) (d - f) + (uint32_t) i);
			acc.vb_px++;
		}
	pthread_mutex_unlock(&ev_lock);
}
static void bk_done(void)
{
	const uint16_t *f = (const uint16_t *) fb;
	pthread_mutex_lock(&ev_lock);
	for (size_t i = 0; i < bk_n; i++) {
		uint32_t v = bk_idx[i];
		if (f[v & ~BK_MEMSET]) {
			if (v & BK_MEMSET)
				acc.mem_mid++;
			else
				acc.vb_mid++;
		}
	}
	bk_n = 0;
	pthread_mutex_unlock(&ev_lock);
}
""", "acc struct and bk functions")
s = sub(s, """			for (size_t i = 0; i < n; i++)
				f += panel[i] != 0;
			memset(panel, 0, n * 2);""", """			for (size_t i = 0; i < n; i++)
				if (panel[i]) {
					f++;
					bk_push((uint32_t) i | BK_MEMSET);
				}
			memset(panel, 0, n * 2);""", "memset noting")
s = sub(s, "	case PCR_EV_ASK_WHOLE: acc.ask_whole++; break;\n",
        "	case PCR_EV_ASK_WHOLE: acc.ask_whole++; break;\n"
        "	case PCR_EV_AROUND: acc.around++; break;\n", "around event")
s = sub(s, """	in_board_step = 1;
	pc_vga_step(g_pc);
	in_board_step = 0;""", """	in_board_step = 1;
	pc_vga_step(g_pc);
	bk_done();
	in_board_step = 0;""", "board_step bk_done")
s = sub(s, """	vga_refresh(g_pc->vga, redraw, NULL, 0);
	in_board_step = was;""", """	vga_refresh(g_pc->vga, redraw, NULL, 0);
	bk_done();
	in_board_step = was;""", "guest_shot bk_done")
s = sub(s, "static uint64_t first_bad_step, first_flash_step;\n",
        "static uint64_t first_bad_step, first_flash_step;\n"
        "static uint64_t tot_around_steps, tot_vb_steps, tot_vb_px, tot_vb_mid_steps, tot_vb_mid_px,\n"
        "	tot_mem_mid_steps, tot_mem_mid_px, first_mid_step;      /* apply42 */\n", "totals")
s = sub(s, """	tot_steps++;
	if (a.cleared)
		tot_clear_steps++;
""", """	tot_steps++;
	if (a.cleared)
		tot_clear_steps++;
	if (a.around)
		tot_around_steps++;
	if (a.vb_px) {
		tot_vb_steps++;
		tot_vb_px += a.vb_px;
	}
	if (a.vb_mid || a.mem_mid) {
		if (!first_mid_step)
			first_mid_step = step_no;
		if (a.vb_mid) {
			tot_vb_mid_steps++;
			tot_vb_mid_px += a.vb_mid;
			strip_trigger("black-between", 0);
		}
		if (a.mem_mid) {
			tot_mem_mid_steps++;
			tot_mem_mid_px += a.mem_mid;
		}
	}
""", "board_record totals")
s = sub(s, """		(unsigned long long) tot_flash_px, (unsigned long long) first_flash_step,
		(unsigned long long) tot_blank_steps);
""", """		(unsigned long long) tot_flash_px, (unsigned long long) first_flash_step,
		(unsigned long long) tot_blank_steps);
	fprintf(hout, "[harness] board: black between two pictures (apply42): clears made around a "
		"picture (VGA_CLEAR_AROUND) in %llu steps; vga_black() made %llu pixels black in %llu "
		"steps, %llu of them (in %llu steps) not black once the picture was done; a clear's "
		"memset() made %llu panel pixels black, %llu of them (in %llu steps) not black once "
		"the picture was done; first at step %llu\\n",
		(unsigned long long) tot_around_steps, (unsigned long long) tot_vb_px,
		(unsigned long long) tot_vb_steps, (unsigned long long) tot_vb_mid_px,
		(unsigned long long) tot_vb_mid_steps, (unsigned long long) tot_flash_px,
		(unsigned long long) tot_mem_mid_px, (unsigned long long) tot_mem_mid_steps,
		(unsigned long long) first_mid_step);
""", "board_end line")
open(p, "w").write(s)
print("patch_harness.py: instr_vga.py and headless.c changed")
