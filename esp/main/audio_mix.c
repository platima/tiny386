/*
 * The sound mix of the ESP builds: audio_mix.h says what and why.
 */
#include "audio_mix.h"
#include <string.h>
#include "../../adlib.h"
#include "../../sb16.h"
#include "../../pcspk.h"
#ifndef AUDIO_MIX_HOST
#include "../../pc.h"
#endif

/* s / 2^shift, towards zero as C's / is: an arithmetic shift rounds down,
   so a negative sum is first raised by 2^shift - 1 */
static inline int32_t level(int32_t s, int shift)
{
	if (s < 0)
		s += ((int32_t) 1 << shift) - 1;
	return s >> shift;
}

int audio_mix_block(int16_t *out, const int16_t *sb, const int16_t *opl,
		    const uint8_t *spk, int n, int shift)
{
	int held = 0;

	if (shift < 0)
		shift = 0;
	if (shift > AUDIO_MIX_SHIFT_MAX)
		shift = AUDIO_MIX_SHIFT_MAX;
	for (int i = 0; i < n; i++) {
		/* at most 32767 + 32767 + 127 x 32 = 69,598 and at least
		   -32768 - 32768 - 128 x 32 = -69,632: far inside 32 bits */
		int32_t s = (int32_t) sb[i] + opl[i / 2];
		if (spk)
			s += ((int32_t) spk[i / 2] - 0x80) * 32;
		s = level(s, shift);
		if (s > INT16_MAX) {
			s = INT16_MAX;
			held++;
		} else if (s < INT16_MIN) {
			s = INT16_MIN;
			held++;
		}
		out[i] = (int16_t) s;
	}
	return held;
}

int audio_mix_devices(int16_t *out, int n, int shift, void *adlib, void *sb16,
		      void *pcspk)
{
	int16_t sb[AUDIO_MIX_MAX];
	int16_t opl[AUDIO_MIX_MAX / 2];
	uint8_t spk[AUDIO_MIX_MAX / 2];
	const uint8_t *s = NULL;

	if (n > AUDIO_MIX_MAX)
		n = AUDIO_MIX_MAX;
	n &= ~1;
	/* nothing to make: no device is asked and out is left as it was.
	   i2s.c always asks for MIXER_BUF_LEN; this keeps the function safe on
	   its own, since memset() would take a negative n as a huge size (v3.5
	   review, N2) */
	if (n <= 0)
		return 0;
	/* pc.c's mixer_callback() with its free = 2 n bytes: the AdLib's mono
	   into n bytes, the SB16's stereo into 2 n, the PC speaker's 8 bits
	   into n / 2, each device leaving what it does not fill as it was */
	memset(opl, 0, n);
	memset(sb, 0, 2 * n);
	adlib_callback(adlib, (uint8_t *) opl, n);
	sb16_audio_callback(sb16, (uint8_t *) sb, 2 * n);
	if (pcspk_get_active_out(pcspk)) {
		memset(spk, 0x80, n / 2);
		pcspk_callback(pcspk, spk, n / 2);
		s = spk;
	}
	return audio_mix_block(out, sb, opl, s, n, shift);
}

#ifndef AUDIO_MIX_HOST
int audio_mix_pc(int16_t *out, int n, int shift, void *opaque)
{
	PC *pc = opaque;

	return audio_mix_devices(out, n, shift, pc->adlib, pc->sb16, pc->pcspk);
}
#endif
