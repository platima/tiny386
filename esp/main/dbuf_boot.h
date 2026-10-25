/*
 * v4.6: double buffering's two frame buffers made with the panel at
 * power-on, on a board that has double buffering (LCD_DOUBLE_BUFFER: the
 * Korvo-1).  dbuf_boot.c says how; esp_main.c calls these in this order.
 */
#ifndef DBUF_BOOT_H
#define DBUF_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include "setup.h"

/* The note a start leaves the next one in the LP SRAM, which a restart
   does not clear (RTC_NOINIT_ATTR, as console.c's ring and the status
   LED's record). */
#define DBUF_NOTE_MAGIC 0x44425546u     /* "DBUF" */
/* this layout (1: a 20-byte one, 2: this one without why_two to dma_free;
   neither was released) */
#define DBUF_NOTE_FORM 3
#define DBUF_NOTE_TWO 1                 /* the next start: two frame buffers, and this ini */
#define DBUF_NOTE_TRYING 2              /* lcd_init_two() under way: a reset in it leaves this */
/* v4.6, after its review: the panel started with neither two frame buffers
   nor one after the restart for them; the next start makes it as lcd_init()
   does, takes this ini, never restarts, and says why in yellow */
#define DBUF_NOTE_ONE 3
struct dbuf_note {
	uint32_t magic;
	uint32_t magic_inv;             /* ~magic */
	uint32_t form;                  /* DBUF_NOTE_FORM */
	uint32_t stage;                 /* DBUF_NOTE_TWO, DBUF_NOTE_TRYING or DBUF_NOTE_ONE */
	char dir[sizeof(((struct setup_list *) 0)->dir)];      /* the ini's folder */
	char name[SETUP_NAME_MAX];      /* the ini */
	/* DBUF_NOTE_ONE: ESP-IDF's errors of the start with two frame buffers
	   and of the start with one after it, and the memory free before them,
	   in KiB (0 in the other stages) */
	int32_t why_two, why_one;
	uint32_t psram_free, psram_block, dma_free;
	uint32_t check;                 /* FNV-1a of everything before it */
};
/* Not static: tests/host/display carries it from one pretend start to the
   next, as the LP SRAM does. */
extern struct dbuf_note dbuf_note;

/* First thing in app_main(): the note taken and cleared. */
void dbuf_boot_take(void);
/* In place of lcd_init(): the panel, with two frame buffers if the note
   asks for them (lcd_init_two()), else with one (lcd_init()).  Returns the
   frame buffer on show, NULL if there is no panel.  If the note asked for
   two and the panel started with neither two nor one, writes a
   DBUF_NOTE_ONE note and restarts (does not return), where a restart comes
   back to tiny386. */
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
/* true if a restart starts tiny386 again (it is the app the bootloader
   starts with no key held), so that the second frame buffer can be made;
   false for the dual boot's VOL+ app under the Linux-default table.  For
   speed.c's Ctrl+Alt+F9 line. */
bool dbuf_boot_comes_back(void);

#endif /* DBUF_BOOT_H */
