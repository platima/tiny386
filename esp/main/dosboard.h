/* The DOS board's own start-up (dosboard.c) and its settings. */
#ifndef DOSBOARD_H
#define DOSBOARD_H

#include <stdint.h>

/* [dosboard] in tiny386.ini; every key is optional */
typedef struct {
	int pclk_phase;          /* pclk_phase = 0..7: MS7210 0x1281 bits 2:0 (4 = PCLK inverted) */
	int pclk_neg;            /* pclk_neg = 0|1: S31 clocks data out on the falling PCLK edge */
	int hdmi_mode;           /* hdmi_mode = auto|hdmi|dvi */
	int audio;               /* hdmi_audio = 0|1 */
	int bounce_px;           /* lcd_bounce_px = 0 or a divisor of 480000, such as 8000 */
	int test_card_ms;        /* test_card_ms = how long the colour bars stay at start, 0 = none */
	int ch9350_state;        /* ch9350_state = 2|0 */
	int kbd_zero_guard_ms;   /* kbd_zero_guard_ms = 0 (off) or a few ms (ch9350.h) */
} dosboard_cfg_t;

extern dosboard_cfg_t dosboard_cfg;

void board_early_init(void);         /* app_main, first thing: board check and pins */
void dosboard_load_config(void);     /* after the card is mounted */
int dosboard_video_start(void);      /* after the RGB panel runs: MS7210 set-up, hot plug task */
void dosboard_test_card(uint16_t *fb, int w, int h);

#endif /* DOSBOARD_H */
