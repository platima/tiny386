/*
 * v4.6: double buffering's two frame buffers made with the panel at
 * power-on, on a board that has double buffering (LCD_DOUBLE_BUFFER: the
 * Korvo-1).  dbuf_boot.c says how; esp_main.c calls these in this order.
 */
#ifndef DBUF_BOOT_H
#define DBUF_BOOT_H

#include <stdint.h>
#include "setup.h"

/* The note a start leaves the next one in the LP SRAM, which a restart
   does not clear (RTC_NOINIT_ATTR, as console.c's ring and the status
   LED's record). */
#define DBUF_NOTE_MAGIC 0x44425546u     /* "DBUF" */
#define DBUF_NOTE_FORM 2                /* this layout (1: a 20-byte one never released) */
#define DBUF_NOTE_TWO 1                 /* the next start: two frame buffers, and this ini */
#define DBUF_NOTE_TRYING 2              /* lcd_init_two() under way: a reset in it leaves this */
struct dbuf_note {
	uint32_t magic;
	uint32_t magic_inv;             /* ~magic */
	uint32_t form;                  /* DBUF_NOTE_FORM */
	uint32_t stage;                 /* DBUF_NOTE_TWO or DBUF_NOTE_TRYING */
	char dir[sizeof(((struct setup_list *) 0)->dir)];      /* the ini's folder */
	char name[SETUP_NAME_MAX];      /* the ini */
	uint32_t check;                 /* FNV-1a of everything before it */
};
/* Not static: tests/host/display carries it from one pretend start to the
   next, as the LP SRAM does. */
extern struct dbuf_note dbuf_note;

/* First thing in app_main(): the note taken and cleared. */
void dbuf_boot_take(void);
/* In place of lcd_init(): the panel, with two frame buffers if the note
   asks for them (lcd_init_two()), else with one (lcd_init()).  Returns the
   frame buffer on show, NULL if there is no panel. */
void *dbuf_boot_panel(void);
/* After banner(): a line on what the note was, if there was one. */
void dbuf_boot_banner(void);
/* In choose_ini(): the entry of l the note names, to start without the
   boot menu, or -1 (no note, or that ini not there); says which. */
int dbuf_boot_pick(const struct setup_list *l);
/* double_buffer_start(): want is the ini's double_buffer, dir and name the
   ini, pool the emulator's memory in bytes (psram_len, which never pays
   for the second frame buffer here: made at power-on, it was taken before
   the pool was sized).  Turns double buffering on, or says why it is not
   made; or writes the note and restarts (does not return). */
void dbuf_boot_start(int want, const char *dir, const char *name, long pool);

#endif /* DBUF_BOOT_H */
