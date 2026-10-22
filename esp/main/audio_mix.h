/*
 * The sound mix of the ESP builds (i2s.c), in place of pc.c's
 * mixer_callback(), which the other front ends keep.  The same three
 * devices, asked in the same way and order, but their samples are added in
 * 32 bits, the level is applied (divided by 2 to the power `shift`, towards
 * zero, as tiny386's own buf[i] / 16 was), and only then is the result held
 * to 16 bits.  pc.c holds the sum to 16 bits before the level, so at a level
 * of 1/4 two loud sources together were clipped at a quarter of full scale;
 * here they come through whole, and a sum still too big after the level is
 * held at full scale, never wrapped round to the other sign.
 *
 * audio_mix_block() is plain C, tested on the host
 * (tests/host/dosboard/audio_mix_test.c, built with AUDIO_MIX_HOST).
 */
#ifndef AUDIO_MIX_H
#define AUDIO_MIX_H

#include <stdint.h>

/* the most 16-bit values (interleaved stereo) one call makes */
#define AUDIO_MIX_MAX 128
/* the largest level shift taken; more is taken as this */
#define AUDIO_MIX_SHIFT_MAX 15

/* n values (n even, at most AUDIO_MIX_MAX) of interleaved stereo into out,
   from sb (n values: the SB16's stereo), opl (n / 2: the AdLib's mono) and
   spk (n / 2: the PC speaker's unsigned 8 bits about 0x80, scaled by 32 as
   pc.c does, or NULL while it is off).  Returns how many of the n values
   were held at full scale. */
int audio_mix_block(int16_t *out, const int16_t *sb, const int16_t *opl,
		    const uint8_t *spk, int n, int shift);

/* One block from the three devices themselves (AdlibState, SB16State,
   PCSpkState), asked as pc.c's mixer_callback() asks them, mixed by
   audio_mix_block().  Returns what that returns. */
int audio_mix_devices(int16_t *out, int n, int shift, void *adlib, void *sb16,
		      void *pcspk);

#ifndef AUDIO_MIX_HOST
/* The same from a PC (pc.h), for i2s.c, which does not include pc.h. */
int audio_mix_pc(int16_t *out, int n, int shift, void *pc);
#endif

#endif /* AUDIO_MIX_H */
