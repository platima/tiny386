/*
 * Host test of the Korvo-1's drawing loop with the panel started again for
 * double buffering (v4.6): esp/main/display.c's vga_task and
 * esp/main/lcd_korvo1.c as the board builds them, vga.c as the board builds
 * it (test 3c's object), and a pretend ESP-IDF RGB panel driver (below, with
 * the headers in tests/host/display/stubs) whose starts and allocations fail
 * when the test says.
 *
 * v4.5 on the board, [esp] double_buffer = 1: a load access fault on core 0
 * in vga_draw_glyph9() under vga_text_refresh(), from vga_task, at mtval
 * 0xFA50.  That is the first cell of 80x25 text (720x400 in the middle of
 * the 800x480 panel: x 40, y 40, 1600 bytes a row) in a frame buffer at
 * address 0: 40 * 1600 + 40 * 2 = 64080 = 0xFA50.  When the panel cannot be
 * started again with two frame buffers or with one (lcd_korvo1.c's last
 * fallback), there is no panel frame buffer, the PC is built with a buffer
 * of its own (esp_main.c's screen_init()), and v4.5's vga_task gave the VGA
 * code lcd_vga_present() after every picture, which is then NULL.
 *
 * Each case runs in a child process of its own, so that lcd_korvo1.c starts
 * from nothing every time: the panel started as app_main() starts it,
 * double buffering asked for as double_buffer_start() asks, the VGA built
 * as screen_init() and pc_new() build it, then vga_task for some pictures.
 * After each picture the buffer the VGA code is to draw into next must be
 * the one lcd_korvo1.c names (the back one, or the only one), never NULL,
 * never one the driver has freed, and the PC's own when there is no panel.
 * A fault in the drawing is caught and its address said: against v4.5's
 * display.c it is 0xfa50, the board's address.  That is the same arithmetic
 * as the board's, not proof of the board's path: any NULL frame buffer
 * under 80x25 text on this panel faults there.
 *
 * With --trace, the same cases for 12 pictures with 1 MiB of video memory,
 * Ctrl+Alt+F9 taken as speed.c takes it after pictures 2, 5, 6, 9 and 11,
 * and the guest switching to VBE 800x480 at 16 bits after picture 3 and at
 * 8 after picture 7; after every picture a line names the buffer the VGA
 * code draws into next and gives a hash of the buffer on show.  Each case's first line starts
 * "[panel]" or "[no panel]".  run.sh compares the "[panel]" cases against
 * display.c at v4.5, which must give the same lines.
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
#include "esp_timer.h"
#include "pc.h"
#include "vga.h"
#include "pci.h"
#include "common.h"
#include "console.h"
#include "lcd.h"
#include "speed.h"

void vga_task(void *arg);       /* display.c */

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define FB_BYTES ((size_t) W * H * 2)
#define VRAM (256 * 1024)
#define VRAM_TRACE (1024 * 1024)
#define PICTURES 6
#define PICTURES_TRACE 12

struct Globals globals;
EventGroupHandle_t global_event_group;

static int failures, checks;
static int turns;               /* pictures drawn in this case */
static bool trace;              /* --trace */
static int pictures = PICTURES; /* to draw in each case */
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

/* ==== the pretend RGB panel driver ==== */

struct esp_lcd_panel_t {
	void *fbs[2];
	int nfbs;
	bool own;               /* fbs[0] is the driver's: freed with the panel */
	int shown;              /* the buffer the DMA sends */
	esp_lcd_rgb_panel_frame_buf_complete_cb_t on_switch;
};

static int new_fails, calloc_fails;     /* calls still to fail */
static esp_lcd_panel_t *live;           /* the panel the DMA runs, if any */
static void *gone[8];                   /* driver buffers freed (kept, to compare) */
static int ngone;
/* what a refused start returns: as the driver's for no free panel slot,
   interrupt or DMA channel, so that it is told from the no memory of the
   second buffer */
#define REFUSED ESP_ERR_NOT_FOUND

esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *cfg,
				esp_lcd_panel_handle_t *ret_panel)
{
	if (new_fails > 0) {
		new_fails--;
		printf("    | (the driver refuses this start)\n");
		return REFUSED;
	}
	esp_lcd_panel_t *p = calloc(1, sizeof(*p));
	p->nfbs = cfg->num_fbs > 1 ? 2 : 1;
	for (int i = 0; i < p->nfbs; i++) {
		p->fbs[i] = cfg->user_fbs[i];
		if (!p->fbs[i]) {
			p->fbs[i] = calloc(1, FB_BYTES);
			p->own = true;
		}
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
	(void) fb_num;
	*fb0 = p->fbs[0];
	return ESP_OK;
}

esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t p)
{
	if (live == p)
		live = NULL;
	if (p->own && ngone < 8)
		gone[ngone++] = p->fbs[0];
	free(p);
	return ESP_OK;
}

esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p,
		const esp_lcd_rgb_panel_event_callbacks_t *cbs, void *ctx)
{
	(void) ctx;
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

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	(void) caps;
	if (calloc_fails > 0) {
		calloc_fails--;
		printf("    | (no memory for this allocation)\n");
		return NULL;
	}
	return calloc(n, size);
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

/* ==== the console and the speed screen: off the panel here ==== */

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

/* ==== the cases ==== */

struct tcase {
	const char *name;
	int boot_fails;         /* starts the driver refuses at power-on */
	int dbuf;               /* [esp] double_buffer */
	int restart_fails;      /* ... and when the panel is started again */
	int calloc_fails;       /* the second buffer's allocation */
	bool want_double;       /* both buffers made */
	bool want_panel;        /* a panel after the start */
	int want_two, want_one; /* v4.6: lcd_double_why() when not made */
};

/* The two cases where the panel is not started again (the last fallback):
   since v4.6 the firmware restarts there (esp_main.c double_buffer_start())
   and never builds the PC; vga_task is run all the same, as with no panel
   at power-on, to keep its handover honest. */
static const struct tcase cases[] = {
	{ "double_buffer = 0", 0, 0, 0, 0, false, true, 0, 0 },
	{ "double_buffer = 1, the panel started again with two buffers", 0, 1, 0, 0, true, true,
	  0, 0 },
	{ "double_buffer = 1, two buffers refused, the panel started again with one", 0, 1, 1, 0,
	  false, true, REFUSED, 0 },
	{ "double_buffer = 1, no memory for the second, the panel started again with one", 0, 1, 0, 1,
	  false, true, 0, 0 },
	{ "double_buffer = 1, the panel not started again with two or with one (v4.5 on the board)",
	  0, 1, 2, 0, false, false, REFUSED, REFUSED },
	{ "double_buffer = 1, no memory for the second, the panel not started again", 0, 1, 1, 1,
	  false, false, 0, REFUSED },
	{ "no panel at power-on, double_buffer = 0", 1, 0, 0, 0, false, false, 0, 0 },
	{ "no panel at power-on, double_buffer = 1", 1, 1, 0, 0, false, false, 0, 0 },
};
#define NCASES ((int) (sizeof(cases) / sizeof(cases[0])))

static int run_case(const struct tcase *c)
{
	long vram_size = trace ? VRAM_TRACE : VRAM;
	char msg[200];
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_fault;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	/* app_main(): the panel first */
	new_fails = c->boot_fails;
	void *fb = lcd_init();
	new_fails = 0;
	check((fb != NULL) == !c->boot_fails, "the panel at power-on, or none, as asked");
	console_buf = fb;               /* console_init() */

	/* double_buffer_start(): the second buffer from the end of the
	   emulator's pool, if there is a panel */
	if (c->dbuf && globals.panel_fb) {
		uint8_t *pool = aligned_alloc(4096, (FB_BYTES + 4095) & ~(size_t) 4095);
		new_fails = c->restart_fails;
		calloc_fails = c->calloc_fails;
		bool ok = lcd_double_start(pool);
		new_fails = calloc_fails = 0;
		check(ok == c->want_double, "lcd_double_start() as expected");
		check(lcd_double_made() == c->want_double, "lcd_double_made() as expected");
		check(console_buf == globals.panel_fb, "the console follows the panel's buffer (or none)");
		if (!ok) {
			int two, one;
			lcd_double_why(&two, &one);
			snprintf(msg, sizeof(msg), "lcd_double_why(): two %#x one %#x, want %#x and %#x",
				 two, one, c->want_two, c->want_one);
			check(two == c->want_two && one == c->want_one, msg);
		}
	}
	snprintf(msg, sizeof(msg), "%s after the start", c->want_panel ? "a panel" : "no panel");
	check((globals.panel_fb != NULL) == c->want_panel && (live != NULL) == c->want_panel, msg);

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
	printf("  %d pictures, %d checks, %d failed\n", turns, checks, failures);
	return failures ? 1 : 0;
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
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			return 2;
		}
		if (pid == 0)
			_exit(run_case(&cases[i]));
		int st;
		if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st)) {
			bad++;
			printf("  FAIL: this case (%s %d)\n", WIFEXITED(st) ? "exit status" : "signal",
			       WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
		}
	}
	if (bad) {
		printf("test_double_start: %d of %d cases FAILED\n", bad, NCASES);
		return 1;
	}
	printf("test_double_start: all %d cases passed\n", NCASES);
	return 0;
}
