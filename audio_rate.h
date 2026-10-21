#ifndef AUDIO_RATE_H
#define AUDIO_RATE_H

/*
 * The emulator's audio output rate in Hz.  The PC speaker (pcspk.c), the
 * AdLib synthesiser (adlib.c), the SB16's resampler (sb16.c) and the ESP-IDF
 * I2S output (esp/main/i2s.c) all work at this one rate, so it must change
 * in all of them together.  A board header may set it first: the DOS board
 * uses 48000, because its HDMI transmitter sends 44.1 kHz audio with its
 * 48 kHz settings (FIRMWARE-PLAN 5.6).
 */
#ifndef AUDIO_RATE
#define AUDIO_RATE 44100
#endif

#endif /* AUDIO_RATE_H */
