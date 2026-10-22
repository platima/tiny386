/* The DOS board's own start-up (dosboard.c) and its settings. */
#ifndef DOSBOARD_H
#define DOSBOARD_H

#include <stdint.h>

/* [dosboard] in dosboard.ini, beside the inis on the card
   (/sdcard/tiny386/dosboard.ini, else /sdcard/dosboard.ini); read before
   the panel starts, so not in the inis the boot menu offers.  Every key is
   optional. */
typedef struct {
	int pclk_phase;          /* pclk_phase = 0..7: MS7210 0x1281 bits 2:0 (4 = PCLK inverted) */
	int pclk_neg;            /* pclk_neg = 0|1: S31 clocks data out on the falling PCLK edge */
	int hdmi_mode;           /* hdmi_mode = auto|hdmi|dvi */
	int audio;               /* hdmi_audio = 0|1 */
	int rb_swap;             /* rb_swap = 0|1: MS7210 0x1201 bit 4, red and blue swapped */
	int bounce_px;           /* lcd_bounce_px = 0 or a divisor of 480000 up to 16000, such as 8000 */
	int test_card_ms;        /* test_card_ms = how long the colour bars stay at start, 0 = none */
	int ch9350_state;        /* ch9350_state = 2|0 */
	int kbd_zero_guard_ms;   /* kbd_zero_guard_ms = 0 (off) or a few ms (ch9350.h) */
	int i2s_slot_bits;       /* i2s_slot_bits = 16|32: I2S slot width, 16-bit samples in it */
	int audio_shift;         /* audio_shift = 0..8: samples divided by 2 to this; default
	                            AUDIO_SHIFT_DEFAULT (2), 4 = tiny386's own */
} dosboard_cfg_t;

/* the largest lcd_bounce_px: 20 rows, two buffers of 32,000 bytes in
   internal RAM */
#define DOSBOARD_BOUNCE_MAX_PX (LCD_WIDTH * 20)

extern dosboard_cfg_t dosboard_cfg;

/* app_main(), first thing: the board check (MS7210 chip ID at I2C 0x59),
   then the pins that are new on this board.  Never returns on another
   board. */
void board_early_init(void);
/* lcd_init(), before the panel: mounts the card if it can and reads
   dosboard.ini */
void dosboard_load_config(void);
/* lcd_init(), once the RGB output runs: MS7210 set-up, hot plug task */
int dosboard_video_start(void);
void dosboard_test_card(uint16_t *fb, int w, int h);

/* A line for the UART now and, until the boot console is up, kept to be
   shown on it then (board_console_ready()). */
void dosboard_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* app_main(), once the boot console is up: the lines kept so far. */
void board_console_ready(void);

#endif /* DOSBOARD_H */
