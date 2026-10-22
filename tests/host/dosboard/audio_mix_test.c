/*
 * Host test of the ESP builds' sound mix, esp/main/audio_mix.c (v3.5).
 *
 * Built by run.sh with -DAUDIO_RATE=48000 (the DOS board's) against the
 * tree's audio_mix.c (with AUDIO_MIX_HOST: no pc.h), pcspk.c, adlib.c and
 * fmopl.c, with sb16.c included here as audio_rate_test.c does, so that the
 * SB16's state can be set up directly.
 *
 *   1. audio_mix_block() against a model in 64 bits: every level shift 0 to
 *      15 and the guards either side, every corner of the inputs, and
 *      random ones: the same value every time, so nothing wraps.
 *   2. v3.4's path (pc.c's mixer_callback(), holding the sum to 16 bits,
 *      then i2s.c's buf[i] / 2^shift) gives the very same values wherever
 *      its holds did not act; where they did, the new value is at least as
 *      far from zero, on the same side.
 *   3. At the DOS board's default level (AUDIO_SHIFT_DEFAULT in
 *      board_dosboard.h, which run.sh passes as AUDIO_SHIFT_DEFAULT_TEST)
 *      nothing can be held: the largest and smallest sums the three devices
 *      can make.  Two loud sources that v3.4 held, and level 0 held, not
 *      wrapped.
 *   4. A lone source at that level against v3.4's default (4): exactly
 *      v / 2^level, and 2^(4 - level) times v3.4's, give or take rounding.
 *   5. The devices themselves at their loudest, through
 *      audio_mix_devices(): the SB16 playing a full-scale 16-bit stereo
 *      square wave, the AdLib with all nine channels keyed on in step at
 *      full volume, the PC speaker on.  Two copies of each device, driven
 *      alike, one through audio_mix_devices() and one through the same calls
 *      made here and v3.4's path: the same values where v3.4 did not hold,
 *      and how loud each level gets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "audio_rate.h"
#ifndef SB16_C
#define SB16_C "sb16.c"
#endif
#include SB16_C
#include "pcspk.h"
#include "adlib.h"
#include "audio_mix.h"

#ifndef AUDIO_SHIFT_DEFAULT_TEST
#error "run.sh passes board_dosboard.h's AUDIO_SHIFT_DEFAULT as AUDIO_SHIFT_DEFAULT_TEST"
#endif
#define LEVEL AUDIO_SHIFT_DEFAULT_TEST

/* ==== what the three need from the rest of the PC (audio_rate_test.c) ==== */

static int pit_count = 1193;
int pit_get_mode(PITState *pit, int channel) { return 3; }
int pit_get_initial_count(PITState *pit, int channel) { return pit_count; }
int pit_get_out(PITState *pit, int channel) { return 0; }
int pit_get_gate(PITState *pit, int channel) { return 1; }
void pit_set_gate(PITState *pit, int channel, int val) { }

void i8257_dma_register_channel(IsaDma *obj, int nchan, IsaDmaTransferHandler transfer_handler,
				void *opaque) { }
void i8257_dma_hold_DREQ(IsaDma *obj, int nchan) { }
void i8257_dma_release_DREQ(IsaDma *obj, int nchan) { }
int i8257_dma_read_memory(IsaDma *obj, int nchan, void *buf, int pos, int len) { return 0; }
int i8257_dma_write_memory(IsaDma *obj, int nchan, void *buf, int pos, int len) { return 0; }
static void set_irq(void *pic, int irq, int level) { }

static long checks, failures;

static void check(int ok, const char *what, long a, long b)
{
	checks++;
	if (!ok) {
		failures++;
		if (failures <= 30)
			printf("FAIL %s: %ld against %ld\n", what, a, b);
	}
}

/* ==== the model, and v3.4's path ==== */

/* spk < 0: the PC speaker off */
static long model(int sb, int opl, int spk, int shift)
{
	long long s = (long long) sb + opl + (spk >= 0 ? (long long) (spk - 0x80) * 32 : 0);
	if (shift < 0)
		shift = 0;
	if (shift > AUDIO_MIX_SHIFT_MAX)
		shift = AUDIO_MIX_SHIFT_MAX;
	long long q = s / (1LL << shift);       /* C: towards zero */
	if (q > 32767)
		q = 32767;
	if (q < -32768)
		q = -32768;
	return (long) q;
}

/* pc.c's mixer_callback() for one value, as v3.4 built it (pc.c lines
   909 to 925 at v3.4: the SB16 and the AdLib added and held to 16 bits,
   then the PC speaker's (x - 0x80) << 5 added and held again), then i2s.c
   v3.4's buf[i] = buf[i] / div with div = 1 << i2s_audio_shift.  *held
   counts the holds that acted. */
static long v34(int sb, int opl, int spk, int shift, int *held)
{
	int res = sb + opl;
	if (res > 32767) { res = 32767; (*held)++; }
	if (res < -32768) { res = -32768; (*held)++; }
	int16_t d2 = res;
	if (spk >= 0) {
		res = d2;
		res += ((int) spk - 0x80) << 5;
		if (res > 32767) { res = 32767; (*held)++; }
		if (res < -32768) { res = -32768; (*held)++; }
		d2 = res;
	}
	const int div = 1 << shift;
	int16_t out = d2 / div;
	return out;
}

/* one value through audio_mix_block(), as both channels of one frame;
   *held is 1 if it was held */
static long one(int sb, int opl, int spk, int shift, int *held)
{
	int16_t s[2] = { sb, sb }, o[1] = { opl }, out[2];
	uint8_t k[1] = { spk >= 0 ? spk : 0x80 };
	int h = audio_mix_block(out, s, o, spk >= 0 ? k : NULL, 2, shift);
	check(out[0] == out[1], "both channels alike", out[0], out[1]);
	check(h == 0 || h == 2, "both channels held alike", h, 2);
	*held = h / 2;
	return out[0];
}

static uint32_t rng = 2463534242u;
static uint32_t xorshift(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

/* ==== 5: the devices at their loudest ==== */

struct devs {
	AdlibState *adlib;
	SB16State *sb16;
	PCSpkState *pcspk;
};

static int16_t sq_buf[AUDIO_BUF_LEN / 2];

static void devs_new(struct devs *d, int use_sb, int use_opl, int use_spk)
{
	d->adlib = adlib_new();
	d->sb16 = sb16_new(0x220, 5, NULL, NULL, NULL, set_irq);
	d->pcspk = pcspk_init((PITState *)1);
	if (use_sb) {
		/* 16-bit stereo at the output rate: one input frame to one output
		   frame; a square wave of 64 frames, full scale */
		SB16State *s = d->sb16;
		s->fmt = AUDIO_FORMAT_S16;
		s->fmt_stereo = 1;
		s->freq = AUDIO_RATE;
		s->active_out = 1;
		for (int i = 0; i < AUDIO_BUF_LEN / 2; i++)
			sq_buf[i] = (i / 2) % 64 < 32 ? 32767 : -32768;
		memcpy(s->audio_buf, sq_buf, AUDIO_BUF_LEN);
		s->audio_p = 0;
		s->audio_q = AUDIO_BUF_LEN;
	}
	if (use_opl) {
		/* every channel: modulator silent, carrier loudest, attack 15,
		   no decay, the same note keyed on, so all nine add in step */
		static const uint8_t mod_off[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
		adlib_write(d->adlib, 0x388, 0x01);
		adlib_write(d->adlib, 0x389, 0x20);
		for (int c = 0; c < 9; c++) {
			const uint8_t regs[][2] = {
				{ 0x20 + mod_off[c], 0x01 }, { 0x23 + mod_off[c], 0x21 },
				{ 0x40 + mod_off[c], 0x3F }, { 0x43 + mod_off[c], 0x00 },
				{ 0x60 + mod_off[c], 0xF0 }, { 0x63 + mod_off[c], 0xF0 },
				{ 0x80 + mod_off[c], 0x00 }, { 0x83 + mod_off[c], 0x00 },
				{ 0xC0 + c, 0x00 },
				{ 0xA0 + c, 580 & 0xFF },
				{ 0xB0 + c, 0x20 | (4 << 2) | (580 >> 8) },
			};
			for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
				adlib_write(d->adlib, 0x388, regs[i][0]);
				adlib_write(d->adlib, 0x389, regs[i][1]);
			}
		}
	}
	if (use_spk)
		pcspk_ioport_write(d->pcspk, 3);        /* gate and data on */
}

static void devs_refill(struct devs *d)
{
	/* the SB16's buffer kept full, as a guest playing on would */
	SB16State *s = d->sb16;
	if (s->active_out)
		s->audio_q = s->audio_p + AUDIO_BUF_LEN;
}

/* one second at AUDIO_RATE of the three devices through audio_mix_devices()
   (copy a) and through the same calls with v3.4's path and audio_mix_block()
   (copy b) */
static void loudest(const char *name, int use_sb, int use_opl, int use_spk)
{
	static int raw_peak;
	printf("  %s:\n", name);
	printf("    shift   new: min    max   held   v3.4: min    max  holds  differ where v3.4 did not hold\n");
	for (int shift = 0; shift <= 4; shift++) {
		struct devs a, b;
		devs_new(&a, use_sb, use_opl, use_spk);
		devs_new(&b, use_sb, use_opl, use_spk);
		long nmin = 0, nmax = 0, omin = 0, omax = 0, nheld = 0, oheld = 0, differ = 0;
		raw_peak = 0;
		for (long made = 0; made < AUDIO_RATE * 2; made += AUDIO_MIX_MAX) {
			int16_t out[AUDIO_MIX_MAX], out2[AUDIO_MIX_MAX];
			int16_t sb[AUDIO_MIX_MAX], opl[AUDIO_MIX_MAX / 2];
			uint8_t spk[AUDIO_MIX_MAX / 2];
			const int n = AUDIO_MIX_MAX;
			devs_refill(&a);
			devs_refill(&b);
			nheld += audio_mix_devices(out, n, shift, a.adlib, a.sb16, a.pcspk);
			/* copy b, asked as pc.c asks */
			memset(opl, 0, n);
			memset(sb, 0, 2 * n);
			adlib_callback(b.adlib, (uint8_t *) opl, n);
			sb16_audio_callback(b.sb16, (uint8_t *) sb, 2 * n);
			int on = pcspk_get_active_out(b.pcspk);
			if (on) {
				memset(spk, 0x80, n / 2);
				pcspk_callback(b.pcspk, spk, n / 2);
			}
			int h = audio_mix_block(out2, sb, opl, on ? spk : NULL, n, shift);
			(void) h;
			for (int i = 0; i < n; i++) {
				int held = 0;
				long o = v34(sb[i], opl[i / 2], on ? spk[i / 2] : -1, shift, &held);
				check(out[i] == out2[i], "audio_mix_devices() as the calls made here", out[i], out2[i]);
				check(out[i] == model(sb[i], opl[i / 2], on ? spk[i / 2] : -1, shift),
				      "devices: the model", out[i], model(sb[i], opl[i / 2], on ? spk[i / 2] : -1, shift));
				if (!held && out[i] != o)
					differ++;
				oheld += held;
				if (opl[i / 2] > raw_peak) raw_peak = opl[i / 2];
				if (-opl[i / 2] > raw_peak) raw_peak = -opl[i / 2];
				if (out[i] < nmin) nmin = out[i];
				if (out[i] > nmax) nmax = out[i];
				if (o < omin) omin = o;
				if (o > omax) omax = o;
			}
		}
		check(differ == 0, "devices: the same as v3.4 where it did not hold", differ, 0);
		printf("    %5d  %9ld %6ld %6ld  %10ld %6ld %6ld  %ld\n", shift, nmin, nmax, nheld,
		       omin, omax, oheld, differ);
		if (shift == LEVEL)
			check(nheld == 0, "devices: nothing held at the default level", nheld, 0);
		adlib_free(a.adlib);
		adlib_free(b.adlib);
	}
	if (use_opl)
		printf("    (the AdLib's own largest value, as fmopl.c gives it, already held to 16 "
		       "bits there: %d)\n", raw_peak);
}

int main(void)
{
	static const int corner[] = { -32768, -32767, -16384, -1, 0, 1, 16383, 32767 };
	static const int spk_corner[] = { -1, 0x00, 0x01, 0x7F, 0x80, 0x81, 0xFE, 0xFF };
	int held;
	printf("audio_mix_test: AUDIO_RATE %d, AUDIO_MIX_MAX %d\n", AUDIO_RATE, AUDIO_MIX_MAX);

	/* 1 and 2: corners, every shift and the guards */
	long corners = 0, v34_same = 0, v34_held = 0;
	for (int shift = -2; shift <= AUDIO_MIX_SHIFT_MAX + 3; shift++)
		for (unsigned a = 0; a < 8; a++)
			for (unsigned b = 0; b < 8; b++)
				for (unsigned c = 0; c < 8; c++) {
					int sb = corner[a], opl = corner[b], spk = spk_corner[c];
					long got = one(sb, opl, spk, shift, &held);
					long want = model(sb, opl, spk, shift);
					check(got == want, "corner: the model", got, want);
					long long sum = (long long) sb + opl + (spk >= 0 ? (spk - 0x80) * 32 : 0);
					check(held == (sum / (1LL << (shift < 0 ? 0 : shift > 15 ? 15 : shift)) != want),
					      "corner: held counted", held, 0);
					check(got == 0 || (got > 0) == (sum > 0), "corner: no wrap", got, sum);
					corners++;
					if (shift >= 0 && shift <= 8) {
						int h = 0;
						long o = v34(sb, opl, spk, shift, &h);
						if (!h) {
							check(o == got, "corner: v3.4 where it did not hold", o, got);
							v34_same++;
						} else {
							check((o == 0 || (o > 0) == (got > 0)) && labs(got) >= labs(o),
							      "corner: v3.4 held, the new value no nearer zero", got, o);
							v34_held++;
						}
					}
				}
	printf("  1. corners: %ld cases (shifts -2 to %d), model and no wrap\n", corners,
	       AUDIO_MIX_SHIFT_MAX + 3);
	printf("  2. against v3.4's path: %ld the same where v3.4 did not hold; %ld where it held\n",
	       v34_same, v34_held);

	/* random blocks of every length, every shift 0 to 8 */
	long values = 0;
	for (int round = 0; round < 40000; round++) {
		int n = 2 * (1 + xorshift() % (AUDIO_MIX_MAX / 2));
		int shift = xorshift() % 9;
		int16_t sb[AUDIO_MIX_MAX], opl[AUDIO_MIX_MAX / 2], out[AUDIO_MIX_MAX + 2];
		uint8_t spk[AUDIO_MIX_MAX / 2];
		int with_spk = xorshift() & 1;
		for (int i = 0; i < n; i++)
			sb[i] = (int16_t) xorshift();
		for (int i = 0; i < n / 2; i++) {
			opl[i] = (int16_t) xorshift();
			spk[i] = (uint8_t) xorshift();
		}
		out[n] = out[n + 1] = 0x5A5A;
		int h = audio_mix_block(out, sb, opl, with_spk ? spk : NULL, n, shift);
		int counted = 0;
		for (int i = 0; i < n; i++) {
			int s = with_spk ? spk[i / 2] : -1;
			long want = model(sb[i], opl[i / 2], s, shift);
			check(out[i] == want, "random: the model", out[i], want);
			long long sum = (long long) sb[i] + opl[i / 2] + (s >= 0 ? (s - 0x80) * 32 : 0);
			counted += sum / (1LL << shift) != want;
			int hv = 0;
			long o = v34(sb[i], opl[i / 2], s, shift, &hv);
			if (!hv)
				check(o == out[i], "random: v3.4 where it did not hold", o, out[i]);
			values++;
		}
		check(h == counted, "random: held counted", h, counted);
		check(out[n] == 0x5A5A && out[n + 1] == 0x5A5A, "random: nothing past n", out[n], 0x5A5A);
	}
	printf("     random: %ld values in 40,000 blocks of 2 to %d, shifts 0 to 8\n", values, AUDIO_MIX_MAX);

	/* 3: the default level can hold nothing */
	{
		long hi = one(32767, 32767, 0xFF, LEVEL, &held);
		check(held == 0 && hi == (32767 + 32767 + 127 * 32) / (1 << LEVEL),
		      "default level: the largest sum, not held", hi, 0);
		long lo = one(-32768, -32768, 0x00, LEVEL, &held);
		check(held == 0 && lo == (-32768 - 32768 - 128 * 32) / (1 << LEVEL),
		      "default level: the smallest sum, not held", lo, 0);
		printf("  3. the default level, %d: the largest and smallest sums the devices can make "
		       "come out %ld and %ld (%.1f and %.1f dB of full scale), nothing held\n", LEVEL,
		       hi, lo, 20 * log10(hi / 32767.0), 20 * log10(lo / -32768.0));
		/* and it is the loudest such level (board_dosboard.h says why it
		   was chosen), and louder than tiny386's own 4 */
		check(LEVEL < 4, "default level: louder than tiny386's 4", LEVEL, 4);
		if (LEVEL > 0) {
			int h1, h2;
			one(32767, 32767, 0xFF, LEVEL - 1, &h1);
			one(-32768, -32768, 0x00, LEVEL - 1, &h2);
			check(h1 && h2, "default level: one step louder can be held", h1 + h2, 2);
			printf("     one step louder, %d, the same sums are held at full scale: the default is "
			       "the loudest level that cannot clip\n", LEVEL - 1);
		}
		/* and where v3.4 held at level 2: the SB16 and the AdLib each at
		   30,000 came out 8,191 (held at 32,767 before the / 4); now 15,000 */
		int h = 0;
		long o = v34(30000, 30000, -1, 2, &h);
		long n = one(30000, 30000, -1, 2, &held);
		check(o == 8191 && h == 1 && n == 15000 && held == 0, "level 2: two loud sources", n, o);
		/* and level 0: held at full scale, where a 16-bit sum would wrap */
		long w = one(30000, 30000, -1, 0, &held);
		int16_t wrapped = (int16_t) (uint16_t) (30000 + 30000);
		check(w == 32767 && held == 1, "level 0: held, not wrapped", w, wrapped);
		long w2 = one(-30000, -30000, 0x00, 0, &held);
		check(w2 == -32768 && held == 1, "level 0: held below too", w2, 0);
		printf("     two sources at 30,000: v3.4 at level 2 gave %ld (held before the level), "
		       "now %ld; at level 0 now %ld (a 16-bit sum would give %d)\n", o, n, w, wrapped);
	}

	/* 4: a lone source against v3.4's default level, 4 */
	{
		long worst = 0;
		const long ratio = 1L << (4 - LEVEL);
		for (int v = -32768; v <= 32767; v++) {
			long now = one(v, 0, -1, LEVEL, &held);
			int h = 0;
			long then = v34(v, 0, -1, 4, &h);
			check(now == v / (1 << LEVEL), "lone source: v / 2^level", now, v / (1 << LEVEL));
			long d = labs(now - ratio * then);
			if (d > worst)
				worst = d;
		}
		check(worst < ratio, "lone source: 2^(4 - level) times v3.4's, within rounding", worst, ratio);
		printf("  4. a lone source at level %d: exactly v / %d for all 65,536 values, %ld times "
		       "v3.4's v / 16 give or take %ld (rounding): %.1f dB louder\n", LEVEL, 1 << LEVEL,
		       ratio, worst, 20 * log10((double) ratio));
	}

	/* 5: the devices */
	printf("  5. the devices at their loudest, 2 s at %d Hz for each level:\n", AUDIO_RATE);
	loudest("SB16 alone: full-scale 16-bit stereo square", 1, 0, 0);
	loudest("AdLib alone: nine channels in step, full volume", 0, 1, 0);
	loudest("SB16, AdLib and PC speaker together", 1, 1, 1);

	printf("audio_mix_test: %ld checks, %ld failed: %s\n", checks, failures, failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
