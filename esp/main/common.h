#ifndef COMMON_H
#define COMMON_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

struct Globals {
	void *pc;
	void *kbd;
	void *mouse;
	void *panel;
	void *panel_fb;   /* RGB panel DMA frame buffer (NULL if not used) */
};

extern EventGroupHandle_t global_event_group;
extern struct Globals globals;

/* i2s.c, set by a board before i2s_main() runs (dosboard.c, from
   dosboard.ini): the I2S slot width (16 or 32, the samples are 16 bits
   either way) and the samples divided by 2 to the power audio_shift (4,
   tiny386's own /16, unless a board says otherwise). */
extern int i2s_slot_bits;
extern int i2s_audio_shift;

#endif /* COMMON_H */
