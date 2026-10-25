#ifdef LCD_DOUBLE_BUFFER
/*
 * v4.6: double buffering's second frame buffer, made with the panel at
 * power-on ([esp] double_buffer = 1, the Korvo-1).
 *
 * v4.4 and v4.5 made it once the ini had been read: the panel made at
 * power-on with one frame buffer was deleted, and started again with two in
 * the room it left (lcd_double_start()).  On Plat's board that start
 * failed, with two frame buffers and with one, after the emulator's pool
 * and the rest had taken their memory (v4.5: the load access fault at
 * 0xFA50).  Why is not known; memory, in PSRAM or internal DMA memory, is
 * the likeliest.  So both frame buffers are now made with the panel at
 * power-on, before anything else can take memory, and the panel is never
 * started again.  The ini that asks for them is only known after the boot
 * menu, so:
 *
 *   1. A start whose ini has double_buffer = 1, with the panel made with
 *      one frame buffer, writes a note in the LP SRAM naming that ini and
 *      restarts (esp_restart()), before the PC is built and before any card
 *      partition is handed to it.
 *   2. The next start takes the note and clears it, first thing in
 *      app_main(); makes the panel with two frame buffers (lcd_init_two());
 *      takes the ini named without the boot menu; and turns double
 *      buffering on.  If the panel will not start with two, it starts with
 *      one, as with double_buffer = 0, and a yellow line says why, with
 *      ESP-IDF's error name.
 *
 * Never more than one restart in a row: a start restarts only if it found
 * no note at all and was not itself started by a software restart, so a
 * note the LP SRAM did not keep cannot bring a second one.  While
 * lcd_init_two() runs, the note says so (DBUF_NOTE_TRYING): a start that
 * finds that, after a reset in the middle of it, has one frame buffer and
 * does not restart.  Where a restart would start Linux (tiny386 the dual
 * boot's test app, started with VOL+ held through RESET: FLASH.txt 2),
 * there is no restart: one frame buffer, and a yellow line says why.
 *
 * With double_buffer = 0 and no note, a start is as v4.5's: lcd_init() and
 * no line from here.
 *
 * Plain C over ESP-IDF's calls: tests/host/display builds it on a PC, with
 * its restarts in child processes and the note carried between them.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "common.h"
#include "console.h"
#include "dbuf_boot.h"
#include "lcd.h"
#include "speed.h"

#define FB_TAKE (((long) LCD_WIDTH * LCD_HEIGHT * 2 + 4095) / 4096 * 4096)

RTC_NOINIT_ATTR struct dbuf_note dbuf_note;

/* what this start found */
enum { FOUND_NONE, FOUND_TWO, FOUND_TRYING, FOUND_BAD };
static int s_found;
static struct dbuf_note s_last;         /* the note, if FOUND_TWO or FOUND_TRYING */
static esp_reset_reason_t s_reset;
/* memory before lcd_init_two(), in KiB, for the yellow line */
static unsigned s_psram_free, s_psram_block, s_dma_free;

static uint32_t note_check(const struct dbuf_note *n)
{
	const uint8_t *p = (const uint8_t *) n;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < offsetof(struct dbuf_note, check); i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void copy_str(char *to, size_t size, const char *from)
{
	size_t n = strlen(from);
	if (n >= size)
		n = size - 1;
	memcpy(to, from, n);
	to[n] = 0;
}

static void note_write(uint32_t stage, const char *dir, const char *name)
{
	struct dbuf_note n;
	memset(&n, 0, sizeof(n));
	n.magic = DBUF_NOTE_MAGIC;
	n.magic_inv = ~DBUF_NOTE_MAGIC;
	n.form = DBUF_NOTE_FORM;
	n.stage = stage;
	copy_str(n.dir, sizeof(n.dir), dir);
	copy_str(n.name, sizeof(n.name), name);
	n.check = note_check(&n);
	dbuf_note = n;
}

static int classify(const struct dbuf_note *n)
{
	if (n->magic != DBUF_NOTE_MAGIC || n->magic_inv != ~DBUF_NOTE_MAGIC)
		return FOUND_NONE;
	if (n->form != DBUF_NOTE_FORM || n->check != note_check(n) ||
	    (n->stage != DBUF_NOTE_TWO && n->stage != DBUF_NOTE_TRYING) ||
	    !memchr(n->dir, 0, sizeof(n->dir)) || !memchr(n->name, 0, sizeof(n->name)) ||
	    !n->name[0])
		return FOUND_BAD;
	return n->stage == DBUF_NOTE_TWO ? FOUND_TWO : FOUND_TRYING;
}

void dbuf_boot_take(void)
{
	struct dbuf_note n = dbuf_note;
	memset(&dbuf_note, 0, sizeof(dbuf_note));
	s_reset = esp_reset_reason();
	s_found = classify(&n);
	if (s_found == FOUND_TWO || s_found == FOUND_TRYING)
		s_last = n;
}

void *dbuf_boot_panel(void)
{
	if (s_found != FOUND_TWO)
		return lcd_init();
	s_psram_free = (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
	s_psram_block = (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024);
	s_dma_free = (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024);
	/* a reset in what follows is seen by the next start, which then does
	   not try again */
	note_write(DBUF_NOTE_TRYING, s_last.dir, s_last.name);
	void *fb = lcd_init_two();
	memset(&dbuf_note, 0, sizeof(dbuf_note));
	return fb;
}

void dbuf_boot_banner(void)
{
	int two, one;
	switch (s_found) {
	case FOUND_TWO:
		lcd_double_why(&two, &one);
		if (lcd_double_made())
			console_puts(CON_INFO, "This start is tiny386's own restart for double_buffer = 1: "
				     "the panel was started with two frame buffers, before anything else.");
		else if (globals.panel_fb)
			console_printf(CON_INFO, "This start is tiny386's own restart for double_buffer = 1, "
				       "but the panel would not start with two frame buffers (%s); it "
				       "started with one.", esp_err_to_name(two));
		else
			console_printf(CON_INFO, "This start is tiny386's own restart for double_buffer = 1, "
				       "but the panel would not start with two frame buffers (%s), nor with "
				       "one (%s).", esp_err_to_name(two), esp_err_to_name(one));
		break;
	case FOUND_TRYING:
		console_printf(CON_INFO, "The last start was cut short while it started the panel with two "
			       "frame buffers for double_buffer = 1 (%s): this start has one, and does not "
			       "restart.", s_last.name);
		break;
	case FOUND_BAD:
		console_puts(CON_INFO, "The LP SRAM held a note of a restart for double buffering that was "
			     "not whole, or not this version's: it was ignored, and tiny386 does not "
			     "restart at this start.");
		break;
	default:
		break;
	}
}

int dbuf_boot_pick(const struct setup_list *l)
{
	if (s_found != FOUND_TWO)
		return -1;
	if (!strcmp(l->dir, s_last.dir))
		for (int i = 0; i < l->n; i++)
			if (!strcmp(l->ini[i].name, s_last.name)) {
				console_printf(CON_GOOD, "Starting %s, as chosen before the restart (no "
					       "menu).", s_last.name);
				return i;
			}
	console_printf(CON_INFO, "%s/%s, chosen before the restart, is not there now: the menu as "
		       "usual.", s_last.dir, s_last.name);
	return -1;
}

/* true if a restart starts this app again: the one the bootloader starts
   with no key held (the factory app: tiny386 alone, or the dual boot's
   tiny386-default table), not the dual boot's test app, which only VOL+
   held through the reset starts (FLASH.txt 2) */
static bool restart_comes_back(void)
{
	const esp_partition_t *p = esp_ota_get_running_partition();
	return p && p->subtype != ESP_PARTITION_SUBTYPE_APP_TEST;
}

static void restart_for_two(const char *dir, const char *name) __attribute__((noreturn));
static void restart_for_two(const char *dir, const char *name)
{
	note_write(DBUF_NOTE_TWO, dir, name);
	console_printf(CON_INFO, "double_buffer = 1: tiny386 starts again now, once, so that the panel "
		       "gets both frame buffers before anything else takes memory. The next start takes "
		       "%s without the menu.", name);
	/* the line out of the UART, and a moment to read it on the panel */
	fflush(stdout);
	vTaskDelay(pdMS_TO_TICKS(1000));
	esp_restart();
}

void dbuf_boot_start(int want, const char *dir, const char *name, long pool)
{
	speed_set_double(want);
	bool made = lcd_double_made();
	if (!want) {
		/* double_buffer = 0 with no note: as v4.5, nothing said */
		if (made)
			console_puts(CON_INFO, "double_buffer = 0, but the panel has two frame buffers at "
				     "this start all the same (made for the ini chosen before the "
				     "restart): pictures are drawn into the one on show, and Ctrl+Alt+F9 "
				     "turns double buffering on and off at once.");
		return;
	}
	if (made) {
		lcd_double_set(true);
		console_printf(CON_INFO, "double_buffer = 1: two frame buffers of %ld KiB, both made with "
			       "the panel at power-on, before the emulator's memory (%ld KiB); pictures go "
			       "to the panel at its frame.", (long) LCD_WIDTH * LCD_HEIGHT * 2 / 1024,
			       pool / 1024);
		return;
	}
	/* not made: the setting stays 1 for Ctrl+Alt+F9, so that one press
	   saves 0, and the speed screen says it was not made */
	speed_double_refused();
	if (!globals.panel_fb) {
		console_puts(CON_WARN, "double_buffer = 1: there is no panel, so no second frame buffer.");
		return;
	}
	switch (s_found) {
	case FOUND_TWO: {
		int two, one;
		lcd_double_why(&two, &one);
		console_printf(CON_WARN, "double_buffer = 1: one frame buffer at this start. The panel would "
			       "not start with two at power-on (%s), with PSRAM free %u KiB (largest block "
			       "%u KiB) and internal DMA memory free %u KiB; it started with one, as with "
			       "double_buffer = 0. Set double_buffer = 0 in %s, or press Ctrl+Alt+F9 once "
			       "the PC runs, which saves it.", esp_err_to_name(two), s_psram_free,
			       s_psram_block, s_dma_free, name);
		return;
	}
	case FOUND_TRYING:
		console_printf(CON_WARN, "double_buffer = 1: one frame buffer at this start. The last start "
			       "was cut short while it made the panel with two, so this start did not try "
			       "again. Set double_buffer = 0 in %s, or press Ctrl+Alt+F9 once the PC runs, "
			       "which saves it; RESET tries once more.", name);
		return;
	case FOUND_BAD:
		console_puts(CON_WARN, "double_buffer = 1: one frame buffer at this start. The note of a "
			     "restart in the LP SRAM was not whole, so tiny386 does not restart now. "
			     "Press RESET to try again.");
		return;
	default:
		break;
	}
	if (s_reset == ESP_RST_SW) {
		console_puts(CON_WARN, "double_buffer = 1: one frame buffer at this start. It followed a "
			     "software restart with no note of tiny386's own (Linux's reboot does that, "
			     "or the LP SRAM did not keep the note), and tiny386 restarts for double "
			     "buffering only once. Press RESET to try again.");
		return;
	}
	if (pool - FB_TAKE < 2 * 1024 * 1024) {
		console_puts(CON_WARN, "double_buffer = 1: no room for a second frame buffer; one is used.");
		return;
	}
	if (!restart_comes_back()) {
		console_printf(CON_WARN, "double_buffer = 1: one frame buffer. The second is made with the "
			       "panel at power-on, by one restart, and tiny386 is the dual boot's VOL+ app "
			       "here, so a restart would start Linux. Set double_buffer = 0 in %s to drop "
			       "this line. With the tiny386-default table (FLASH.txt) it can be made.",
			       name);
		return;
	}
	restart_for_two(dir, name);
}

#endif /* LCD_DOUBLE_BUFFER */
