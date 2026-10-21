/*
 * Host check that the emulator's audio follows AUDIO_RATE (FIRMWARE-PLAN 5.6).
 *
 * Built twice by run.sh, with -DAUDIO_RATE=44100 and -DAUDIO_RATE=48000,
 * against the tree's own pcspk.c, adlib.c and fmopl.c, and with sb16.c
 * included here so that its state can be set up directly.  Each part makes
 * one second of output at AUDIO_RATE and measures what a listener would
 * hear: the pitch must be the same at both rates.
 *
 *   PC speaker: PIT channel 2 count 1193, 1193182 / 1193 = 1000.15 Hz.
 *   AdLib: OPL2 channel 0, F-number 580, block 4, one sine operator:
 *          580 x 49716 / 2^16 = 440.0 Hz (the OPL2's own formula).
 *   SB16: 22,050 Hz 16-bit mono from the guest, resampled to AUDIO_RATE:
 *          output frames per input sample must be AUDIO_RATE / 22050.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "audio_rate.h"
/* run.sh points this at a patched copy to try a resampler change */
#ifndef SB16_C
#define SB16_C "sb16.c"
#endif
#include SB16_C
#include "pcspk.h"
#include "adlib.h"

/* ==== what the three need from the rest of the PC ==== */

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

static int failures;

/* Input samples the SB16 takes for one second of output at AUDIO_RATE,
   16-bit mono at guest_rate, asked for in chunks of `frames` stereo frames
   (0 = 64, the ESP mixer's). */
static double sb16_taken(int guest_rate, int frames)
{
	static SB16State *s;
	static int16_t out[2 * 1024];
	long taken = 0, made = 0;

	if (!s)
		s = sb16_new(0x220, 5, NULL, NULL, NULL, set_irq);
	if (!frames)
		frames = 64;
	s->fmt = AUDIO_FORMAT_S16;
	s->fmt_stereo = 0;
	s->freq = guest_rate;
	s->active_out = 1;
	s->audio_p = s->audio_q = 0;
	while (made < AUDIO_RATE) {
		unsigned before = s->audio_p;
		s->audio_q = s->audio_p + AUDIO_BUF_LEN;          /* the buffer always full */
		sb16_audio_callback(s, (uint8_t *)out, frames * 4);  /* bytes, s16 stereo */
		taken += (s->audio_p - before) / 2;
		made += frames;
	}
	return taken * (double)AUDIO_RATE / made;
}

static void report(const char *what, double got, double want, double tol)
{
	int ok = fabs(got - want) <= tol;
	printf("  %-44s %10.2f (expect %.2f +- %.2f) %s\n", what, got, want, tol, ok ? "ok" : "FAIL");
	if (!ok)
		failures++;
}

int main(void)
{
	printf("AUDIO_RATE %d\n", AUDIO_RATE);

	/* PC speaker: count sign changes over one second of output */
	{
		PCSpkState *s = pcspk_init((PITState *)1);
		uint8_t buf[256];
		int samples = 0, changes = 0, last = -1;

		pcspk_ioport_write(s, 3);                /* gate and data on */
		while (samples < AUDIO_RATE) {
			int n = AUDIO_RATE - samples < 256 ? AUDIO_RATE - samples : 256;
			memset(buf, 0x80, sizeof(buf));
			pcspk_callback(s, buf, n);
			for (int i = 0; i < n; i++) {
				int hi = buf[i] < 0x80;          /* 0x20 or 0xE0 */
				if (last >= 0 && hi != last)
					changes++;
				last = hi;
			}
			samples += n;
		}
		report("PC speaker, count 1193 (Hz)", changes / 2.0, 1193182.0 / 1193, 2.0);
	}

	/* AdLib: count rising zero crossings from 0.1 s to 1.1 s */
	{
		AdlibState *a = adlib_new();
		static const uint8_t regs[][2] = {
			{ 0x01, 0x20 },          /* waveform select enable */
			{ 0x20, 0x01 },          /* modulator: MULT 1 */
			{ 0x23, 0x21 },          /* carrier: sustain, MULT 1 */
			{ 0x40, 0x3F },          /* modulator silent */
			{ 0x43, 0x00 },          /* carrier loudest */
			{ 0x60, 0xF0 },          /* attack 15, decay 0 */
			{ 0x63, 0xF0 },
			{ 0x80, 0x00 },          /* sustain level 0, release 0 */
			{ 0x83, 0x00 },
			{ 0xC0, 0x00 },          /* FM, no feedback */
			{ 0xA0, 580 & 0xFF },    /* F-number low */
			{ 0xB0, 0x20 | (4 << 2) | (580 >> 8) },   /* key on, block 4 */
		};
		int16_t buf[512];
		long n = 0, first = AUDIO_RATE / 10, last_n = first + AUDIO_RATE;
		int crossings = 0, prev = 0;

		for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			adlib_write(a, 0x388, regs[i][0]);
			adlib_write(a, 0x389, regs[i][1]);
		}
		while (n < last_n) {
			memset(buf, 0, sizeof(buf));
			adlib_callback(a, (uint8_t *)buf, sizeof(buf));   /* bytes; s16 mono */
			for (int i = 0; i < 512 && n < last_n; i++, n++) {
				if (n >= first && prev < 0 && buf[i] >= 0)
					crossings++;
				prev = buf[i];
			}
		}
		report("AdLib, F-number 580, block 4 (Hz)", crossings, 580 * 49716.0 / 65536, 2.0);
	}

	/* SB16: a guest playing at half the output rate must be taken at
	   exactly half the output rate.  If sb16.c still resampled to 44,100,
	   a 24,000 Hz guest would be taken at 44100 x 6 / 11 per second. */
	{
		double got = sb16_taken(AUDIO_RATE / 2, 0);
		report("SB16 guest at AUDIO_RATE / 2, samples a second", got, AUDIO_RATE / 2.0, 1.0);
	}

	/* SB16 at the guest rates DOS programs use, in the chunks the ESP
	   mixer asks for (MIXER_BUF_LEN 128: 64 stereo frames), and in bigger
	   ones.  Not a pass or fail: it shows how far the resampler's own
	   rate approximation is off (sb16.c approx_frac, denominators up to 8). */
	{
		static const int rates[] = { 8000, 11025, 22050, 22222, 32000, 44100, 45454 };
		printf("  SB16 accuracy, 16-bit mono, input taken per second of output:\n");
		printf("    guest Hz   64-frame chunks          1024-frame chunks\n");
		for (unsigned k = 0; k < sizeof(rates) / sizeof(rates[0]); k++) {
			double a = sb16_taken(rates[k], 64), b = sb16_taken(rates[k], 1024);
			printf("    %8d   %9.1f (%+6.2f %%)    %9.1f (%+6.2f %%)\n", rates[k],
			       a, 100.0 * (a - rates[k]) / rates[k], b, 100.0 * (b - rates[k]) / rates[k]);
		}
	}

	printf("audio_rate_test %d: %s\n", AUDIO_RATE, failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
