/*
 * Host test for esp/main/ms7210.c against a fake I2C bus.
 *
 * The fake keeps a register image (filled with a pattern, so that a
 * read-modify-write that disturbs other bits shows), answers the chip ID,
 * the PLL lock bit, hot plug, the input-stable bit and the PCLK meter, and
 * serves an EDID at 0x50 only while the DDC pass-through is on.  Every bus
 * event goes to a log, which check_ms7210_table.py then reads against the
 * table in encoder.md 8.4.
 *
 *   ms7210_test OUTDIR      writes OUTDIR/ms7210_<scenario>.log, prints results
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ms7210.h"

static int failures, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); printf("\n"); } \
} while (0)

typedef struct {
	uint8_t regs[0x10000];
	uint8_t written[0x10000];
	uint32_t hz;
	int hpd;
	int pll_lock_after;     /* locked once 0x0910 has been written this often; 0 = never */
	int pll_triggers;
	int chip_nak;
	uint8_t chip_id0;
	const uint8_t *edid;
	int edid_nak;
	int pass_on;
	FILE *log;
	/* what was seen */
	int ddc_wrong_speed;    /* 0x50 transactions not at 20 kHz */
	int ddc_closed;         /* 0x50 transactions with the pass-through off */
	int bad_wire;           /* register writes that were not 3 bytes */
	uint32_t delay_us;
	char row[8];
	int e1_rows, e7_rows, edid_reads;
} fake_t;

static uint8_t pattern(int a)
{
	return (uint8_t)(a * 37 + 0x5A);
}

static void fake_reset(fake_t *f)
{
	FILE *log = f->log;

	memset(f, 0, sizeof(*f));
	f->log = log;
	for (int a = 0; a < 0x10000; a++)
		f->regs[a] = pattern(a);
	f->hz = 100000;
	f->pll_lock_after = 1;
	f->chip_id0 = 0xA2;
}

static void lg(fake_t *f, const char *fmt, ...)
{
	va_list ap;

	if (!f->log)
		return;
	va_start(ap, fmt);
	vfprintf(f->log, fmt, ap);
	va_end(ap);
}

static uint8_t reg_read(fake_t *f, uint16_t r)
{
	switch (r) {
	case 0x0000: return f->chip_id0;
	case 0x0001: return 0x20;
	case 0x0002: return 0x0A;
	case 0x001E: return 6068 & 0xFF;
	case 0x001F: return 6068 >> 8;
	case 0x0502: return (f->regs[r] & ~1) | (f->hpd & 1);
	case 0x051D: return f->regs[r] | 0x10;
	case 0x091C: {
		int locked = f->pll_lock_after && f->pll_triggers >= f->pll_lock_after;
		return (f->regs[r] & 0x7F) | (locked ? 0x80 : 0);
	}
	default:
		return f->regs[r];
	}
}

static int f_write(void *ctx, uint8_t addr, const uint8_t *b, int len)
{
	fake_t *f = ctx;

	lg(f, "W %02X", addr);
	for (int i = 0; i < len; i++)
		lg(f, " %02X", b[i]);
	lg(f, " hz=%u\n", f->hz);
	if (addr != MS7210_I2C_ADDR || f->chip_nak) {
		lg(f, "NAK\n");
		return -1;
	}
	if (len != 3) {
		f->bad_wire++;
		return -1;
	}
	uint16_t r = b[0] | (b[1] << 8);
	f->regs[r] = b[2];
	f->written[r] = 1;
	if (r == 0x0910 && b[2] == 0x01)
		f->pll_triggers++;
	if (r == 0x0012)
		f->pass_on = (b[2] == 0x0E);
	return 0;
}

static int f_write_read(void *ctx, uint8_t addr, const uint8_t *w, int wlen, uint8_t *r, int rlen)
{
	fake_t *f = ctx;

	lg(f, "R %02X", addr);
	for (int i = 0; i < wlen; i++)
		lg(f, " %02X", w[i]);
	if (addr == MS7210_I2C_ADDR && !f->chip_nak && wlen == 2 && rlen == 1) {
		r[0] = reg_read(f, w[0] | (w[1] << 8));
		lg(f, " -> %02X n=1 hz=%u\n", r[0], f->hz);
		return 0;
	}
	if (addr == MS7210_EDID_ADDR && wlen == 1) {
		if (f->hz != 20000)
			f->ddc_wrong_speed++;
		if (!f->pass_on)
			f->ddc_closed++;
		if (!f->pass_on || f->edid_nak || !f->edid) {
			lg(f, " NAK hz=%u\n", f->hz);
			return -1;
		}
		for (int i = 0; i < rlen; i++)
			r[i] = (w[0] + i < 256) ? f->edid[w[0] + i] : 0xFF;
		f->edid_reads++;
		lg(f, " -> %02X %02X %02X ... n=%d hz=%u\n", r[0], r[1], r[2], rlen, f->hz);
		return 0;
	}
	lg(f, " NAK hz=%u\n", f->hz);
	return -1;
}

static void f_speed(void *ctx, uint32_t hz)
{
	fake_t *f = ctx;

	f->hz = hz;
	lg(f, "SPEED %u\n", hz);
}

static void f_rstn(void *ctx, int level)
{
	lg(ctx, "RSTN %d\n", level);
}

static void f_delay(void *ctx, uint32_t us)
{
	fake_t *f = ctx;

	f->delay_us += us;
	lg(f, "DELAY %u\n", us);
}

static void f_log(void *ctx, const char *line)
{
	lg(ctx, "# %s\n", line);
	printf("    %s\n", line);
}

static void f_row(void *ctx, const char *id)
{
	fake_t *f = ctx;

	snprintf(f->row, sizeof(f->row), "%s", id);
	if (strcmp(id, "E1") == 0)
		f->e1_rows++;
	if (strcmp(id, "E7") == 0)
		f->e7_rows++;
	lg(f, "ROW %s\n", id);
}

static fake_t fake;
static const ms7210_io_t io = {
	.ctx = &fake,
	.i2c_write = f_write,
	.i2c_write_read = f_write_read,
	.i2c_speed = f_speed,
	.rstn = f_rstn,
	.delay_us = f_delay,
	.log = f_log,
	.row = f_row,
};

/* ==== a fake monitor ==== */

static uint8_t sum8(const uint8_t *p, int n)
{
	uint8_t s = 0;
	while (n--)
		s += *p++;
	return s;
}

/* EDID 1.3 with 800x600 at 60 Hz in the established timings and as the
   first detailed timing, a name, and one CEA-861 block with an audio data
   block and, if hdmi, the HDMI vendor block (IEEE OUI 00-0C-03). */
static void make_edid(uint8_t *e, int hdmi, int est, int dtd)
{
	static const uint8_t hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	uint16_t id = (('T' - '@') << 10) | (('S' - '@') << 5) | ('T' - '@');
	uint8_t *x = e + 128;
	int i;

	memset(e, 0, 256);
	memcpy(e, hdr, 8);
	e[8] = id >> 8;
	e[9] = id & 0xFF;
	e[10] = 0x34;
	e[11] = 0x12;
	e[0x12] = 1;
	e[0x13] = 3;
	e[0x23] = 0x20 | (est ? 0x01 : 0);      /* 640x480 60, 800x600 60 */
	for (i = 0x26; i < 0x36; i++)
		e[i] = 0x01;                        /* unused standard timings */
	if (dtd) {
		uint8_t *d = e + 0x36;              /* 800x600, 40 MHz */
		d[0] = 4000 & 0xFF; d[1] = 4000 >> 8;
		d[2] = 800 & 0xFF; d[3] = 256 & 0xFF; d[4] = ((800 >> 8) << 4) | (256 >> 8);
		d[5] = 600 & 0xFF; d[6] = 28; d[7] = (600 >> 8) << 4;
	} else {
		uint8_t *d = e + 0x36;              /* 1024x768 */
		d[0] = 6500 & 0xFF; d[1] = 6500 >> 8;
		d[2] = 1024 & 0xFF; d[3] = 320 & 0xFF; d[4] = ((1024 >> 8) << 4) | (320 >> 8);
		d[5] = 768 & 0xFF; d[6] = 38; d[7] = (768 >> 8) << 4;
	}
	uint8_t *n = e + 0x48;
	n[3] = 0xFC;
	memcpy(n + 5, "DOS TEST\n    ", 13);
	e[0x5A + 3] = 0x10;                     /* dummy descriptors */
	e[0x6C + 3] = 0x10;
	e[0x7E] = 1;
	e[0x7F] = (uint8_t)(0x100 - sum8(e, 127));

	x[0] = 0x02;
	x[1] = 0x03;
	x[3] = 0x40;                            /* basic audio */
	i = 4;
	x[i++] = (1 << 5) | 3;                  /* audio data block: LPCM, 2 ch, 32/44.1/48 kHz */
	x[i++] = 0x09; x[i++] = 0x07; x[i++] = 0x07;
	if (hdmi) {
		x[i++] = (3 << 5) | 5;              /* vendor block */
		x[i++] = 0x03; x[i++] = 0x0C; x[i++] = 0x00;
		x[i++] = 0x10; x[i++] = 0x00;
	} else {
		x[i++] = (3 << 5) | 3;              /* a vendor block of somebody else */
		x[i++] = 0x11; x[i++] = 0x22; x[i++] = 0x33;
	}
	x[2] = i;
	x[127] = (uint8_t)(0x100 - sum8(x, 127));
}

/* ==== checks on the log ==== */

/* InfoFrames: from the log, the header registers and the bytes written
   to 0x050D after them must sum to zero; returns how many frames were found */
static int check_infoframes(const char *path, int *bad)
{
	FILE *fp = fopen(path, "r");
	char line[256];
	uint8_t hdr[3] = { 0 }, data[40];
	int n = 0, frames = 0, in_frame = 0;

	*bad = 0;
	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		unsigned a, lo, hi, v;
		if (sscanf(line, "W %x %x %x %x", &a, &lo, &hi, &v) != 4 || a != 0x59)
			continue;
		unsigned r = lo | (hi << 8);
		if (r == 0x050D) {
			if (!in_frame) {
				in_frame = 1;
				n = 0;
			}
			data[n++] = v;
			if (n == hdr[2] + 1) {
				uint8_t s = sum8(hdr, 3) + sum8(data, n);
				frames++;
				printf("  InfoFrame type %02X version %d length %d, checksum %02X: sum %s\n",
				       hdr[0], hdr[1], hdr[2], data[0], s == 0 ? "0, right" : "WRONG");
				if (s)
					(*bad)++;
				in_frame = 0;
			}
			continue;
		}
		in_frame = 0;
		if (r == 0x050A) hdr[0] = v;
		if (r == 0x0509) hdr[1] = v;
		if (r == 0x050B) hdr[2] = v;
	}
	fclose(fp);
	return frames;
}

/* every register write is START, 0x59, index low, index high, data: find
   the write of 0x1281 (B22) and look at its bytes */
static int check_wire(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[256];
	int found = 0;

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		unsigned a, b0, b1, b2;
		if (sscanf(line, "W %x %x %x %x", &a, &b0, &b1, &b2) == 4 &&
		    a == 0x59 && b0 == 0x81 && b1 == 0x12)
			found++;
		if (sscanf(line, "W %x %x %x %x", &a, &b0, &b1, &b2) == 4 &&
		    a == 0x59 && b0 == 0x12 && b1 == 0x81)
			return -1;   /* high byte first: wrong */
	}
	fclose(fp);
	return found;
}

static void dump_final(FILE *log, fake_t *f)
{
	for (int r = 0; r < 0x10000; r++)
		if (f->written[r])
			fprintf(log, "FINAL %04X %02X\n", r, f->regs[r]);
}

static ms7210_t m;

static FILE *open_log(const char *dir, const char *name, char *path)
{
	sprintf(path, "%s/ms7210_%s.log", dir, name);
	FILE *fp = fopen(path, "w");
	if (!fp) {
		perror(path);
		exit(2);
	}
	return fp;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[512];
	static uint8_t edid_hdmi[256], edid_dvi[256], edid_plain[256];
	ms7210_cfg_t cfg;
	int bad, r;

	make_edid(edid_hdmi, 1, 1, 1);
	make_edid(edid_dvi, 0, 1, 1);
	make_edid(edid_plain, 1, 0, 0);

	/* 1. InfoFrame payloads against encoder.md 8.4 E58 and E64 */
	{
		uint8_t avi[14], aif[11];
		static const uint8_t want_avi[14] = { 0x45, 0x12, 0x18 };
		static const uint8_t want_aif[11] = { 0x70, 0x01 };
		ms7210_avi_infoframe(avi);
		ms7210_audio_infoframe(aif);
		printf("infoframes: AVI %02X %02X %02X ..., audio %02X %02X ...\n",
		       avi[0], avi[1], avi[2], aif[0], aif[1]);
		CHECK(memcmp(avi, want_avi, 14) == 0, "AVI payload differs from E58");
		CHECK(memcmp(aif, want_aif, 11) == 0, "audio payload differs from E64");
		CHECK((uint8_t)(0x82 + 0x02 + 0x0D + sum8(avi, 14)) == 0, "AVI sum");
		CHECK((uint8_t)(0x84 + 0x01 + 0x0A + sum8(aif, 11)) == 0, "audio sum");
	}

	/* 2. the D rows against encoder.md 8.3's arithmetic for VESA 800x600 60 */
	{
		const int hact = 800, hfp = 40, hs = 128, hbp = 88, vact = 600, vfp = 1, vs = 4, vbp = 23;
		const int htot = hact + hfp + hs + hbp, vtot = vact + vfp + vs + vbp;
		const int hoff = hs + hbp, voff = vs + vbp;
		const struct { const char *id; int v; } want[] = {
			{ "D1", htot }, { "D2", vtot }, { "D3", 3 + hoff }, { "D4", 2 },
			{ "D5", hs }, { "D6", vs }, { "D7", hoff }, { "D8", hoff + hact },
			{ "D9", voff }, { "D10", voff + vact }, { "D11", voff + vtot / 2 + 1 },
			{ "D12", voff + vtot / 2 + vact / 2 + 1 },
		};
		int ok = 0;
		for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++)
			for (int k = 0; ms7210_rows[k].id; k++)
				if (strcmp(ms7210_rows[k].id, want[i].id) == 0) {
					CHECK(ms7210_rows[k].arg == want[i].v, "%s is %d, timing gives %d",
					      want[i].id, ms7210_rows[k].arg, want[i].v);
					ok += ms7210_rows[k].arg == want[i].v;
				}
		printf("timing rows D1 to D12: %d of 12 match the VESA arithmetic\n", ok);
		CHECK(htot == 1056 && vtot == 628, "VESA totals");
		/* 40 MHz / (1056 x 628) */
		printf("  frame rate %.3f Hz\n", 40e6 / (htot * vtot));
	}

	/* 3. EDID parsing */
	{
		ms7210_edid_info_t in;
		uint8_t e[256];
		CHECK(ms7210_edid_parse(edid_hdmi, 256, &in) == 0 && in.valid && in.hdmi &&
		      in.ext_ok && in.basic_audio && in.est_800x600_60 && in.dtd_800x600 &&
		      strcmp(in.name, "DOS TEST") == 0 && strcmp(in.vendor, "TST") == 0 &&
		      in.product == 0x1234, "HDMI EDID parse");
		CHECK(ms7210_edid_parse(edid_dvi, 256, &in) == 0 && in.valid && !in.hdmi &&
		      in.est_800x600_60, "DVI EDID parse");
		CHECK(ms7210_edid_parse(edid_plain, 256, &in) == 0 && in.hdmi && !in.est_800x600_60 &&
		      !in.dtd_800x600 && !in.std_800x600_60, "EDID without 800x600");
		memcpy(e, edid_hdmi, 256);
		e[0x26] = 0x45; e[0x27] = 0x40; e[0x7F] -= 0x45 + 0x40 - 2;
		CHECK(ms7210_edid_parse(e, 256, &in) == 0 && in.std_800x600_60, "standard timing 800x600");
		memcpy(e, edid_hdmi, 256);
		e[0x7F] ^= 1;
		CHECK(ms7210_edid_parse(e, 256, &in) != 0 && !in.valid, "bad block 0 checksum");
		memcpy(e, edid_hdmi, 256);
		e[255] ^= 1;
		CHECK(ms7210_edid_parse(e, 256, &in) == 0 && !in.ext_ok && !in.hdmi,
		      "bad extension checksum: no HDMI claimed");
		printf("EDID parser: done\n");
	}

	/* 4. a monitor with an HDMI vendor block, PLL locks at once: the log for
	      check_ms7210_table.py */
	printf("scenario hdmi:\n");
	fake.log = open_log(dir, "hdmi", path);
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_hdmi;
	ms7210_default_cfg(&cfg);
	r = ms7210_probe(&m, &io, &cfg);
	CHECK(r == 0 && m.found, "probe");
	r = ms7210_init(&m);
	CHECK(r == 0 && m.errors == 0, "init without errors");
	CHECK(!m.dvi && m.edid_ok && m.edid_info.hdmi, "HDMI mode chosen");
	CHECK(m.pll_locked && m.pll_tries == 1, "PLL locked on the first pass");
	CHECK(m.pclk_meter == 6068, "PCLK meter read %u", m.pclk_meter);
	CHECK((fake.regs[0x0503] & 1) == 0, "0x0503 bit 0 clear (HDMI)");
	CHECK(fake.regs[0x0531] == 0x84, "0x0531 = 0x84 (audio on, 48 kHz), is %02X", fake.regs[0x0531]);
	CHECK((fake.regs[0x050E] & 0x60) == 0x60, "AVI and audio InfoFrames on");
	CHECK((fake.regs[0x1281] & 7) == 0, "0x1281 phase 0 (plan A)");
	CHECK(fake.regs[0x0012] == 0x0F, "DDC pass-through off at the end");
	CHECK(fake.ddc_wrong_speed == 0, "all DDC reads at 20 kHz");
	CHECK(fake.ddc_closed == 0, "no DDC read with the pass-through off");
	CHECK(fake.hz == 100000, "bus back at 100 kHz");
	CHECK(fake.bad_wire == 0, "every register write 3 bytes");
	CHECK(fake.edid_reads == 2, "EDID: two blocks read");
	dump_final(fake.log, &fake);
	fclose(fake.log);
	fake.log = NULL;
	r = check_wire(path);
	CHECK(r > 0, "register 0x1281 written as 81 12 (index low byte first)");
	r = check_infoframes(path, &bad);
	CHECK(r == 2 && bad == 0, "two InfoFrames with right sums (found %d, bad %d)", r, bad);

	/* 5. a DVI monitor (no HDMI vendor block): DVI mode, no audio, no InfoFrames */
	printf("scenario dvi:\n");
	fake.log = open_log(dir, "dvi", path);
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_dvi;
	ms7210_probe(&m, &io, &cfg);
	r = ms7210_init(&m);
	CHECK(r == 0 && m.dvi, "DVI mode chosen");
	CHECK((fake.regs[0x0503] & 1) == 1, "0x0503 bit 0 set (DVI)");
	CHECK((fake.regs[0x0531] & 0x80) == 0, "audio enable off");
	CHECK((fake.regs[0x050E] & 0x60) == 0, "no InfoFrames enabled");
	dump_final(fake.log, &fake);
	fclose(fake.log);
	fake.log = NULL;

	/* 6. no monitor at start, then one plugged in, out, and a DVI one in */
	printf("scenario hotplug:\n");
	fake.log = open_log(dir, "hotplug", path);
	fake_reset(&fake);
	fake.hpd = 0;
	fake.edid = edid_hdmi;
	ms7210_probe(&m, &io, &cfg);
	r = ms7210_init(&m);
	CHECK(r == 0 && !m.dvi && fake.edid_reads == 0 && fake.e1_rows == 1,
	      "no monitor: HDMI set up, no EDID read");
	CHECK(ms7210_poll(&m) == 0, "poll with nothing new");
	fake.hpd = 1;
	CHECK(ms7210_poll(&m) == 1 && fake.e1_rows == 2 && fake.edid_reads == 2 && !m.dvi,
	      "plugged in: EDID read, section E again");
	CHECK(ms7210_poll(&m) == 0 && fake.e1_rows == 2, "no change: nothing done");
	fake.hpd = 0;
	CHECK(ms7210_poll(&m) == 0 && fake.e1_rows == 2, "unplugged: nothing set up");
	fake.edid = edid_dvi;
	fake.hpd = 1;
	CHECK(ms7210_poll(&m) == 1 && fake.e1_rows == 3 && m.dvi, "DVI monitor plugged in");
	fclose(fake.log);
	fake.log = NULL;

	/* 7. PLL locks on the third pass */
	printf("scenario pll3:\n");
	fake.log = open_log(dir, "pll3", path);
	fake_reset(&fake);
	fake.pll_lock_after = 3;
	ms7210_probe(&m, &io, &cfg);
	ms7210_init(&m);
	CHECK(m.pll_locked && m.pll_tries == 3 && fake.e7_rows == 3,
	      "three passes through E7 to E40 (tries %d, E7 rows %d)", m.pll_tries, fake.e7_rows);
	fclose(fake.log);
	fake.log = NULL;

	/* 8. PLL never locks: eight passes, then the rest of section E anyway */
	printf("scenario pllnever:\n");
	fake.log = open_log(dir, "pllnever", path);
	fake_reset(&fake);
	fake.pll_lock_after = 0;
	ms7210_probe(&m, &io, &cfg);
	ms7210_init(&m);
	CHECK(!m.pll_locked && m.pll_tries == MS7210_PLL_TRIES && fake.e7_rows == MS7210_PLL_TRIES,
	      "eight passes (tries %d, E7 rows %d)", m.pll_tries, fake.e7_rows);
	CHECK((fake.regs[0x0920] & 0x04) == 0x04, "E72 still reached");
	fclose(fake.log);
	fake.log = NULL;

	/* 9. monitor attached but the EDID does not answer: HDMI, as the vendor does */
	printf("scenario edidnak:\n");
	fake.log = open_log(dir, "edidnak", path);
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_dvi;
	fake.edid_nak = 1;
	ms7210_probe(&m, &io, &cfg);
	r = ms7210_init(&m);
	CHECK(!m.dvi && !m.edid_ok, "no EDID: HDMI");
	CHECK(fake.regs[0x0012] == 0x0F && fake.hz == 100000, "pass-through off and fast bus again");
	fclose(fake.log);
	fake.log = NULL;

	/* 10. the other clock edge (plan B), forced DVI, audio off */
	printf("scenario options:\n");
	fake.log = open_log(dir, "options", path);
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_hdmi;
	cfg.pclk_phase = 4;
	ms7210_probe(&m, &io, &cfg);
	ms7210_init(&m);
	CHECK((fake.regs[0x1281] & 7) == 4, "0x1281 phase 4 (plan B)");
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_hdmi;
	cfg.pclk_phase = 0;
	cfg.mode = MS7210_MODE_DVI;
	ms7210_probe(&m, &io, &cfg);
	ms7210_init(&m);
	CHECK(m.dvi && (fake.regs[0x0503] & 1) && fake.edid_reads == 0, "forced DVI, no EDID read");
	fake_reset(&fake);
	fake.hpd = 1;
	fake.edid = edid_hdmi;
	cfg.mode = MS7210_MODE_AUTO;
	cfg.audio = 0;
	ms7210_probe(&m, &io, &cfg);
	ms7210_init(&m);
	CHECK(!m.dvi && (fake.regs[0x0531] & 0x80) == 0 && (fake.regs[0x050E] & 0x60) == 0x40,
	      "HDMI without sound: audio and its InfoFrame off, AVI on");
	fclose(fake.log);
	fake.log = NULL;

	/* 11. the wrong board: no chip, or another chip */
	printf("scenario wrongboard:\n");
	fake.log = open_log(dir, "wrongboard", path);
	fake_reset(&fake);
	fake.chip_nak = 1;
	ms7210_default_cfg(&cfg);
	CHECK(ms7210_probe(&m, &io, &cfg) != 0 && !m.found, "nothing at 0x59: not found");
	fake_reset(&fake);
	fake.chip_id0 = 0x55;
	CHECK(ms7210_probe(&m, &io, &cfg) != 0 && !m.found, "other chip ID: not found");
	fclose(fake.log);
	fake.log = NULL;

	printf("ms7210_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
