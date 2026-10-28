/*
 * Host test of the Korvo-1's start-up for double buffering and its drawing
 * loop (v4.6): esp/main/dbuf_boot.c, esp/main/lcd_korvo1.c and
 * esp/main/display.c's vga_task as the board builds them, vga.c as the board
 * builds it (test 3c's object), and a pretend ESP-IDF RGB panel driver
 * (below, with the headers in tests/host/display/stubs) whose starts fail,
 * or reset the chip, when a case says.
 *
 * v4.5 on the board, [esp] double_buffer = 1: a load access fault on core 0
 * in vga_draw_glyph9() under vga_text_refresh(), from vga_task, at mtval
 * 0xFA50.  That is the first cell of 80x25 text (720x400 in the middle of
 * the 800x480 panel: x 40, y 40, 1600 bytes a row) in a frame buffer at
 * address 0: 40 * 1600 + 40 * 2 = 64080 = 0xFA50.  v4.5 deleted the panel
 * and started it again with two frame buffers before the PC was built; on
 * the board that start failed, with two and with one, so there was no panel
 * frame buffer, and v4.5's vga_task gave the VGA code lcd_vga_present(),
 * NULL.  v4.6 makes both frame buffers with the panel at power-on, after one
 * restart (dbuf_boot.c), and never starts the panel again.
 *
 * Each case is a run of starts, each start a child process of its own, so
 * that every file starts from nothing as a restarted chip does; the note in
 * the LP SRAM (dbuf_note) is carried from one start to the next by the
 * parent, as the LP SRAM carries it, and so is the reset's cause (a
 * software restart after esp_restart()).  A start goes as app_main() goes:
 * the note taken (dbuf_boot_take()), the panel (dbuf_boot_panel()), the
 * banner's line, the emulator's pool sized from what PSRAM is left, the ini
 * chosen (dbuf_boot_pick(), else the boot menu's choice), double buffering
 * as the ini asks (dbuf_boot_start()), and if that does not restart, the
 * VGA built as screen_init() and pc_new() build it, then vga_task for some
 * pictures.  Checked: never more than one restart in a row, but for one more
 * when the panel started with neither two frame buffers nor one after the
 * first (the review of v4.6, display finding 2), and none after a crash, a
 * watchdog or a power dip, or after RESET on the crash screen (display
 * finding 1); the note cleared
 * as soon as it is taken, and after every start that does not restart; the
 * restart's note naming the ini chosen, and the second's both errors and the
 * memory figures; a note that is not whole, or of
 * another form, ignored with no restart; the panel's starts asked of the
 * driver (one frame buffer as v4.5 unless the note asks for two); the
 * console's lines, among them the yellow one with ESP-IDF's error names;
 * Ctrl+Alt+F9's setting; the pool 750 KiB smaller with both frame buffers;
 * and after each picture the buffer the VGA code is to draw into next is the
 * one lcd_korvo1.c names (the back one, or the only one), never NULL, never
 * one the driver has freed, and the PC's own when there is no panel.  A
 * fault in the drawing is caught and its address said: against v4.5's
 * display.c it is 0xfa50, the board's address in the cases with no panel.
 * That is the same arithmetic as the board's, not proof of the board's
 * path: any NULL frame buffer under 80x25 text on this panel faults there.
 *
 * With --trace, the last start of each case draws 12 pictures with 1 MiB of
 * video memory, Ctrl+Alt+F9 taken as speed.c takes it after pictures 1, 4,
 * 5, 8 and 10, and the guest switching to VBE 800x480 at 16 bits after
 * picture 2 and at 8 after picture 6; after every picture a line names the
 * buffer the VGA code draws into next and gives a hash of the buffer on
 * show.  Each case's first line starts "[panel]" or "[no panel]".  run.sh
 * compares the "[panel]" cases against display.c at v4.5, which must give
 * the same lines.
 *
 * Exit status 0 when every case passes.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_cache.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "pc.h"
#include "vga.h"
#include "pci.h"
#include "common.h"
#include "console.h"
#include "dbuf_boot.h"
#include "lcd.h"
#include "setup.h"
#include "speed.h"

void vga_task(void *arg);       /* display.c */

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define FB_BYTES ((size_t) W * H * 2)
#define VRAM (256 * 1024)
#define VRAM_TRACE (1024 * 1024)
#define PICTURES 6
#define PICTURES_TRACE 12
/* PSRAM free at power-on, before the panel: with one frame buffer the
   emulator's pool is then 15,104 KiB, as on Plat's board (photo 13) */
#define PSRAM_START (16110L * 1024)
#define KEEP_FREE (256 * 1024)  /* board_korvo1.h's PSRAM_KEEP_FREE */
#define DMA_FREE (120 * 1024)   /* internal DMA memory free, made up */

struct Globals globals;
EventGroupHandle_t global_event_group;

static int failures, checks;
static int turns;               /* pictures drawn in this start */
static bool trace;              /* --trace */
static int pictures = PICTURES; /* to draw in the last start */
static uint8_t vram[VRAM_TRACE + VGA_MEM_SLACK];

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		if (failures <= 8)
			printf("  FAIL: %s\n", what);
	}
}

/* ==== the clock ==== */

static int64_t now_us = 1000000;
uint32_t get_uticks(void)
{
	return (uint32_t) now_us;
}

int64_t esp_timer_get_time(void)
{
	return now_us;
}

uint32_t esp_cpu_get_cycle_count(void)
{
	static uint32_t c;
	return c += 1000;
}

esp_err_t esp_cache_msync(void *addr, size_t size, int flags)
{
	(void) addr; (void) size; (void) flags;
	return ESP_OK;
}

const char *esp_err_to_name(esp_err_t err)
{
	switch (err) {
	case ESP_OK: return "ESP_OK";
	case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
	case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
	case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
	default: return "ESP_FAIL";
	}
}

/* ==== what vga.c wants beside ==== */

void *pcmalloc(long size)
{
	return calloc(1, size);
}

PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
			       uint16_t vendor_id, uint16_t device_id,
			       uint8_t revision_id, uint16_t class_id)
{
	return NULL;
}

void pci_register_bar(PCIDevice *d, unsigned int bar_num, uint32_t size, int type,
		      void *opaque, void (*set_bar)(void *opaque, int bar_num,
						    uint32_t addr, bool enabled))
{
}

/* ==== a start's end, reported to the parent ==== */

enum { OUT_RAN = 1, OUT_RESTART, OUT_RESET };
struct report {
	int outcome;
	int failures, checks;
	struct dbuf_note note;          /* the LP SRAM as the start left it */
	int pick;                       /* the ini chosen */
};
static int report_fd = -1;
static int picked = -1;

static void start_end(int outcome) __attribute__((noreturn));
static void start_end(int outcome)
{
	struct report r;
	memset(&r, 0, sizeof(r));
	r.outcome = outcome;
	r.failures = failures;
	r.checks = checks;
	r.note = dbuf_note;
	r.pick = picked;
	if (write(report_fd, &r, sizeof(r)) != (ssize_t) sizeof(r))
		_exit(5);
	_exit(0);
}

/* ==== the pretend PSRAM and RGB panel driver ==== */

static long psram_free;         /* bytes: one free block, the largest */

size_t heap_caps_get_free_size(uint32_t caps)
{
	return caps & MALLOC_CAP_SPIRAM ? (size_t) psram_free : DMA_FREE;
}

size_t heap_caps_get_largest_free_block(uint32_t caps)
{
	return caps & MALLOC_CAP_SPIRAM ? (size_t) psram_free : DMA_FREE;
}

/* display.c's stacks line (v5.1): nothing is taken here, so the least is
   what is free */
size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
	return heap_caps_get_free_size(caps);
}

struct esp_lcd_panel_t {
	void *fbs[2];
	int nfbs;
	int shown;              /* the buffer the DMA sends */
	esp_lcd_rgb_panel_frame_buf_complete_cb_t on_switch;
};

static int results[4], nresult; /* what this start's driver starts return, in turn */
static int reg_result;          /* what the frame event's registration returns */
static bool reset_in_two;       /* the chip resets in the start with two */
static int starts_one, starts_two;      /* starts asked, with one and two frame buffers */
static esp_lcd_panel_t *live;   /* the panel the DMA runs, if any */
static void *gone[8];           /* driver buffers freed (kept, to compare) */
static int ngone;
/* what a refused start returns: as the driver's for no free panel slot,
   interrupt or DMA channel */
#define REFUSED ESP_ERR_NOT_FOUND

esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *cfg,
				esp_lcd_panel_handle_t *ret_panel)
{
	int n = (int) cfg->num_fbs;
	check(!cfg->user_fbs[0] && !cfg->user_fbs[1], "the driver allocates the frame buffers");
	check(n == 1 || n == 2, "one frame buffer or two");
	if (n == 2)
		starts_two++;
	else
		starts_one++;
	if (n == 2 && reset_in_two) {
		printf("    | (the chip resets in the start with two frame buffers)\n");
		start_end(OUT_RESET);
	}
	int r = nresult < 4 ? results[nresult++] : 0;
	if (r) {
		printf("    | (the driver refuses this start, with %d: %s)\n", n, esp_err_to_name(r));
		return r;
	}
	if (psram_free < (long) (n * FB_BYTES)) {
		printf("    | (no PSRAM for %d frame buffers)\n", n);
		return ESP_ERR_NO_MEM;
	}
	esp_lcd_panel_t *p = calloc(1, sizeof(*p));
	p->nfbs = n;
	for (int i = 0; i < n; i++) {
		p->fbs[i] = calloc(1, FB_BYTES);
		psram_free -= FB_BYTES;
	}
	*ret_panel = p;
	return ESP_OK;
}

esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t p)
{
	live = p;
	p->shown = 0;
	return ESP_OK;
}

esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t fb_num,
					     void **fb0, ...)
{
	check(fb_num >= 1 && (int) fb_num <= p->nfbs, "frame buffers asked of the panel: as many as it has");
	if (!(fb_num >= 1 && (int) fb_num <= p->nfbs))
		return ESP_ERR_INVALID_ARG;
	*fb0 = p->fbs[0];
	if (fb_num == 2) {
		va_list ap;
		va_start(ap, fb0);
		void **fb1 = va_arg(ap, void **);
		*fb1 = p->fbs[1];
		va_end(ap);
	}
	return ESP_OK;
}

esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t p)
{
	if (live == p)
		live = NULL;
	for (int i = 0; i < p->nfbs; i++) {
		if (ngone < 8)
			gone[ngone++] = p->fbs[i];
		psram_free += FB_BYTES;
	}
	free(p);
	return ESP_OK;
}

esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p,
		const esp_lcd_rgb_panel_event_callbacks_t *cbs, void *ctx)
{
	(void) ctx;
	if (reg_result) {
		printf("    | (the driver refuses the frame event: %s)\n", esp_err_to_name(reg_result));
		return reg_result;
	}
	p->on_switch = cbs->on_frame_buf_complete;
	return ESP_OK;
}

/* a pointer into one of the panel's buffers: the DMA moves to it, and the
   switch is reported at once */
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x0, int y0, int x1, int y1,
				    const void *data)
{
	(void) x0; (void) y0; (void) x1; (void) y1;
	check(p != NULL, "a swap asked of no panel");
	if (!p)
		return ESP_ERR_INVALID_ARG;
	for (int i = 0; i < p->nfbs; i++)
		if ((const uint8_t *) data >= (const uint8_t *) p->fbs[i] &&
		    (const uint8_t *) data < (const uint8_t *) p->fbs[i] + FB_BYTES)
			p->shown = i;
	if (p->on_switch)
		p->on_switch(p, NULL, NULL);
	return ESP_OK;
}

struct HostSem {
	int n;
};

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	return calloc(1, sizeof(struct HostSem));
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
	(void) wait;
	if (!s->n)
		return pdFALSE;
	s->n = 0;
	return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
	s->n = 1;
	if (woken)
		*woken = pdFALSE;
	return pdTRUE;
}

/* ==== ESP-IDF's system: the reset's cause, the partition, the restart ==== */

static esp_reset_reason_t reset_now = ESP_RST_POWERON;
static esp_partition_t running = { ESP_PARTITION_SUBTYPE_APP_FACTORY };

esp_reset_reason_t esp_reset_reason(void)
{
	return reset_now;
}

const esp_partition_t *esp_ota_get_running_partition(void)
{
	return &running;
}

/* ==== the console: its lines kept, and shown ==== */

#define MAX_LINES 32
static int nlines;
static int line_level[MAX_LINES];
static char line_text[MAX_LINES][640];

static void keep_line(int level, const char *s)
{
	static const char *const names[] = { "grey", "green", "yellow", "red", "cyan", "title" };
	if (nlines < MAX_LINES) {
		line_level[nlines] = level;
		snprintf(line_text[nlines], sizeof(line_text[0]), "%s", s);
		nlines++;
	}
	printf("    [%s] %s\n", level >= 0 && level <= 5 ? names[level] : "?", s);
}

void console_printf(int level, const char *fmt, ...)
{
	char s[640];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	keep_line(level, s);
}

void console_puts(int level, const char *s)
{
	keep_line(level, s);
}

/* a line of this start at that level with that in it */
static bool said(int level, const char *piece)
{
	for (int i = 0; i < nlines; i++)
		if (line_level[i] == level && strstr(line_text[i], piece))
			return true;
	return false;
}

/* any line of this start with that in it */
static bool said_any(const char *piece)
{
	for (int i = 0; i < nlines; i++)
		if (strstr(line_text[i], piece))
			return true;
	return false;
}

void esp_restart(void)
{
	printf("    | (esp_restart())\n");
	/* the second restart (DBUF_NOTE_ONE) comes before the console is up,
	   with no panel: its line goes to the UART alone */
	check((nlines > 0 && strstr(line_text[nlines - 1], "tiny386 starts again now, once")) ||
	      (dbuf_note.stage == DBUF_NOTE_ONE && nlines == 0),
	      "the restart says so on the panel first, or is the second, before the console");
	start_end(OUT_RESTART);
}

/* console.c's: whether the ring the last run left was marked by the crash
   screen (the case's first start only) */
static bool ring_crashed;

bool console_previous_crashed(void)
{
	return ring_crashed;
}

/* ==== speed.c's double buffering setting ==== */

static int db_value = -1;
static bool db_refused;

void speed_set_double(int on)
{
	db_value = on ? 1 : 0;
}

void speed_double_refused(void)
{
	db_refused = true;
}

/* ==== FreeRTOS's tasks: vga_task's turns ==== */

static sigjmp_buf run_end;
static TickType_t ticks;
static void after_picture(void);
static void trace_line(void);

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	return (TaskHandle_t) &ticks;
}

TaskHandle_t xTaskGetHandle(const char *name)
{
	(void) name;
	return NULL;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
	(void) task;
	return 0;
}

BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
	(void) task;
	return pdPASS;
}

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{
	(void) clear; (void) wait;
	return 0;
}

TickType_t xTaskGetTickCount(void)
{
	return ticks;
}

void vTaskDelay(TickType_t t)
{
	ticks += t;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
				BaseType_t clear, BaseType_t all, TickType_t wait)
{
	(void) group; (void) clear; (void) all; (void) wait;
	return bits;
}

/* the end of a turn of vga_task: the picture and its swap are done */
BaseType_t xTaskDelayUntil(TickType_t *last, TickType_t period)
{
	turns++;
	after_picture();
	if (trace)
		trace_line();
	*last += period;
	ticks = *last;
	now_us += 30000;
	if (turns >= pictures)
		siglongjmp(run_end, 1);
	return pdTRUE;
}

/* ==== the console's frame buffer and the speed screen: off the panel here ==== */

static uint16_t *console_buf;

void display_lock(void)
{
}

void display_unlock(void)
{
}

void console_set_fb(uint16_t *fb)
{
	console_buf = fb;
}

bool console_owns_display(void)
{
	return false;
}

bool console_held(void)
{
	return false;
}

void console_uart_printf(const char *fmt, ...)
{
	(void) fmt;
}

/* speed_screen_poll(): below, with the PC */

/* ==== the PC: only its VGA ==== */

static uint8_t *vga_buf;        /* where the VGA code draws now, as it was told */
static uint8_t *own_buf;        /* screen_init()'s own, when there is no panel */

/* display.c's vga_set_fb() (built with -Dvga_set_fb=host_vga_set_fb) */
void host_vga_set_fb(VGAState *s, uint8_t *fb)
{
	vga_buf = fb;
	vga_set_fb(s, fb);
}

/* esp_main.c's redraw() with LCD_DOUBLE_BUFFER */
static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque;
	lcd_vga_flush(x, y, w, h);
}

/* pc.c's, without the information port's counting */
void pc_vga_step(void *o)
{
	PC *pc = o;
	if (vga_step(pc->vga)) {
		vga_refresh(pc->vga, pc->redraw, pc->redraw_data, pc->full_update != 0);
		if (pc->full_update == 2)
			pc->full_update = 0;
	}
}

static void after_picture(void)
{
	char msg[200];
	snprintf(msg, sizeof(msg), "picture %d: the VGA code is to draw into NULL next", turns);
	check(vga_buf != NULL, msg);
	if (!vga_buf)
		return;
	for (int i = 0; i < ngone; i++) {
		snprintf(msg, sizeof(msg), "picture %d: the VGA code is to draw into a frame "
			 "buffer the driver has freed", turns);
		check(vga_buf != gone[i], msg);
	}
	if (live) {
		snprintf(msg, sizeof(msg), "picture %d: the VGA code is to draw where "
			 "lcd_vga_fb() says", turns);
		check(vga_buf == (uint8_t *) lcd_vga_fb(), msg);
		if (lcd_double_on()) {
			snprintf(msg, sizeof(msg), "picture %d: double buffering: the VGA code "
				 "draws into the buffer that is not on show", turns);
			check(vga_buf != (uint8_t *) live->fbs[live->shown], msg);
		} else {
			snprintf(msg, sizeof(msg), "picture %d: one buffer: the VGA code draws "
				 "into the one on show", turns);
			check(vga_buf == (uint8_t *) live->fbs[live->shown], msg);
		}
	} else {
		snprintf(msg, sizeof(msg), "picture %d: no panel: the VGA code draws into the "
			 "PC's own buffer (screen_init())", turns);
		check(vga_buf == own_buf, msg);
	}
}

/* ==== --trace: Ctrl+Alt+F9 and the VBE modes during the run ==== */

static const char *role(const uint8_t *p)
{
	if (!p)
		return "NULL";
	if (live && p == live->fbs[0])
		return live->shown == 0 ? "fb0 shown" : "fb0 hidden";
	if (live && live->nfbs > 1 && p == live->fbs[1])
		return live->shown == 1 ? "fb1 shown" : "fb1 hidden";
	if (p == own_buf)
		return "the PC's own";
	return "another";
}

static uint32_t hash(const uint8_t *p)
{
	uint32_t h = 2166136261u;
	if (!p)
		return 0;
	for (size_t i = 0; i < FB_BYTES; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void trace_line(void)
{
	const uint8_t *shown = live ? live->fbs[live->shown] : own_buf;
	printf("    picture %d: next into %s; double buffering %s%s; on show %08x\n", turns,
	       role(vga_buf), lcd_double_made() ? "made" : "not made",
	       lcd_double_on() ? ", on" : "", (unsigned) hash(shown));
}

static void vbe(VGAState *s, int idx, int val)
{
	vbe_write(s, 0, idx);
	vbe_write(s, 1, val);
}

/* speed.c's, from vga_task after each picture: with --trace, Ctrl+Alt+F9 as
   speed.c takes it, and the guest switching to VBE 800x480 at 16 bits and
   then 8 (vbe_blocks 0: the whole picture each time) */
void speed_screen_poll(void *opaque, uint32_t pictures_per_s)
{
	PC *pc = opaque;
	int t = turns;
	(void) pictures_per_s;
	if (!trace)
		return;
	if (t == 1 || t == 4 || t == 5 || t == 8 || t == 10) {
		if (lcd_double_made()) {
			display_lock();
			bool on = !lcd_double_on();
			void *fb = lcd_double_set(on);
			vga_set_fb(pc->vga, fb);        /* speed.c's own call */
			vga_buf = fb;
			display_unlock();
			printf("    Ctrl+Alt+F9: double buffering %s now\n", on ? "on" : "off");
		} else {
			printf("    Ctrl+Alt+F9: for the next start\n");
		}
	}
	if (t == 2 || t == 6) {
		int bpp = t == 2 ? 16 : 8;
		vga_dirty_blocks = 0;
		vbe(pc->vga, 4, 0);
		vbe(pc->vga, 1, W);
		vbe(pc->vga, 2, H);
		vbe(pc->vga, 3, bpp);
		vbe(pc->vga, 4, 0x01 | 0x40);
		for (long i = 0; i < (long) W * H * bpp / 8; i++)
			vram[i] = (uint8_t) (1 + (i * 7 + t) % 15);
		printf("    VBE %dx%d at %d bits\n", W, H, bpp);
	}
}

/* ==== a fault in the drawing ==== */

static char *put_str(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	return p;
}

static void on_fault(int sig, siginfo_t *si, void *uc)
{
	(void) sig; (void) uc;
	char line[160], *p = line;
	uintptr_t a = (uintptr_t) si->si_addr;
	int n = 0;
	p = put_str(p, "  FAIL: the drawing faulted (SIGSEGV) at address 0x");
	while (n < 2 * (int) sizeof(a) - 1 && !(a >> (4 * (2 * sizeof(a) - 1 - n))))
		n++;
	for (; n < 2 * (int) sizeof(a); n++)
		*p++ = "0123456789abcdef"[(a >> (4 * (2 * sizeof(a) - 1 - n))) & 15];
	p = put_str(p, ", in picture ");
	*p++ = '0' + (turns + 1) % 10;
	*p++ = '\n';
	if (write(1, line, p - line) < 0)
		_exit(4);
	_exit(3);
}

/* ==== the card's inis ==== */

/* /sdcard/tiny386 as setup_find() lists it, and each ini's double_buffer */
static struct setup_list inis = {
	.dir = "/sdcard/tiny386",
	.n = 3,
	.ini = { { "freedos.ini", "" }, { "win95-sd.ini", "" }, { "win95-14m.ini", "" } },
};
#define INI_DOS 0
#define INI_SD 1
#define INI_14M 2

/* ==== the cases ==== */

/* what the LP SRAM holds at the first start */
enum { RTC_EMPTY, RTC_NOISE, RTC_TWO, RTC_TRYING, RTC_BAD_CHECK, RTC_OLD_FORM, RTC_NEW_FORM,
       RTC_ONE };

struct tcase {
	const char *name;
	int dbuf;               /* win95-sd.ini's double_buffer (freedos.ini 0, win95-14m.ini 1) */
	int rtc;                /* the LP SRAM at the first start */
	const char *rtc_ini;    /* the ini a note there names */
	esp_reset_reason_t reset;       /* the first start's cause (0: power-on) */
	bool linux_default;     /* tiny386 the dual boot's test app (VOL+) */
	int fail[3][4];         /* what the driver's starts return, at each start */
	int reg_at;             /* the start at which the frame event is refused */
	int reset_at;           /* the start at which the chip resets in the start with two */
	bool lose_note;         /* the LP SRAM does not keep the note through the restart */
	long psram;             /* PSRAM free at power-on (0: PSRAM_START) */
	/* what has to happen */
	int want_starts, want_restarts;
	bool want_panel;        /* at the last start */
	bool want_double;       /* both frame buffers made at the last start */
	bool want_on;           /* double buffering on at the last start */
	bool want_menu;         /* the boot menu at the last start */
	int want_level;         /* a line of the last start: its level, */
	const char *want_line;  /* and something in it (NULL: no line about double buffering at all) */
	/* the review of v4.6 */
	bool crashed;           /* the ring marked by the crash screen at the first start */
	bool lose_second;       /* the LP SRAM does not keep the second restart's note */
};

static const struct tcase cases[] = {
	{ "double_buffer = 0: as v4.5, one frame buffer, no line", 0, RTC_EMPTY, NULL, 0, false,
	  { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, 0, NULL, false, false },
	{ "double_buffer = 1: one restart, then both frame buffers made with the panel", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 } }, 0, 0, false, 0,
	  2, 1, true, true, true, false, CON_INFO, "both made with the panel at power-on", false, false },
	{ "double_buffer = 1, two refused after the restart: one, and the yellow line", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 }, { REFUSED, 0 } }, 0, 0, false, 0,
	  2, 1, true, false, false, false, CON_WARN,
	  "The panel would not start with two at power-on (ESP_ERR_NOT_FOUND)", false, false },
	{ "double_buffer = 1, no memory for two after the restart: one, and the yellow line", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 }, { ESP_ERR_NO_MEM, 0 } }, 0, 0, false, 0,
	  2, 1, true, false, false, false, CON_WARN,
	  "The panel would not start with two at power-on (ESP_ERR_NO_MEM)", false, false },
	{ "double_buffer = 1, the frame event refused after the restart: one, and the yellow line",
	  1, RTC_EMPTY, NULL, 0, false, { { 0 } }, 2, 0, false, 0,
	  2, 1, true, false, false, false, CON_WARN,
	  "The panel would not start with two at power-on (ESP_ERR_INVALID_ARG)", false, false },
	{ "double_buffer = 1, two and then one refused after the restart: a second restart, then one and the yellow line",
	  1, RTC_EMPTY, NULL, 0, false, { { 0 }, { REFUSED, REFUSED }, { 0 } }, 0, 0, false, 0,
	  3, 2, true, false, false, false, CON_WARN,
	  "would not start with two frame buffers (ESP_ERR_NOT_FOUND), nor with one after that "
	  "(ESP_ERR_NOT_FOUND), with PSRAM free 16110 KiB (largest block 16110 KiB) and internal "
	  "DMA memory free 120 KiB", false, false },
	{ "double_buffer = 1, two and one refused after the restart, the second note lost: no third restart",
	  1, RTC_EMPTY, NULL, 0, false, { { 0 }, { REFUSED, ESP_ERR_NO_MEM }, { 0 } }, 0, 0, false, 0,
	  3, 2, true, false, false, true, CON_WARN, "followed a software restart with no note",
	  false, true },
	{ "double_buffer = 1, two and one refused after the restart, and one again after the second: no panel, no third restart",
	  1, RTC_EMPTY, NULL, 0, false, { { 0 }, { REFUSED, REFUSED }, { REFUSED } }, 0, 0, false, 0,
	  3, 2, false, false, false, false, CON_WARN, "there is no panel, so no second frame buffer", false, false },
	{ "a note asking for two under the Linux-default table, the panel with neither: no restart, no panel",
	  1, RTC_TWO, "win95-sd.ini", ESP_RST_SW, true, { { REFUSED, REFUSED } }, 0, 0, false, 0,
	  1, 0, false, false, false, false, CON_WARN, "there is no panel, so no second frame buffer", false, false },
	{ "a second restart's note at power-on (the LP SRAM kept it): one, the yellow line, no restart",
	  1, RTC_ONE, "win95-sd.ini", 0, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, false, CON_WARN,
	  "two frame buffers (ESP_ERR_NOT_FOUND), nor with one after that (ESP_ERR_NO_MEM), with PSRAM "
	  "free 1000 KiB (largest block 900 KiB) and internal DMA memory free 100 KiB", false, false },
	{ "double_buffer = 1 after a crash (the reset a panic): no restart, the report kept, one", 1,
	  RTC_EMPTY, NULL, ESP_RST_PANIC, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN,
	  "The last run ended with a crash, a watchdog or a power dip (above)", false, false },
	{ "double_buffer = 1 after a CPU lock-up: no restart, the report kept, one", 1,
	  RTC_EMPTY, NULL, ESP_RST_CPU_LOCKUP, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "RESET makes both", false, false },
	{ "double_buffer = 1 after a brownout: no restart, the report kept, one", 1,
	  RTC_EMPTY, NULL, ESP_RST_BROWNOUT, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "a restart now would take that off the panel", false, false },
	{ "double_buffer = 1 after RESET on the crash screen (power-on, the ring marked): no restart", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN,
	  "The last run ended with a crash, a watchdog or a power dip (above)", true, false },
	{ "double_buffer = 0 after a crash: as v4.5, no line", 0,
	  RTC_EMPTY, NULL, ESP_RST_PANIC, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, 0, NULL, true, false },
	{ "double_buffer = 1, Linux the default (tiny386 the VOL+ app): no restart, the yellow line",
	  1, RTC_EMPTY, NULL, 0, true, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "tiny386 is the dual boot's VOL+ app here", false, false },
	{ "double_buffer = 1, the LP SRAM forgets the note: one restart, no second", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 } }, 0, 0, true, 0,
	  2, 1, true, false, false, true, CON_WARN, "followed a software restart with no note", false, false },
	{ "double_buffer = 1, a software restart with no note (Linux's reboot): no restart", 1,
	  RTC_EMPTY, NULL, ESP_RST_SW, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "followed a software restart with no note", false, false },
	{ "double_buffer = 1, a note not whole (one byte changed): ignored, no restart", 1,
	  RTC_BAD_CHECK, "win95-sd.ini", ESP_RST_EXT, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "The note of a restart in the LP SRAM was not whole", false, false },
	{ "double_buffer = 1, a note of another form (the dbuf series' 20 bytes): ignored, no restart",
	  1, RTC_OLD_FORM, NULL, ESP_RST_SW, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "The note of a restart in the LP SRAM was not whole", false, false },
	{ "double_buffer = 1, a whole note of a later form: ignored, no restart", 1,
	  RTC_NEW_FORM, "win95-sd.ini", ESP_RST_SW, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, false, false, true, CON_WARN, "The note of a restart in the LP SRAM was not whole", false, false },
	{ "double_buffer = 1, noise in the LP SRAM at power-on: no note, one restart, both made", 1,
	  RTC_NOISE, NULL, 0, false, { { 0 } }, 0, 0, false, 0,
	  2, 1, true, true, true, false, CON_INFO, "both made with the panel at power-on", false, false },
	{ "a note naming an ini no longer there: both made, the menu, win95-sd.ini", 1,
	  RTC_TWO, "gone.ini", ESP_RST_SW, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, true, true, true, CON_INFO, "gone.ini, chosen before the restart, is not there now", false, false },
	{ "a note naming an ini with double_buffer = 0: both made, kept off, Ctrl+Alt+F9 at once", 1,
	  RTC_TWO, "freedos.ini", ESP_RST_SW, false, { { 0 } }, 0, 0, false, 0,
	  1, 0, true, true, false, false, CON_INFO, "double_buffer = 0, but the panel has two frame buffers", false, false },
	{ "double_buffer = 1, a reset while the panel was made with two: then one, no restart", 1,
	  RTC_EMPTY, NULL, 0, false, { { 0 } }, 0, 2, false, 0,
	  3, 1, true, false, false, true, CON_WARN, "The last start was cut short while it made the panel with two", false, false },
	{ "no panel at power-on, double_buffer = 0", 0, RTC_EMPTY, NULL, 0, false,
	  { { REFUSED } }, 0, 0, false, 0,
	  1, 0, false, false, false, true, 0, NULL, false, false },
	{ "no panel at power-on, double_buffer = 1: no restart", 1, RTC_EMPTY, NULL, 0, false,
	  { { REFUSED } }, 0, 0, false, 0,
	  1, 0, false, false, false, true, CON_WARN, "there is no panel, so no second frame buffer", false, false },
	{ "double_buffer = 1, no room for a second frame buffer: no restart", 1, RTC_EMPTY, NULL, 0,
	  false, { { 0 } }, 0, 0, false, (long) FB_BYTES + KEEP_FREE + 2560L * 1024,
	  1, 0, true, false, false, true, CON_WARN, "no room for a second frame buffer", false, false },
};
#define NCASES ((int) (sizeof(cases) / sizeof(cases[0])))

static int ini_dbuf(const struct tcase *c, int i)
{
	return i == INI_SD ? c->dbuf : i == INI_14M ? 1 : 0;
}

/* FNV-1a over the note up to its check, as dbuf_boot.c's */
static uint32_t note_fnv(const struct dbuf_note *n)
{
	const uint8_t *p = (const uint8_t *) n;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < offsetof(struct dbuf_note, check); i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static bool note_clear(const struct dbuf_note *n)
{
	static const struct dbuf_note zero;
	return !memcmp(n, &zero, sizeof(zero));
}

/* the LP SRAM a case starts with */
static void rtc_first(const struct tcase *c, struct dbuf_note *n)
{
	memset(n, 0, sizeof(*n));
	switch (c->rtc) {
	case RTC_NOISE:
		for (size_t i = 0; i < sizeof(*n); i++)
			((uint8_t *) n)[i] = (uint8_t) (i * 37 + 11);
		break;
	case RTC_TWO:
	case RTC_TRYING:
	case RTC_BAD_CHECK:
	case RTC_NEW_FORM: {
		n->magic = DBUF_NOTE_MAGIC;
		n->magic_inv = ~DBUF_NOTE_MAGIC;
		/* a later version's, whole: its own check right */
		n->form = c->rtc == RTC_NEW_FORM ? DBUF_NOTE_FORM + 1 : DBUF_NOTE_FORM;
		n->stage = c->rtc == RTC_TRYING ? DBUF_NOTE_TRYING : DBUF_NOTE_TWO;
		snprintf(n->dir, sizeof(n->dir), "%s", inis.dir);
		snprintf(n->name, sizeof(n->name), "%s", c->rtc_ini);
		n->check = note_fnv(n);
		if (c->rtc == RTC_BAD_CHECK)
			n->name[3] ^= 0x20;
		break;
	}
	case RTC_ONE:
		/* a second restart's note, whole, as dbuf_boot_panel() writes it */
		n->magic = DBUF_NOTE_MAGIC;
		n->magic_inv = ~DBUF_NOTE_MAGIC;
		n->form = DBUF_NOTE_FORM;
		n->stage = DBUF_NOTE_ONE;
		snprintf(n->dir, sizeof(n->dir), "%s", inis.dir);
		snprintf(n->name, sizeof(n->name), "%s", c->rtc_ini);
		n->why_two = REFUSED;
		n->why_one = ESP_ERR_NO_MEM;
		n->psram_free = 1000;
		n->psram_block = 900;
		n->dma_free = 100;
		n->check = note_fnv(n);
		break;
	case RTC_OLD_FORM: {
		/* the dbuf series' note: magic, ~magic, two, one, stopped */
		uint32_t old[5] = { DBUF_NOTE_MAGIC, ~DBUF_NOTE_MAGIC, ESP_ERR_NOT_FOUND,
				    ESP_ERR_NOT_FOUND, 0 };
		memcpy(n, old, sizeof(old));
		break;
	}
	default:
		break;
	}
}

/* one start, in a child process: as app_main() goes */
static void start(const struct tcase *c, int k)
{
	long vram_size = trace ? VRAM_TRACE : VRAM;
	char msg[240];
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_fault;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	failures = checks = 0;          /* this start's own */
	memcpy(results, c->fail[k - 1], sizeof(results));
	nresult = 0;
	reg_result = c->reg_at == k ? ESP_ERR_INVALID_ARG : 0;
	reset_in_two = c->reset_at == k;
	running.subtype = c->linux_default ? ESP_PARTITION_SUBTYPE_APP_TEST :
		ESP_PARTITION_SUBTYPE_APP_FACTORY;
	psram_free = c->psram ? c->psram : PSRAM_START;
	ring_crashed = c->crashed && k == 1;
	bool last = k == c->want_starts;

	/* app_main(): the note first, then the panel */
	struct dbuf_note found = dbuf_note;
	dbuf_boot_take();
	check(note_clear(&dbuf_note), "the note is cleared as soon as it is taken");
	void *fb = dbuf_boot_panel();
	check(note_clear(&dbuf_note), "no note left once the panel is up");
	bool asked_two = found.magic == DBUF_NOTE_MAGIC && found.magic_inv == ~DBUF_NOTE_MAGIC &&
		found.form == DBUF_NOTE_FORM && found.stage == DBUF_NOTE_TWO &&
		found.check == note_fnv(&found);
	snprintf(msg, sizeof(msg), "the driver asked for two frame buffers %s (%d asks)",
		 asked_two ? "once" : "never", starts_two);
	check(starts_two == (asked_two ? 1 : 0), msg);
	if (!asked_two) {
		snprintf(msg, sizeof(msg), "one start with one frame buffer, as v4.5's power-on "
			 "(%d)", starts_one);
		check(starts_one == 1, msg);
	}
	console_buf = fb;               /* console_init() */
	dbuf_boot_banner();             /* after banner() */

	/* the emulator's pool, as app_main() sizes it from the largest block */
	long pool = (psram_free - KEEP_FREE) & ~4095L;
	long pool_one = ((c->psram ? c->psram : PSRAM_START) - (long) FB_BYTES - KEEP_FREE) & ~4095L;

	/* choose_ini() */
	int pick = dbuf_boot_pick(&inis);
	bool menu = pick < 0;
	if (menu)
		pick = INI_SD;          /* the menu's choice */
	picked = pick;
	if (last) {
		snprintf(msg, sizeof(msg), "the boot menu %s at the last start", c->want_menu ?
			 "shown" : "not shown");
		check(menu == c->want_menu, msg);
	}

	/* double_buffer_start(): may restart (esp_restart() ends the start) */
	int want = ini_dbuf(c, pick);
	dbuf_boot_start(want, inis.dir, inis.ini[pick].name, pool);
	check(last, "no restart at this start, as the case has it");

	snprintf(msg, sizeof(msg), "%s at the last start", c->want_panel ? "a panel" : "no panel");
	check((globals.panel_fb != NULL) == c->want_panel && (live != NULL) == c->want_panel, msg);
	check(lcd_double_made() == c->want_double, "both frame buffers made, or not, as the case has it");
	check(lcd_double_on() == c->want_on, "double buffering on, or not, as the case has it");
	check(console_buf == globals.panel_fb, "the console draws into the frame buffer on show");
	/* Ctrl+Alt+F9: the ini's value; refused when it asked for two and did
	   not get them, so that one press saves 0 */
	check(db_value == want, "Ctrl+Alt+F9 starts from the ini's double_buffer");
	check(db_refused == (want && !c->want_double), "the speed screen told the second was not made");
	/* the second frame buffer paid for once: before the pool was sized */
	if (c->want_double) {
		snprintf(msg, sizeof(msg), "the pool %ld KiB, %ld KiB less than with one frame buffer",
			 pool / 1024, (pool_one - pool) / 1024);
		check(pool_one - pool >= 750 * 1024 && pool_one - pool <= 754 * 1024, msg);
	} else if (c->want_panel) {
		check(pool == pool_one, "with one frame buffer the pool is as v4.5's");
	}
	if (c->want_line) {
		snprintf(msg, sizeof(msg), "the line \"...%s...\" at the last start", c->want_line);
		check(said(c->want_level, c->want_line), msg);
	} else {
		check(!said_any("double_buffer") && !said_any("frame buffer") && !said_any("restart"),
		      "nothing said about double buffering, as v4.5");
	}

	/* screen_init() and pc_new() */
	uint8_t *sfb;
	if (globals.panel_fb) {
		sfb = lcd_vga_fb();
	} else {
		own_buf = calloc(1, FB_BYTES);
		sfb = own_buf;
	}
	memset(vram, 0, sizeof(vram));
	VGAState *vga = vga_init((char *) vram, vram_size, sfb, W, H);
	vga_buf = sfb;
	for (int row = 0; row < 25; row++)
		for (int col = 0; col < 80; col++) {
			vram[(row * 80 + col) * 4] = 'A' + (row + col) % 26;
			vram[(row * 80 + col) * 4 + 1] = 0x1f;
		}
	PC *pc = calloc(1, sizeof(PC));
	pc->vga = vga;
	pc->redraw = redraw;
	pc->redraw_data = pc;
	globals.pc = pc;

	/* vga_task, for 'pictures' pictures */
	turns = 0;
	if (!sigsetjmp(run_end, 1))
		vga_task(NULL);

	/* the text reached the buffer on show */
	if (live) {
		const uint16_t *shown = live->fbs[live->shown];
		long lit = 0;
		for (int y = 40; y < 440; y++)
			for (int x = 40; x < 760; x++)
				lit += shown[y * W + x] != 0;
		check(lit > 0, "the text is in the buffer on show");
	}
	printf("  start %d: %d pictures, %d checks, %d failed\n", k, turns, checks, failures);
	start_end(OUT_RAN);
}

/* a case: its starts, the LP SRAM and the reset's cause carried between them */
static int run_case(const struct tcase *c)
{
	struct dbuf_note rtc;
	rtc_first(c, &rtc);
	esp_reset_reason_t why = c->reset ? c->reset : ESP_RST_POWERON;
	int restarts = 0, resets = 0, k, bad = 0;
	int first_pick = -1;            /* the ini the first restart's note names */
	bool second_one = false;        /* the second restart was the panel's (DBUF_NOTE_ONE) */
	char msg[300];
	failures = checks = 0;
	for (k = 1; k <= 4; k++) {
		int fd[2];
		if (pipe(fd)) {
			perror("pipe");
			return 1;
		}
		if (!trace)
			printf("  start %d (%s%s):\n", k, why == ESP_RST_SW ? "a software restart" :
			       why == ESP_RST_POWERON ? "power-on" : why == ESP_RST_EXT ?
			       "the reset pin" : why == ESP_RST_PANIC ? "a crash" :
			       why == ESP_RST_BROWNOUT ? "a brownout" : "a CPU lock-up",
			       c->crashed && k == 1 ? ", the ring marked by the crash screen" : "");
		dbuf_note = rtc;
		reset_now = why;
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			return 1;
		}
		if (pid == 0) {
			close(fd[0]);
			report_fd = fd[1];
			start(c, k);
			_exit(6);       /* not reached */
		}
		close(fd[1]);
		struct report r;
		ssize_t got = read(fd[0], &r, sizeof(r));
		close(fd[0]);
		int st;
		waitpid(pid, &st, 0);
		if (got != (ssize_t) sizeof(r) || !WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("  FAIL: start %d ended without its report (%s %d)\n", k,
			       WIFEXITED(st) ? "exit status" : "signal",
			       WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
			bad++;
			break;
		}
		bad += r.failures;
		if (r.outcome == OUT_RESTART && r.note.stage == DBUF_NOTE_ONE) {
			/* the second: the panel started with neither, before the ini
			   was chosen; the note names the first restart's ini, with the
			   start's two errors and the memory before them */
			restarts++;
			second_one = restarts == 2;
			int fb_two = c->fail[k - 1][0], fb_one = c->fail[k - 1][1];
			snprintf(msg, sizeof(msg), "the second restart's note names %s/%s, with %s and %s, and "
				 "the memory figures", inis.dir, first_pick >= 0 ? inis.ini[first_pick].name : "?",
				 esp_err_to_name(fb_two), esp_err_to_name(fb_one));
			check(r.note.magic == DBUF_NOTE_MAGIC && first_pick >= 0 &&
			      r.note.check == note_fnv(&r.note) && !strcmp(r.note.dir, inis.dir) &&
			      !strcmp(r.note.name, inis.ini[first_pick].name) &&
			      r.note.why_two == fb_two && r.note.why_one == fb_one &&
			      r.note.psram_free == PSRAM_START / 1024 &&
			      r.note.psram_block == PSRAM_START / 1024 && r.note.dma_free == DMA_FREE / 1024,
			      msg);
			if (c->lose_second)
				memset(&rtc, 0, sizeof(rtc));
			else
				rtc = r.note;
			why = ESP_RST_SW;
			continue;
		}
		if (r.outcome == OUT_RESTART) {
			restarts++;
			first_pick = r.pick;
			snprintf(msg, sizeof(msg), "the restart's note names %s/%s",
				 inis.dir, r.pick >= 0 ? inis.ini[r.pick].name : "?");
			check(r.pick >= 0 && r.note.magic == DBUF_NOTE_MAGIC && r.note.stage == DBUF_NOTE_TWO &&
			      !strcmp(r.note.dir, inis.dir) && !strcmp(r.note.name, inis.ini[r.pick].name) &&
			      !r.note.why_two && !r.note.why_one && !r.note.psram_free,
			      msg);
			if (c->lose_note)
				memset(&rtc, 0, sizeof(rtc));
			else
				rtc = r.note;
			why = ESP_RST_SW;
			continue;
		}
		if (r.outcome == OUT_RESET) {
			resets++;
			check(r.note.magic == DBUF_NOTE_MAGIC && r.note.stage == DBUF_NOTE_TRYING,
			      "a reset in the start with two leaves the note saying so");
			rtc = r.note;
			why = ESP_RST_CPU_LOCKUP;
			continue;
		}
		check(note_clear(&r.note), "no note left after a start that did not restart");
		break;
	}
	snprintf(msg, sizeof(msg), "%d starts and %d restarts (%d resets), the case wants %d and %d",
		 k, restarts, resets, c->want_starts, c->want_restarts);
	check(k == c->want_starts && restarts == c->want_restarts, msg);
	check(restarts <= 1 || (restarts == 2 && second_one),
	      "never more than one restart, and a second only when the panel started with neither");
	return bad + failures;
}

int main(int argc, char **argv)
{
	int bad = 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 1 && !strcmp(argv[1], "--trace")) {
		trace = true;
		pictures = PICTURES_TRACE;
	}
	for (int i = 0; i < NCASES; i++) {
		if (trace)
			printf("[%s] %s\n", cases[i].want_panel ? "panel" : "no panel", cases[i].name);
		else
			printf("%s\n", cases[i].name);
		if (run_case(&cases[i])) {
			bad++;
			printf("  FAIL: this case\n");
		}
	}
	if (bad) {
		printf("test_double_start: %d of %d cases FAILED\n", bad, NCASES);
		return 1;
	}
	printf("test_double_start: all %d cases passed\n", NCASES);
	return 0;
}
