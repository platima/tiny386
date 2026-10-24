/*
 * The boot console: see console.h.
 *
 * Also built on a PC for tests/host/test_console.c, with CONSOLE_HOST
 * defined: no ESP-IDF, no locks, no hooks, the ring in ordinary memory, the
 * UART is stdout.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"

#ifdef CONSOLE_HOST
#define IRAM_ATTR
#define RTC_NOINIT_ATTR
#define ring_lock() do { } while (0)
#define ring_unlock() do { } while (0)
#define con_lock() do { } while (0)
#define con_unlock() do { } while (0)
#define con_alloc(size) malloc(size)
#define uq_lock() do { } while (0)
#define uq_unlock() do { } while (0)
/* the test's clock and the UART's FIFO: console_host_set_ms(),
   console_host_uart_drain() */
static uint32_t s_host_ms;
static uint32_t s_host_room;
#define con_now_ms() s_host_ms
#else
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "status_led.h"
#if CONFIG_ESP_CONSOLE_UART
#include "hal/uart_ll.h"
/* the UART queue's writer puts bytes straight into the console UART's FIFO,
   as ESP-IDF's own UART output does without its driver */
#define UQ_UART 1
#endif

static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;
#define ring_lock() portENTER_CRITICAL_SAFE(&s_ring_mux)
#define ring_unlock() portEXIT_CRITICAL_SAFE(&s_ring_mux)
/* the UART queue: a few microseconds at a time, from any task */
static portMUX_TYPE s_uq_mux = portMUX_INITIALIZER_UNLOCKED;
#define uq_lock() portENTER_CRITICAL(&s_uq_mux)
#define uq_unlock() portEXIT_CRITICAL(&s_uq_mux)
#define con_now_ms() ((uint32_t) (esp_timer_get_time() / 1000))

/* the console's text, line buffers and the order of lines on the UART;
   recursive, as a line on its way out may make another */
static SemaphoreHandle_t s_con_mutex;
/* the frame buffer: the console, or vga_task drawing a picture */
static SemaphoreHandle_t s_display_mutex;

static void con_lock(void)
{
	if (s_con_mutex)
		xSemaphoreTakeRecursive(s_con_mutex, portMAX_DELAY);
}

static void con_unlock(void)
{
	if (s_con_mutex)
		xSemaphoreGiveRecursive(s_con_mutex);
}

/* The text grids are read only when drawing: PSRAM is fine, and leaves
   internal RAM to the emulator. */
static void *con_alloc(size_t size)
{
	void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
	return p ? p : malloc(size);
}
#endif

extern const uint8_t vgafont16[256 * 16];

#define RGB565(r, g, b) \
	((uint16_t) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* the VGA's own colours: light grey, light green, yellow, light red,
   light cyan, and white on blue for the title */
static const uint16_t fg_of[CON_NLEVELS] = {
	RGB565(170, 170, 170), RGB565(85, 255, 85), RGB565(255, 255, 85),
	RGB565(255, 85, 85), RGB565(85, 255, 255), RGB565(255, 255, 255),
};
static const uint16_t bg_of[CON_NLEVELS] = {
	0, 0, 0, 0, 0, RGB565(0, 0, 170),
};

#define CELL_W 8
#define CELL_H 16
#define CARD_H 16       /* the colour card along the bottom */

/* ====
 * The ring: the last 8 KiB of log text, in memory that a reset does not
 * clear (RTC_NOINIT_ATTR: the LP SRAM), for the next boot to show.
 */

#define RING_SIZE 8192                  /* a power of two */
#define RING_MAGIC 0x54333836u          /* "T386" */
#define RING_CRASHED 0x43524153u        /* "CRAS": the crash screen ran */

struct log_ring {
	uint32_t magic;
	uint32_t magic_inv;     /* ~magic: the header is not just noise */
	uint32_t wpos;          /* bytes ever written; the next goes at
				   buf[wpos % RING_SIZE] */
	uint32_t crashed;
	char buf[RING_SIZE];
};

static RTC_NOINIT_ATTR struct log_ring s_ring;

static bool ring_valid(void)
{
	return s_ring.magic == RING_MAGIC && s_ring.magic_inv == ~RING_MAGIC;
}

static void ring_reset(void)
{
	s_ring.wpos = 0;
	s_ring.crashed = 0;
	s_ring.magic = RING_MAGIC;
	s_ring.magic_inv = ~RING_MAGIC;
}

static inline void IRAM_ATTR ring_putc_locked(char c)
{
	s_ring.buf[s_ring.wpos & (RING_SIZE - 1)] = c;
	s_ring.wpos++;
}

static void ring_write_line(const char *s, int n)
{
	ring_lock();
	for (int i = 0; i < n; i++)
		if (s[i] != '\r')
			ring_putc_locked(s[i]);
	ring_putc_locked('\n');
	ring_unlock();
}

#ifndef CONSOLE_HOST
/* ROM printf channel 2: esp_rom_printf(), ESP_EARLY_LOGx and the messages
   of ESP_ERROR_CHECK.  May run from an interrupt or with the cache off, so
   it only puts the character in the ring. */
static void IRAM_ATTR con_rom_putc(char c)
{
	if (c == '\r')
		return;
	ring_lock();
	ring_putc_locked(c);
	ring_unlock();
}
#endif

static inline bool printable(char c)
{
	return (unsigned char) c >= 0x20 && c != 0x7f;
}

int console_ring_tail(char *out, int size, int lines)
{
	uint32_t end = s_ring.wpos;
	uint32_t start = end - (end < RING_SIZE ? end : RING_SIZE);

	if (size <= 0)
		return 0;
	out[0] = 0;
	if (!ring_valid() || lines <= 0 || size < 2 || end == 0)
		return 0;
#define AT(p) (s_ring.buf[(p) & (RING_SIZE - 1)])
	/* after a wrap the oldest line is cut: start after it */
	if (start > 0) {
		while (start < end && AT(start) != '\n')
			start++;
		if (start < end)
			start++;
	}
	if (start >= end)
		return 0;
	/* the text is start to stop, without the last line's '\n' */
	uint32_t stop = end;
	if (AT(stop - 1) == '\n')
		stop--;
	/* whole lines back from the end, while they fit: the copy is
	   best to stop, a '\n' and a '\0' */
	uint32_t best = stop, q = stop;
	int found = 0;
	for (;;) {
		uint32_t b = q;         /* this line is b to q */
		while (b > start && AT(b - 1) != '\n')
			b--;
		if (stop - b > (uint32_t) size - 2)
			break;
		best = b;
		if (++found == lines || b == start)
			break;
		q = b - 1;              /* over the '\n' before it */
	}
	if (!found)
		return 0;
	int o = 0;
	for (uint32_t p = best; p < stop; p++) {
		char c = AT(p);
		out[o++] = c == '\n' || printable(c) ? c : '?';
	}
#undef AT
	out[o++] = '\n';
	out[o] = 0;
	return found;
}

/* ====
 * The text on the panel: a grid of characters and their colours as they
 * should be (want) and as they are drawn (shown).  Drawing goes over the
 * cells that differ and writes back the rows it drew.
 */

static uint16_t *s_fb;
static int s_w, s_h;
static console_flush_fn *s_flush;
static int s_cols, s_rows;              /* row 0 is the title */
static uint16_t *s_want, *s_shown;      /* character | level << 8 */
static int s_cur_row, s_cur_col;
static bool s_pending_nl;
static bool s_card_dirty;
static volatile bool s_owner_console = true;
static int s_problems;

#define CELL(ch, lvl) ((uint16_t) ((uint8_t) (ch) | ((lvl) << 8)))
#define CELL_NONE 0xffff                /* never a real cell */

static void draw_cell(uint16_t *base, int stride, int x, int y,
		      uint8_t ch, uint16_t fg, uint16_t bg, bool compare)
{
	const uint8_t *g = vgafont16 + ch * 16;
	uint16_t *d = base + y * stride + x;
	for (int l = 0; l < CELL_H; l++, d += stride) {
		unsigned bits = g[l];
		for (int b = 0; b < CELL_W; b++) {
			uint16_t px = (bits & (0x80 >> b)) ? fg : bg;
			if (!compare || d[b] != px)
				d[b] = px;
		}
	}
}

/* Eight bars (black, blue, green, cyan, red, magenta, yellow, white) on
   the left half, sixteen greys from black to white on the right, under a
   grey line.  With the colour order right they look like that; with red
   and blue swapped, the second bar is red and the fifth blue. */
static void draw_card(void)
{
	static const uint16_t bars[8] = {
		RGB565(0, 0, 0), RGB565(0, 0, 255), RGB565(0, 255, 0),
		RGB565(0, 255, 255), RGB565(255, 0, 0), RGB565(255, 0, 255),
		RGB565(255, 255, 0), RGB565(255, 255, 255),
	};
	int y0 = s_rows * CELL_H;
	for (int y = y0; y < s_h; y++) {
		uint16_t *d = s_fb + y * s_w;
		for (int x = 0; x < s_w; x++) {
			uint16_t px;
			if (y < y0 + 2) {
				px = RGB565(85, 85, 85);
			} else if (x < s_w / 2) {
				px = bars[x * 16 / s_w];
			} else {
				int v = (x - s_w / 2) * 16 / (s_w - s_w / 2);
				v = v * 255 / 15;
				px = RGB565(v, v, v);
			}
			if (d[x] != px)
				d[x] = px;
		}
	}
}

static void display_trylock_draw(void);

static void render(void)
{
	if (!s_fb || !s_want || !s_owner_console)
		return;
	display_trylock_draw();
}

/* with the frame buffer ours: draw what changed, write back its rows */
static void render_locked(void)
{
	int y0 = s_h, y1 = 0;
	if (s_card_dirty) {
		draw_card();
		s_card_dirty = false;
		y0 = s_rows * CELL_H;
		y1 = s_h;
	}
	for (int r = 0; r < s_rows; r++) {
		bool drawn = false;
		uint16_t *want = s_want + r * s_cols;
		uint16_t *shown = s_shown + r * s_cols;
		for (int c = 0; c < s_cols; c++) {
			if (want[c] == shown[c])
				continue;
			int lvl = want[c] >> 8;
			draw_cell(s_fb, s_w, c * CELL_W, r * CELL_H,
				  want[c] & 0xff, fg_of[lvl], bg_of[lvl], true);
			shown[c] = want[c];
			drawn = true;
		}
		if (drawn) {
			if (r * CELL_H < y0)
				y0 = r * CELL_H;
			if (r * CELL_H + CELL_H > y1)
				y1 = r * CELL_H + CELL_H;
		}
	}
	if (y1 > y0 && s_flush)
		s_flush(0, y0, s_w, y1 - y0);
}

static void scroll_up(void)
{
	/* rows 2 .. s_rows - 1 up to 1 .. s_rows - 2; the title stays */
	memmove(s_want + 1 * s_cols, s_want + 2 * s_cols,
		(size_t) (s_rows - 2) * s_cols * sizeof(*s_want));
	for (int c = 0; c < s_cols; c++)
		s_want[(s_rows - 1) * s_cols + c] = CELL(' ', CON_INFO);
}

static void newline_now(void)
{
	s_pending_nl = false;
	s_cur_col = 0;
	if (s_cur_row < s_rows - 1)
		s_cur_row++;
	else
		scroll_up();
}

static void grid_put_line(int level, const char *s, int n)
{
	static uint8_t vis[600];        /* the line as it will show */
	int m = 0;

	if (!s_want)
		return;
	for (int i = 0; i < n && m < (int) sizeof(vis); i++) {
		unsigned char c = s[i];
		if (c == 0x1b) {
			/* a terminal colour sequence: ESC [ ... letter */
			while (i + 1 < n && !((s[i + 1] >= 'A' && s[i + 1] <= 'Z') ||
					      (s[i + 1] >= 'a' && s[i + 1] <= 'z')))
				i++;
			i++;
			continue;
		}
		if (c == '\r')
			continue;
		if (c == '\t') {
			do
				vis[m++] = ' ';
			while ((m & 7) && m < (int) sizeof(vis));
			continue;
		}
		vis[m++] = c < 0x20 || c == 0x7f ? '?' : c;
	}

	if (s_pending_nl)
		newline_now();
	/* as many rows as it takes, broken at a space where there is one in
	   the second half of the row */
	for (int i = 0; i < m; ) {
		int take = m - i;
		int room = s_cols - s_cur_col;
		if (take > room) {
			take = room;
			for (int k = room; k > room / 2; k--)
				if (vis[i + k - 1] == ' ') {
					take = k;
					break;
				}
		}
		for (int k = 0; k < take; k++)
			s_want[s_cur_row * s_cols + s_cur_col++] = CELL(vis[i + k], level);
		i += take;
		if (i < m) {
			newline_now();
			while (i < m && vis[i] == ' ')
				i++;
		}
	}
	s_pending_nl = true;
}

/* ====
 * Lines out: UART, ring, panel.
 */

#define TO_UART  1
#define TO_RING  2
#define TO_PANEL 4
#define TO_ALL   (TO_UART | TO_RING | TO_PANEL)

/* stdout as ESP-IDF set it up: UART0.  On the host, whatever the test
   gives (console_host_set_uart()), or nothing. */
static FILE *s_uart;
#ifdef CONSOLE_HOST
void console_host_set_uart(FILE *f)
{
	s_uart = f;
}
#endif

/* Lines to the UART as well: yes until console_uart_quiet(true), which the
   firmware calls as the PC starts unless the ini says [esp] uart_log = 1.
   Written straight out, a line at 115200 baud holds the core that logs it
   for about 87 us a byte; so while the PC runs with uart_log = 1 the lines
   go through the UART queue below instead.  The crash report ESP-IDF
   prints goes to the UART whatever this says. */
static volatile bool s_uart_quiet;

void console_uart_quiet(bool quiet)
{
	s_uart_quiet = quiet;
}

/* ====
 * The UART queue (v3.8).  After console_uart_async(true) a line for the
 * UART is copied into this queue, and the uart_log task on core 0 writes
 * it out as the UART's FIFO takes it, a tick (10 ms) at a time, so that
 * the task that logged it (the emulator's above all) never waits for the
 * UART.  A line that does not fit is dropped from the UART (not from the
 * ring or the panel) and counted, and once the queue has emptied the task
 * says how many.  console_uart_async(false) writes out what is queued
 * there and then, and lines go straight to the UART again: an emulator
 * abort, whose dump must be on the UART before the panic handler's report.
 */
#define UQ_SIZE 4096                    /* a power of two */
static char *s_uq;
static uint32_t s_uq_w, s_uq_r;         /* bytes ever put in, taken out */
static unsigned s_uq_dropped;           /* lines that did not fit */
static unsigned s_uq_said;              /* of those, how many were said */
static volatile bool s_uq_on;           /* changed with the console's lock */

#ifdef UQ_UART
static TaskHandle_t s_uq_task;
#define uq_wake() do { if (s_uq_task) xTaskNotifyGive(s_uq_task); } while (0)
static inline uint32_t uart_room(void)
{
	return uart_ll_get_txfifo_len(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM));
}
static inline void uart_put(char c)
{
	uart_ll_write_txfifo(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM),
			     (const uint8_t *) &c, 1);
}
#elif defined(CONSOLE_HOST)
#define uq_wake() do { } while (0)
static inline uint32_t uart_room(void)
{
	return s_host_room;
}
static inline void uart_put(char c)
{
	s_host_room--;
	if (s_uart)
		fputc(c, s_uart);
}
#else
/* a console that is not a UART: no queue (console_uart_async() does
   nothing) */
#define uq_wake() do { } while (0)
#endif

/* a line into the queue, or counted as dropped; no wait */
static void uq_put(const char *s, int n)
{
	bool put = false;
	uq_lock();
	if ((uint32_t) n + 1 <= UQ_SIZE - (s_uq_w - s_uq_r)) {
		for (int i = 0; i < n; i++)
			if (s[i] != '\r')
				s_uq[s_uq_w++ & (UQ_SIZE - 1)] = s[i];
		s_uq[s_uq_w++ & (UQ_SIZE - 1)] = '\n';
		put = true;
	} else {
		s_uq_dropped++;
	}
	uq_unlock();
	if (put)
		uq_wake();
}

#if defined(UQ_UART) || defined(CONSOLE_HOST)
/* as much of the queue as the FIFO has room for now, each '\n' as "\r\n"
   as ESP-IDF's console sends it (CONFIG_LIBC_STDOUT_LINE_ENDING_CRLF);
   returns the bytes left.  The FIFO is written only under the queue's lock,
   so two writers never overfill it. */
static uint32_t uq_out(void)
{
	uq_lock();
	uint32_t room = uart_room();
	while (s_uq_r != s_uq_w) {
		char c = s_uq[s_uq_r & (UQ_SIZE - 1)];
		if (room < (c == '\n' ? 2u : 1u))
			break;
		if (c == '\n') {
			uart_put('\r');
			room--;
		}
		uart_put(c);
		room--;
		s_uq_r++;
	}
	uint32_t left = s_uq_w - s_uq_r;
	uq_unlock();
	return left;
}

/* the lines dropped since this was last said, as a line of the queue's
   own; true if there were any */
static bool uq_say_dropped(void)
{
	uq_lock();
	unsigned n = s_uq_dropped - s_uq_said;
	s_uq_said = s_uq_dropped;
	uq_unlock();
	if (!n)
		return false;
	char line[96];
	int len = snprintf(line, sizeof(line), "(%u line%s dropped here: the UART could not "
			   "keep up; the ring and the panel have them)", n, n == 1 ? "" : "s");
	uq_put(line, len);
	return true;
}
#endif

#ifdef UQ_UART
static void uq_task(void *arg)
{
	(void) arg;
	for (;;) {
		if (uq_out()) {
			vTaskDelay(1);  /* the FIFO's 128 bytes last 11 ms */
			continue;
		}
		if (uq_say_dropped())
			continue;
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}
}
#endif

void console_uart_async(bool on)
{
#if defined(UQ_UART) || defined(CONSOLE_HOST)
	con_lock();
	if (on && !s_uq_on) {
		if (!s_uq)
			s_uq = con_alloc(UQ_SIZE);
#ifdef UQ_UART
		if (s_uq && !s_uq_task &&
		    xTaskCreatePinnedToCore(uq_task, "uart_log", 3072, NULL, 1, &s_uq_task, 0) != pdPASS)
			s_uq_task = NULL;
		s_uq_on = s_uq && s_uq_task;
#else
		s_uq_on = s_uq != NULL;
#endif
	} else if (!on && s_uq_on) {
		/* what is queued, now, by this task: the FIFO's room is waited
		   for here (an abort: nothing else is waiting for this core) */
		s_uq_on = false;
		while (uq_out()) {
#ifdef UQ_UART
			esp_rom_delay_us(200);
#else
			s_host_room = 128;
#endif
		}
	}
	con_unlock();
#else
	(void) on;
#endif
}

static void emit(int level, const char *s, int n, int to)
{
	if (level == CON_WARN || level == CON_ERROR)
		s_problems++;
	if (s_uart_quiet)
		to &= ~TO_UART;
	if ((to & TO_UART) && s_uq_on) {
		uq_put(s, n);
	} else if ((to & TO_UART) && s_uart) {
		fwrite(s, 1, n, s_uart);
		fputc('\n', s_uart);
	}
	if (to & TO_RING)
		ring_write_line(s, n);
	if (to & TO_PANEL) {
		grid_put_line(level, s, n);
		render();
	}
}

/* ESP_LOGx lines begin "E (1234) tag: " */
static int level_of(const char *s, int n)
{
	if (n >= 3 && s[1] == ' ' && s[2] == '(') {
		if (s[0] == 'E')
			return CON_ERROR;
		if (s[0] == 'W')
			return CON_WARN;
	}
	return CON_INFO;
}

#define CON_AUTO (-1)

/* ====
 * Lines that repeat (v3.8).  After console_log_limit(true), which the
 * firmware calls as the PC starts unless the ini says [esp] verbose_log =
 * 1, the lines that come through printf, fprintf(stderr) and ESP_LOGx (the
 * emulator's own debug lines, the guest BIOS's port 0x402 text, the
 * drivers': not console_printf()'s) are sorted by kind, a line's kind being
 * its text with every word that holds a digit taken as one and the same
 * ("inw 0xa20 <= 0xffff" and "inw 0x3f <= 0xffff" are one kind).  Of each
 * kind the first LIM_FIRST lines in LIM_PERIOD_MS go out as before; the
 * rest are counted, and when the period is over one line says how many,
 * with the last of them, at the level of the most serious of them (since
 * v3.9: held error lines give a red count).  So a guest that hits an unknown port thousands
 * of times a second costs a few lines every few seconds, on the UART, in
 * the ring (the crash screen's last lines) and on the panel, where every
 * line drawn while Ctrl+Alt+F12 is up costs a redraw.
 */
#define LIM_KINDS     16
#define LIM_FIRST     5
#define LIM_PERIOD_MS 5000
#define LIM_LAST      100

struct lim_kind {
	uint32_t kind;          /* 0: a free slot */
	uint32_t start_ms;      /* when its period began */
	uint32_t seen_ms;       /* its last line: the slot least used goes first */
	uint32_t shown;         /* lines out in this period */
	uint32_t held;          /* lines held back in this period */
	uint8_t level;          /* the most serious of them (CON_INFO, CON_WARN,
				   CON_ERROR): the count line's level */
	char last[LIM_LAST];    /* the last line held back */
};
static struct lim_kind *s_lim;
static bool s_lim_on;
static volatile bool s_lim_holding;     /* any kind has lines held back */
static volatile unsigned s_lim_total;   /* lines held back since boot */

static inline bool word_char(unsigned char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       c == '_';
}

/* the line's kind: FNV-1a over its text, each word with a digit in it as
   '#'; never 0 */
static uint32_t kind_of(const char *s, int n)
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < n; ) {
		if (!word_char(s[i])) {
			h = (h ^ (unsigned char) s[i++]) * 16777619u;
			continue;
		}
		int j = i;
		bool digit = false;
		for (; j < n && word_char(s[j]); j++)
			digit |= s[j] >= '0' && s[j] <= '9';
		if (digit)
			h = (h ^ '#') * 16777619u;
		else
			for (; i < j; i++)
				h = (h ^ (unsigned char) s[i]) * 16777619u;
		i = j;
	}
	return h ? h : 1;
}

/* one line for what a kind held back, and a new period for it */
static void lim_say(struct lim_kind *k, uint32_t now)
{
	if (k->held) {
		char line[LIM_LAST + 80];
		unsigned s = (now - k->start_ms + 999) / 1000;
		int n = snprintf(line, sizeof(line), "(not shown: %u more line%s like \"%s\" in %u s)",
				 (unsigned) k->held, k->held == 1 ? "" : "s", k->last, s ? s : 1);
		emit(k->level, line, n < (int) sizeof(line) ? n : (int) sizeof(line) - 1, TO_ALL);
	}
	k->start_ms = now;
	k->shown = 0;
	k->held = 0;
	k->level = CON_INFO;
}

/* true if the line (of the given level) goes out; with the console's lock
   held */
static bool lim_pass(int level, const char *s, int n)
{
	if (!s_lim_on)
		return true;
	uint32_t now = con_now_ms(), kind = kind_of(s, n);
	struct lim_kind *k = NULL, *spare = NULL;
	for (int i = 0; i < LIM_KINDS && !k; i++) {
		struct lim_kind *x = &s_lim[i];
		if (x->kind == kind)
			k = x;
		else if (!spare || (spare->kind && (!x->kind ||
						    now - x->seen_ms > now - spare->seen_ms)))
			spare = x;
	}
	if (!k) {
		/* a new kind: a free slot, or the one least lately used,
		   whose count is said first */
		k = spare;
		lim_say(k, now);
		k->kind = kind;
	} else if (now - k->start_ms >= LIM_PERIOD_MS) {
		lim_say(k, now);
	}
	k->seen_ms = now;
	if (k->shown < LIM_FIRST) {
		k->shown++;
		return true;
	}
	k->held++;
	if (level > k->level)
		k->level = level;
	s_lim_total++;
	s_lim_holding = true;
	int m = n < LIM_LAST - 1 ? n : LIM_LAST - 1;
	for (int i = 0; i < m; i++)
		k->last[i] = (unsigned char) s[i] < 0x20 ? ' ' : s[i];
	k->last[m] = 0;
	return false;
}

/* the counts of every kind whose period is over (all: every count) */
static void lim_say_due(bool all)
{
	uint32_t now = con_now_ms();
	bool holding = false;
	for (int i = 0; i < LIM_KINDS; i++) {
		struct lim_kind *k = &s_lim[i];
		if (k->held && (all || now - k->start_ms >= LIM_PERIOD_MS))
			lim_say(k, now);
		holding |= k->held != 0;
	}
	s_lim_holding = holding;
}

void console_log_limit(bool on)
{
	con_lock();
	if (on && !s_lim)
		s_lim = con_alloc(LIM_KINDS * sizeof(*s_lim));
	if (s_lim) {
		if (s_lim_on)
			lim_say_due(true);
		memset(s_lim, 0, LIM_KINDS * sizeof(*s_lim));
		s_lim_holding = false;
	}
	s_lim_on = on && s_lim;
	con_unlock();
}

void console_log_tick(void)
{
	if (!s_lim_holding)
		return;
	con_lock();
	if (s_lim_on)
		lim_say_due(false);
	con_unlock();
}

void console_log_stats(unsigned *held, unsigned *dropped)
{
	*held = s_lim_total;
	*dropped = s_uq_dropped;
}

/* text of one or more lines: "a\n" and "a" are one line, "" is an empty
   one; limited: through the limit on repeated lines */
static void emit_text(int level, const char *s, int to, bool limited)
{
	for (const char *p = s; ; ) {
		const char *e = strchr(p, '\n');
		int n = e ? (int) (e - p) : (int) strlen(p);
		if (!e && !n && p != s)
			break;          /* nothing after the last '\n' */
		int lvl = level == CON_AUTO ? level_of(p, n) : level;
		if (!limited || lim_pass(lvl, p, n))
			emit(lvl, p, n, to);
		if (!e)
			break;
		p = e + 1;
	}
}

static char s_fmt_buf[512];

void console_vprintf(int level, const char *fmt, va_list ap)
{
	con_lock();
	vsnprintf(s_fmt_buf, sizeof(s_fmt_buf), fmt, ap);
	emit_text(level, s_fmt_buf, TO_ALL, false);
	con_unlock();
}

void console_printf(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	console_vprintf(level, fmt, ap);
	va_end(ap);
}

void console_uart_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	con_lock();
	vsnprintf(s_fmt_buf, sizeof(s_fmt_buf), fmt, ap);
	emit_text(CON_INFO, s_fmt_buf, TO_UART, false);
	con_unlock();
	va_end(ap);
}

void console_puts(int level, const char *s)
{
	con_lock();
	emit_text(level, s, TO_ALL, false);
	con_unlock();
}

int console_problems(void)
{
	return s_problems;
}

void console_set_title(const char *title)
{
	if (!s_want)
		return;
	con_lock();
	int n = strlen(title);
	for (int c = 0; c < s_cols; c++)
		s_want[c] = CELL(c < n ? title[c] : ' ', CON_TITLE);
	render();
	con_unlock();
}

/* ====
 * The run before this reset.
 */

#define PREV_LINES 25
#define PREV_SIZE (PREV_LINES * 104)
static char *s_prev;
static int s_prev_lines;
static bool s_prev_crashed;

int console_previous_lines(void)
{
	return s_prev_lines;
}

bool console_previous_crashed(void)
{
	return s_prev_crashed;
}

void console_show_previous(const char *why)
{
	if (!s_prev || !s_prev_lines)
		return;
	con_lock();
	char head[160];
	snprintf(head, sizeof(head), "The last run ended with %s. Its last %d lines:",
		 why, s_prev_lines);
	emit(CON_WARN, head, strlen(head), TO_UART | TO_PANEL);
	/* not into the ring again: they would come back after the next reset */
	emit_text(CON_OLD, s_prev, TO_UART | TO_PANEL, false);
	const char *tail = "(end of the last run's lines)";
	emit(CON_OLD, tail, strlen(tail), TO_UART | TO_PANEL);
	con_unlock();
}

/* ====
 * Who has the panel.
 */

#ifdef CONSOLE_HOST
void display_lock(void) { }
void display_unlock(void) { }
static void display_trylock_draw(void)
{
	render_locked();
}
#else
void display_lock(void)
{
	if (s_display_mutex)
		xSemaphoreTake(s_display_mutex, portMAX_DELAY);
}

void display_unlock(void)
{
	if (s_display_mutex)
		xSemaphoreGive(s_display_mutex);
}

/* Called with the console's lock held.  Never waits long for the frame
   buffer: if a picture is being drawn that prints a line, waiting here
   for ever would wait for ourselves.  The text is kept, and drawn the next
   time. */
static void display_trylock_draw(void)
{
	if (s_display_mutex &&
	    xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
		return;
	if (s_owner_console)
		render_locked();
	display_unlock();
}
#endif

bool console_owns_display(void)
{
	return s_owner_console;
}

/* set once: the console keeps the panel for good */
static volatile bool s_held;

void console_release_display(void)
{
	/* Held (a fatal message, or the PC switched off): the panel stays the
	   console's, whoever asks (concurrency review, round 2).  A hold that
	   comes while this runs on another core is seen by the second look:
	   console_hold() sets s_held before it takes the panel, and both
	   flags are sequentially consistent, so either that look sees it, or
	   the hold's own take comes after the clear here. */
	if (__atomic_load_n(&s_held, __ATOMIC_SEQ_CST))
		return;
	con_lock();
	render();
	__atomic_store_n(&s_owner_console, false, __ATOMIC_SEQ_CST);
	con_unlock();
	if (__atomic_load_n(&s_held, __ATOMIC_SEQ_CST))
		console_take_display();         /* back, and drawn whole again */
}

void console_take_display(void)
{
	display_lock();
	__atomic_store_n(&s_owner_console, true, __ATOMIC_SEQ_CST);
	display_unlock();
	con_lock();
	if (s_shown) {
		/* the picture drew over everything: all of it again */
		for (int i = 0; i < s_rows * s_cols; i++)
			s_shown[i] = CELL_NONE;
		s_card_dirty = true;
	}
	render();
	con_unlock();
}

void console_hold(void)
{
	__atomic_store_n(&s_held, true, __ATOMIC_SEQ_CST);
	console_take_display();
}

bool console_held(void)
{
	return s_held;
}

void console_fatal(const char *fmt, ...)
{
	va_list ap;
	/* the end of the run: on the UART too */
	s_uart_quiet = false;
	va_start(ap, fmt);
	console_vprintf(CON_ERROR, fmt, ap);
	va_end(ap);
	console_puts(CON_ERROR, "tiny386 has stopped. Put this right, then press RESET.");
	console_hold();
#ifdef CONSOLE_HOST
	exit(3);
#else
	/* red 5 on the status LED, unless the caller named the error first
	   (no ini, a BIOS missing, the ini's problems: the first one stays) */
	status_led_fatal(STATUS_ERR_STOPPED);
	for (;;)
		vTaskDelay(portMAX_DELAY);
#endif
}

/* ====
 * Start-up.
 */

void console_init(uint16_t *fb, int width, int height, console_flush_fn *flush)
{
#ifndef CONSOLE_HOST
	s_con_mutex = xSemaphoreCreateRecursiveMutex();
	s_display_mutex = xSemaphoreCreateMutex();
	s_uart = stdout;
#endif
	/* what the last run left in the ring, before this run writes there */
	if (ring_valid()) {
		s_prev_crashed = s_ring.crashed == RING_CRASHED;
		s_prev = malloc(PREV_SIZE);
		if (s_prev)
			s_prev_lines = console_ring_tail(s_prev, PREV_SIZE, PREV_LINES);
	}
	ring_reset();

	if (!fb || width < CELL_W * 2 || height < CELL_H * 3 + CARD_H)
		return;
	s_fb = fb;
	s_w = width;
	s_h = height;
	s_flush = flush;
	s_cols = width / CELL_W;
	s_rows = (height - CARD_H) / CELL_H;
	s_want = con_alloc((size_t) s_cols * s_rows * sizeof(*s_want));
	s_shown = con_alloc((size_t) s_cols * s_rows * sizeof(*s_shown));
	if (!s_want || !s_shown) {
		free(s_want);
		free(s_shown);
		s_want = s_shown = NULL;
		return;
	}
	for (int i = 0; i < s_cols * s_rows; i++) {
		s_want[i] = CELL(' ', CON_INFO);
		s_shown[i] = CELL_NONE;
	}
	for (int c = 0; c < s_cols; c++)
		s_want[c] = CELL(' ', CON_TITLE);
	s_cur_row = 0;
	s_cur_col = 0;
	s_pending_nl = true;
	s_card_dirty = true;
	s_owner_console = true;
	render();
}

void console_set_fb(uint16_t *fb)
{
	con_lock();
	s_fb = fb;
	if (s_shown && s_owner_console) {
		for (int i = 0; i < s_rows * s_cols; i++)
			s_shown[i] = CELL_NONE;
		s_card_dirty = true;
	}
	con_unlock();
}

uint16_t *console_fb(int *width, int *height)
{
	if (width)
		*width = s_w;
	if (height)
		*height = s_h;
	return s_fb;
}

/* ====
 * The hooks.
 */

/* a line from printf, puts or fprintf(stderr), through the limit on
   repeated lines; with the console's lock held */
static void tee_line(int level, const char *s, int n)
{
	if (lim_pass(level, s, n))
		emit(level, s, n, TO_ALL);
}

#ifndef CONSOLE_HOST
static vprintf_like_t s_prev_vprintf;
static char s_log_buf[512];
static bool s_in_log;

/* ESP_LOGx.  Task context, any task, either core. */
static int con_log_vprintf(const char *fmt, va_list ap)
{
	if (!xPortCanYield())
		/* from an interrupt or a critical section: no locks, so the ROM's
		   printf, which takes none and reaches the UART and, through
		   con_rom_putc(), the ring */
		return esp_rom_vprintf(fmt, ap);
	con_lock();
	int n;
	if (s_in_log) {
		/* a line logged while one is on its way out: UART only (through
		   the queue while there is one: no wait) */
		if (s_uart_quiet) {
			n = 0;
		} else if (s_uq_on) {
			char line[160];
			n = vsnprintf(line, sizeof(line), fmt, ap);
			int len = n < (int) sizeof(line) ? n : (int) sizeof(line) - 1;
			while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
				len--;
			if (len > 0)
				uq_put(line, len);
		} else {
			n = vfprintf(s_uart, fmt, ap);
		}
	} else {
		s_in_log = true;
		n = vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, ap);
		emit_text(CON_AUTO, s_log_buf, TO_ALL, true);
		s_in_log = false;
	}
	con_unlock();
	return n;
}

/* printf, puts, fprintf(stderr): a character at a time, a line at a
   time out */
static char s_tee_line[256];
static int s_tee_len;

static int tee_put(char c, FILE *f)
{
	(void) f;
	con_lock();
	if (c == '\n') {
		s_tee_line[s_tee_len] = 0;
		tee_line(level_of(s_tee_line, s_tee_len), s_tee_line, s_tee_len);
		s_tee_len = 0;
	} else if (c != '\r') {
		if (s_tee_len >= (int) sizeof(s_tee_line) - 1) {
			tee_line(CON_INFO, s_tee_line, s_tee_len);
			s_tee_len = 0;
		}
		s_tee_line[s_tee_len++] = c;
	}
	con_unlock();
	return (unsigned char) c;
}

static int tee_flush(FILE *f)
{
	(void) f;
	return 0;
}

static FILE s_tee = FDEV_SETUP_STREAM(tee_put, NULL, tee_flush, _FDEV_SETUP_WRITE);

void console_hooks_install(void)
{
	s_uart = stdout;
	s_prev_vprintf = esp_log_set_vprintf(con_log_vprintf);
	(void) s_prev_vprintf;
	stdout = &s_tee;
	stderr = &s_tee;
	esp_rom_install_channel_putc(2, con_rom_putc);
}
#else
void console_hooks_install(void)
{
}

/* for the host test: what a last run would have left */
void console_host_ring_preset(const char *text, bool crashed)
{
	ring_reset();
	for (const char *p = text; *p; p++)
		ring_putc_locked(*p);
	if (crashed)
		s_ring.crashed = RING_CRASHED;
}

/* a line as the emulator's fprintf(stderr) gives it the tee, and the
   ESP_LOGx path: through the limit on repeated lines */
void console_host_tee_line(const char *s)
{
	tee_line(level_of(s, (int) strlen(s)), s, (int) strlen(s));
}

void console_host_log_line(const char *s)
{
	emit_text(CON_AUTO, s, TO_ALL, true);
}

/* the clock the limit's periods are counted on */
void console_host_set_ms(uint32_t ms)
{
	s_host_ms = ms;
}

/* the uart_log task's work: room bytes taken by the UART's FIFO, then the
   dropped lines said once the queue is empty; returns the bytes left */
uint32_t console_host_uart_drain(uint32_t room)
{
	s_host_room = room;
	uint32_t left = uq_out();
	if (!left && uq_say_dropped()) {
		s_host_room = room;
		left = uq_out();
	}
	return left;
}
#endif

/* ====
 * The crash screen.  Runs inside the panic handler: interrupts off, the
 * other core stopped, so no locks, no heap, no stdio; straight stores into
 * base.
 */

static char s_crash_tail[2048];

struct crash_text {
	uint16_t *base;
	int w, h, cols, rows;
	int row, col;
};

static void ct_newline(struct crash_text *t)
{
	t->row++;
	t->col = 0;
}

static void ct_put(struct crash_text *t, const char *s, uint16_t fg, uint16_t bg)
{
	for (; *s; s++) {
		if (*s == '\n') {
			ct_newline(t);
			continue;
		}
		if (t->col >= t->cols)
			ct_newline(t);
		if (t->row >= t->rows)
			return;
		char c = printable(*s) ? *s : '?';
		draw_cell(t->base, t->w, t->col * CELL_W, t->row * CELL_H,
			  (uint8_t) c, fg, bg, false);
		t->col++;
	}
}

static char *put_hex(char *p, uint32_t v)
{
	for (int i = 7; i >= 0; i--)
		*p++ = "0123456789abcdef"[(v >> (4 * i)) & 15];
	*p = 0;
	return p;
}

static char *put_str(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	*p = 0;
	return p;
}

/* the same line into the ring, for the next boot */
static void ring_note(const char *s)
{
	if (!ring_valid())
		ring_reset();
	while (*s)
		ring_putc_locked(*s++);
	ring_putc_locked('\n');
}

static char *put_dec(char *p, uint32_t v)
{
	char d[10];
	int n = 0;
	do {
		d[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	while (n)
		*p++ = d[--n];
	*p = 0;
	return p;
}

/* The lines the limit on repeated lines still holds back, said into the
   ring as lim_say() would say them, so that the crash screen's last lines
   have the newest of them and their count.  An emulator abort says them
   itself, first (console_log_limit(false)); any other crash (a device
   model's load fault, a watchdog, an assert) comes here with up to 5 s of
   each kind still held.  In the panic handler: no locks, no stdio. */
static void crash_say_held(void)
{
	if (!s_lim_on || !s_lim)
		return;
	for (int i = 0; i < LIM_KINDS; i++) {
		struct lim_kind *k = &s_lim[i];
		if (!k->kind || !k->held)
			continue;
		char line[LIM_LAST + 64], *p;
		p = put_str(line, "(not shown: ");
		p = put_dec(p, k->held);
		p = put_str(p, k->held == 1 ? " more line like \"" : " more lines like \"");
		for (int j = 0; j < LIM_LAST - 1 && k->last[j]; j++)
			*p++ = printable(k->last[j]) ? k->last[j] : '?';
		put_str(p, "\" before the crash)");
		ring_note(line);
		k->held = 0;    /* said */
	}
}

void console_draw_crash(uint16_t *base, int width, int height,
			const struct crash_report *r)
{
	const uint16_t white = RGB565(255, 255, 255);
	const uint16_t yellow = fg_of[CON_WARN];
	const uint16_t grey = fg_of[CON_INFO];
	const uint16_t red = RGB565(170, 0, 0);
	char line[160], *p;

	/* the log's last lines, before this report goes in, with what the limit
	   on repeated lines still held */
	crash_say_held();
	int tail = console_ring_tail(s_crash_tail, sizeof(s_crash_tail), 40);

	if (base) {
		uint32_t *d = (uint32_t *) base;
		for (int i = 0; i < width * height / 2; i++)
			d[i] = 0;
	}
	struct crash_text t = {
		.base = base, .w = width, .h = height,
		.cols = width / CELL_W, .rows = height / CELL_H,
	};
	if (!base)
		t.rows = 0;     /* the ring only */

	/* the title bar, the whole width */
	p = put_str(line, " tiny386 has stopped: a crash on core ");
	*p++ = '0' + (r->core & 7);
	*p = 0;
	ct_put(&t, line, white, red);
	while (t.col < t.cols && t.row < t.rows)
		ct_put(&t, " ", white, red);
	t.row = 2;
	t.col = 0;
	ring_note("CRASH (the crash screen was shown):");

	p = put_str(line, "What happened: ");
	put_str(p, r->what ? r->what : "unknown");
	ct_put(&t, line, white, 0);
	ring_note(line);
	ct_newline(&t);
	if (r->detail && r->detail[0]) {
		ct_put(&t, r->detail, yellow, 0);
		ring_note(r->detail);
		ct_newline(&t);
	}
	ct_newline(&t);

	p = put_str(line, "Where: mepc ");
	p = put_hex(p, r->mepc);
	p = put_str(p, "  ra ");
	p = put_hex(p, r->ra);
	p = put_str(p, "  sp ");
	p = put_hex(p, r->sp);
	p = put_str(p, "  mcause ");
	p = put_hex(p, r->mcause);
	p = put_str(p, "  mtval ");
	put_hex(p, r->mtval);
	ct_put(&t, line, white, 0);
	ring_note(line);
	ct_newline(&t);

	p = put_str(line, "Backtrace (code addresses on the stack, newest first):");
	if (!r->nbt)
		p = put_str(p, " none found");
	for (int i = 0; i < r->nbt && i < CRASH_BT_MAX; i++) {
		p = put_str(p, " ");
		p = put_hex(p, r->bt[i]);
	}
	ct_put(&t, line, white, 0);
	ring_note(line);
	ct_newline(&t);
	ct_put(&t, "To decode: riscv32-esp-elf-addr2line -pfiaC -e tiny386.elf "
	       "and the addresses above", grey, 0);
	ct_newline(&t);
	ct_newline(&t);

	/* as many of the log's last lines as fit, leaving two rows */
	int room = t.rows - t.row - 3;
	if (room > 0 && tail > 0) {
		ct_put(&t, "The last lines of the log:", white, 0);
		ct_newline(&t);
		/* skip the lines that do not fit, oldest first; a line longer
		   than the screen takes more than one row */
		int rows_needed = 0;
		const char *s = s_crash_tail;
#define ROWS_OF(len) ((len) ? ((len) + t.cols - 1) / t.cols : 1)
		for (const char *q = s; *q; ) {
			const char *e = strchr(q, '\n');
			int len = e ? (int) (e - q) : (int) strlen(q);
			rows_needed += ROWS_OF(len);
			q += len + (e ? 1 : 0);
		}
		while (rows_needed > room && *s) {
			const char *e = strchr(s, '\n');
			int len = e ? (int) (e - s) : (int) strlen(s);
			rows_needed -= ROWS_OF(len);
			s += len + (e ? 1 : 0);
		}
#undef ROWS_OF
		for (; *s && t.row < t.rows - 2; ) {
			const char *e = strchr(s, '\n');
			int len = e ? (int) (e - s) : (int) strlen(s);
			char one[104];
			while (len > 0) {
				int k = len < 100 ? len : 100;
				memcpy(one, s, k);
				one[k] = 0;
				ct_put(&t, one, grey, 0);
				s += k;
				len -= k;
			}
			ct_newline(&t);
			s += e ? 1 : 0;
		}
	}
	t.row = t.rows - 1;
	t.col = 0;
	ct_put(&t, "The full report is on the UART (115200 baud). Take a photo of "
	       "this screen, then press RESET.", yellow, 0);

	s_ring.crashed = RING_CRASHED;
}
