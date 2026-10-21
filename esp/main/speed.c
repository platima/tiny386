/*
 * How fast the emulator runs the guest: see speed.h.
 */
#include <stdio.h>
#include <string.h>

#include "speed.h"

/* ==== counting ==== */

void speed_init(struct speed *s, uint32_t now_us, uint32_t cycle)
{
	memset(s, 0, sizeof(*s));
	s->last_us = now_us;
	s->last_cycle = cycle;
	s->win_start = now_us;
}

int speed_step(struct speed *s, uint32_t now_us, uint32_t cycle, uint8_t m)
{
	int ev = 0;
	uint32_t done = cycle - s->last_cycle;
	uint32_t dt = now_us - s->last_us;

	s->last_cycle = cycle;
	s->last_us = now_us;
	s->insns += done;
	if (done)
		s->run_us += dt;
	else
		s->halt_us += dt;       /* the guest waited for an interrupt */

	if (now_us - s->win_start >= 1000000) {
		uint64_t di = s->insns - s->win_insns;
		uint64_t dr = s->run_us - s->win_run;
		uint64_t dh = s->halt_us - s->win_halt;
		s->kips = dr ? (uint32_t) (di * 1000 / dr) : 0;
		s->halt_pct = dr + dh ? (uint32_t) (dh * 100 / (dr + dh)) : 0;
		s->win_insns = s->insns;
		s->win_run = s->run_us;
		s->win_halt = s->halt_us;
		s->win_start = now_us;
		s->seconds++;
		ev |= SPEED_SECOND;
	}

	/* the Linux route's -marks: a new mark ends the piece before it.
	   This firmware watches the byte all the time, where the Linux route
	   does only when asked, so only a run that began with 0xA1 counts:
	   another program's stray 0xA2 to 0xAF there is no BENCH (review
	   round 2, emulator correctness 2) */
	if (m != s->mark && m >= 0xA1 && m <= 0xAF) {
		if (m == 0xA1) {
			/* BENCH starts, or starts again: a new table */
			s->marks_run = true;
			s->marks_done = false;
			s->nmarks = 0;
		} else if (s->marks_run && s->mark >= 0xA1 && s->mark != 0xAF &&
			   s->nmarks < SPEED_MAX_MARKS) {
			struct speed_mark *k = &s->marks[s->nmarks++];
			k->mark = s->mark;
			k->insns = s->insns - s->mark_insns;
			k->us = s->run_us - s->mark_us;
		}
		s->mark = m;
		s->mark_insns = s->insns;
		s->mark_us = s->run_us;
		if (m == 0xAF && s->marks_run) {
			s->marks_run = false;
			if (s->nmarks > 0) {
				s->marks_done = true;
				ev |= SPEED_MARKS_DONE;
			}
		}
	}
	return ev;
}

/* ==== the lines ==== */

/* a 64-bit number in decimal, whatever printf can do */
static const char *u64s(uint64_t v, char *buf)
{
	char t[24];
	int n = 0;
	do {
		t[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	for (int i = 0; i < n; i++)
		buf[i] = t[n - 1 - i];
	buf[n] = 0;
	return buf;
}

/* the names the Linux route's -marks gives the pieces of BENCH.COM */
const char *speed_mark_name(int mark)
{
	static const char *const names[16] = {
		[1] = "16-bit arithmetic in registers",
		[2] = "16-bit loads and stores",
		[3] = "16-bit calls and stack",
		[4] = "rep movsw and stosw, 32 MB",
		[5] = "DOS calls (int 21h)",
		[6] = "32-bit arithmetic, paging on",
		[7] = "32-bit loads and stores, paging on",
		[8] = "32-bit calls and stack, paging on",
		[9] = "translation cache miss every time",
	};
	const char *n = names[mark & 15];
	return n ? n : "";
}

/* seconds with two decimals, right-aligned in 7 like "%7.2f" */
static void secs2(uint64_t us, char *buf, size_t size)
{
	char a[24];
	uint64_t cs = (us + 5000) / 10000;
	char whole[48];
	snprintf(whole, sizeof(whole), "%s.%02u", u64s(cs / 100, a), (unsigned) (cs % 100));
	snprintf(buf, size, "%7s", whole);
}

void speed_mark_line(const struct speed *s, int i, char *buf, size_t size)
{
	const struct speed_mark *k = &s->marks[i];
	char t[64], a[24], b[24];
	secs2(k->us, t, sizeof(t));
	if (k->mark == 0xA4) {
		/* a few thousand instructions that move and fill 1000 times 32
		   KB: the bytes are what count */
		uint64_t kps = k->us ? (32768ull * 1000000 + k->us / 2) / k->us : 0;
		snprintf(buf, size, "tiny386: %02x %-34s  32768000 bytes        in %s s:"
			 " %5s thousand a second", k->mark, speed_mark_name(k->mark), t,
			 u64s(kps, a));
		return;
	}
	uint64_t kps = k->us ? (k->insns * 1000 + k->us / 2) / k->us : 0;
	snprintf(buf, size, "tiny386: %02x %-34s %9s instructions in %s s:"
		 " %5s thousand a second", k->mark, speed_mark_name(k->mark),
		 u64s(k->insns, a), t, u64s(kps, b));
}

void speed_total_line(uint64_t insns, uint64_t us, const char *what,
		      char *buf, size_t size)
{
	char a[24], b[24], c[24];
	uint64_t ds = (us + 50000) / 100000;            /* tenths of a second */
	uint64_t mil = us ? (insns * 1000 + us / 2) / us : 0;   /* thousandths */
	snprintf(buf, size, "tiny386: %s instructions in %s.%u s of running%s: "
		 "%s.%03u million a second", u64s(insns, a), u64s(ds / 10, b),
		 (unsigned) (ds % 10), what, u64s(mil / 1000, c), (unsigned) (mil % 1000));
}

/* ==== on the board ==== */

#ifndef SPEED_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../pc.h"
#include "common.h"
#include "console.h"
#ifdef LCD_ZERO_COPY
#include "lcd.h"
#endif
#ifdef USER_KEY_GPIO
#include "driver/gpio.h"
#endif

/* how often the figures go to the UART */
#define SPEED_LOG_S 10

/* the emulator's core only */
static struct speed s_speed;

/* the last whole second, for core 0: 32-bit words, each read whole */
static volatile uint32_t s_kips, s_halt_pct, s_seconds;
static volatile uint32_t s_total_kinsns, s_total_run_ms;
static volatile uint32_t s_flushes;      /* disk flushes, for the log */

/* screen requests, from the USB task or the emulator's core */
#define REQ_TOGGLE 1
#define REQ_SHOW 2
static int s_req;
static bool s_shown;            /* core 0 only, as below */
static const char *s_normal_title = "";
static uint32_t s_last_title_s, s_last_log_s;

void speed_run_init(void *opaque)
{
	PC *pc = opaque;
	speed_init(&s_speed, get_uticks(), (uint32_t) cpui386_get_cycle(pc->cpu));
}

void speed_forget_requests(void)
{
	/* a Ctrl+Alt+F12 from before the PC ran is not for its screen: called
	   before vga_task is let go, so that it never sees one */
	__atomic_store_n(&s_req, 0, __ATOMIC_RELEASE);
}

void speed_run_step(void *opaque, uint32_t now_us)
{
	PC *pc = opaque;
	int ev = speed_step(&s_speed, now_us, (uint32_t) cpui386_get_cycle(pc->cpu),
			    (uint8_t) pc->phys_mem[0x4f0]);
	if (ev & SPEED_SECOND) {
		s_kips = s_speed.kips;
		s_halt_pct = s_speed.halt_pct;
		s_total_kinsns = (uint32_t) (s_speed.insns / 1000);
		s_total_run_ms = (uint32_t) (s_speed.run_us / 1000);
		s_seconds = s_speed.seconds;
	}
	if (ev & SPEED_MARKS_DONE) {
		/* once, at the end of BENCH: to the UART, the ring and the
		   console, which is then shown */
		char line[160];
		console_printf(CON_GOOD, "BENCH.COM is done: %d pieces, timed as the "
			       "Linux route's -marks times them.", s_speed.nmarks);
		uint64_t ti = 0, tu = 0;
		for (int i = 0; i < s_speed.nmarks; i++) {
			speed_mark_line(&s_speed, i, line, sizeof(line));
			console_puts(CON_INFO, line);
			if (s_speed.marks[i].mark != 0xA4) {
				ti += s_speed.marks[i].insns;
				tu += s_speed.marks[i].us;
			}
		}
		speed_total_line(ti, tu, " in BENCH (without a4)", line, sizeof(line));
		console_puts(CON_GOOD, line);
		speed_total_line(s_speed.insns, s_speed.run_us, " since the PC started",
				 line, sizeof(line));
		console_puts(CON_GOOD, line);
		console_puts(CON_INFO, "Ctrl+Alt+F12 goes back to the PC.");
		__atomic_store_n(&s_req, REQ_SHOW, __ATOMIC_RELEASE);
	}
}

void speed_run_aside(uint32_t now_us)
{
	s_speed.last_us = now_us;
	s_flushes++;
}

void speed_hotkey(void)
{
	__atomic_store_n(&s_req, REQ_TOGGLE, __ATOMIC_RELEASE);
}

void speed_set_title(const char *normal)
{
	s_normal_title = normal;
}

static void figures(char *buf, size_t size, uint32_t pps)
{
	uint32_t k = s_kips;
	snprintf(buf, size, "%u.%03u million instructions a second, halted %u %%, "
		 "%u pictures a second", (unsigned) (k / 1000), (unsigned) (k % 1000),
		 (unsigned) s_halt_pct, (unsigned) pps);
}

static void show_title(uint32_t pps)
{
	char f[100], t[140];
	figures(f, sizeof(f), pps);
	snprintf(t, sizeof(t), " %s   Ctrl+Alt+F12: back", f);
	console_set_title(t);
}

/* the console off the panel, the PC's picture whole again: the frame
   buffer cleared (the console's text is outside the picture too) and one
   full redraw asked for */
static void give_back(PC *pc)
{
	console_set_title(s_normal_title);
	console_release_display();
#ifdef LCD_ZERO_COPY
	display_lock();
	if (globals.panel_fb) {
		memset(globals.panel_fb, 0, LCD_WIDTH * LCD_HEIGHT * 2);
		lcd_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);
	}
	if (pc)
		pc->full_update = 2;
	display_unlock();
#endif
}

void speed_screen_poll(void *opaque, uint32_t pps)
{
	PC *pc = opaque;
	int req = __atomic_exchange_n(&s_req, 0, __ATOMIC_ACQ_REL);
	/* a fatal message, or the PC switched off: the console stays, and
	   neither the hotkey nor the USER key gives the panel back */
	if (console_held())
		return;
	bool want = s_shown;
	if (req == REQ_TOGGLE)
		want = !s_shown;
	else if (req == REQ_SHOW)
		want = true;
#ifdef USER_KEY_GPIO
	/* held: shown; let go: the picture again */
	static bool key_init, key_was;
	if (!key_init) {
		gpio_config_t io = {
			.pin_bit_mask = 1ULL << USER_KEY_GPIO,
			.mode = GPIO_MODE_INPUT,
			.pull_up_en = GPIO_PULLUP_ENABLE,
		};
		gpio_config(&io);
		key_init = true;
	}
	bool key = gpio_get_level(USER_KEY_GPIO) == 0;
	if (key != key_was)
		want = key;
	key_was = key;
#endif
	uint32_t secs = s_seconds;
	if (want && !s_shown) {
		s_shown = true;
		console_take_display();
		char f[100];
		figures(f, sizeof(f), pps);
		console_printf(CON_INFO, "Speed: %s; %u million instructions in %u s of "
			       "running.", f, (unsigned) (s_total_kinsns / 1000),
			       (unsigned) (s_total_run_ms / 1000));
		show_title(pps);
		s_last_title_s = secs;
	} else if (!want && s_shown) {
		s_shown = false;
		give_back(pc);
	} else if (s_shown && secs != s_last_title_s) {
		show_title(pps);
		s_last_title_s = secs;
	}
	if (secs >= s_last_log_s + SPEED_LOG_S) {
		char f[100];
		figures(f, sizeof(f), pps);
		console_uart_printf("speed: %s; %u million instructions in %u s of "
				    "running; %u disk flushes", f,
				    (unsigned) (s_total_kinsns / 1000),
				    (unsigned) (s_total_run_ms / 1000), (unsigned) s_flushes);
		s_last_log_s = secs;
	}
}
#endif /* SPEED_HOST */
