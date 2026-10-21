/*
 * Host test of the PS/2 mouse as the ESP-IDF port builds i8042.c
 * (PS2_KBD_HOOKS): reports go into a ring, kbd_step() makes the packets on
 * the emulator's thread, whole or not at all, no faster than the guest's
 * sample rate, one button state a packet.  What the guest reads comes from
 * ports 60h and 64h, as a DOS or Windows driver reads them.
 *
 * Built twice by run.sh:
 *   without BUILD_ESP32, through kbd_input.c (KBD_INPUT_HOST): one thread,
 *     a fake clock; the backlog that put the packets out of step in v3
 *     (review, emulator correctness F1), clicks, pacing, remote mode,
 *     reset, the mouse off;
 *   with BUILD_ESP32 (THREAD_SAFE, the stand-in FreeRTOS of shim/) and
 *     TWO_THREADS: one thread reports as a USB task would, another runs
 *     kbd_step() and reads as the emulator would, with the real clock.
 *
 * Exit status 0 when every check passes.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "i8042.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static void set_irq(void *pic, int irq, int level) { (void) pic; (void) irq; (void) level; }
static void sys_reset(void *sys) { (void) sys; }

static KBDState *ctrl;
static PS2KbdState *kbd;
static PS2MouseState *mouse;

static void mouse_cmd(int v)
{
	kbd_write_command(ctrl, 0x64, 0xD4);
	kbd_write_data(ctrl, 0x60, v);
}

/* what the guest reads from port 60h while the status says there is
   something, mouse bytes only (none of these tests types) */
static int guest_read(uint8_t *buf, int max)
{
	int n = 0;
	while (n < max && (kbd_read_status(ctrl, 0x64) & 0x01))
		buf[n++] = (uint8_t) kbd_read_data(ctrl, 0x60);
	return n;
}

/* a stream of 3- or 4-byte packets the way a driver frames it: every first
   byte has bit 3 set, no overflow bits, the buttons as allowed */
static bool framed(const uint8_t *b, int n, int size, int buttons_allowed)
{
	if (n % size)
		return false;
	for (int i = 0; i < n; i += size)
		if (!(b[i] & 0x08) || (b[i] & 0xC0) || (b[i] & 0x07 & ~buttons_allowed))
			return false;
	return true;
}

#ifndef TWO_THREADS
/* ==== one thread, through kbd_input.c, a fake clock ==== */

#include "kbd_input.h"

static uint32_t now_us = 1000000;
uint32_t get_uticks(void)
{
	return now_us;
}

/* the emulator's loop for ms milliseconds, a step every millisecond, the
   guest reading everything as it comes; out gets the bytes, at[] the
   millisecond each packet's first byte came in */
static int run_ms(int ms, uint8_t *out, int max, int *at, int size)
{
	int n = 0;
	for (int t = 0; t < ms; t++) {
		now_us += 1000;
		kbd_step(ctrl);
		int got = guest_read(out + n, max - n);
		for (int i = 0; at && i < got; i++)
			if ((n + i) % size == 0)
				at[(n + i) / size] = t;
		n += got;
	}
	return n;
}

static void mouse_reset_enable(int size)
{
	uint8_t b[64];
	mouse_cmd(0xFF);
	if (size == 4) {
		static const int rates[3] = { 200, 100, 80 };
		for (int i = 0; i < 3; i++) {
			mouse_cmd(0xF3);
			mouse_cmd(rates[i]);
		}
		mouse_cmd(0xF3);
		mouse_cmd(100);
	}
	mouse_cmd(0xF4);
	run_ms(30, b, sizeof(b), NULL, 1);
}

/* The review's case (emulator correctness F1): the guest stops reading for
   1.6 s while the mouse keeps moving at 125 reports a second, reads four
   packets, then everything; the mouse then moves 5 down. */
static void test_backlog(int size)
{
	static uint8_t b[16384];
	int n = 0;

	printf("backlog, %d-byte packets:\n", size);
	mouse_reset_enable(size);
	for (int i = 0; i < 200; i++) {
		kbd_input_mouse(100, 0, 0, 0);
		for (int t = 0; t < 8; t++) {   /* the emulator runs; the guest does not read */
			now_us += 1000;
			kbd_step(ctrl);
		}
	}
	n += guest_read(b + n, 4 * size);
	kbd_input_mouse(1, 0, 0, 0);
	n += run_ms(2000, b + n, sizeof(b) - n, NULL, size);
	kbd_input_mouse(0, 5, 0, 0);
	n += run_ms(100, b + n, sizeof(b) - n, NULL, size);
	int sum = 0;
	for (int i = 0; i + size <= n; i += size)
		sum += (int8_t) b[i + 1];
	int last = n - size;
	printf("  %d bytes read, %d packets, %d counts right in all\n", n, n / size, sum);
	printf("  last packet:");
	for (int i = 0; i < size; i++)
		printf(" %02X", b[last + i]);
	printf(" (want 28 00 FB%s)\n", size == 4 ? " 00" : "");
	CHECK(framed(b, n, size, 0), "the stream is not whole %d-byte packets with no buttons", size);
	CHECK(last >= 0 && b[last] == 0x28 && b[last + 1] == 0x00 && b[last + 2] == 0xFB,
	      "last packet is not 5 down");
	/* 20,001 counts went in; what the guest had not read stayed within the
	   1,023-count backlog, so at least the queue's worth and the backlog
	   came out, and never more than went in */
	CHECK(sum <= 20001 && sum >= 1023, "counts right: %d", sum);
}

/* A press and a release in one report interval: two packets. */
static void test_click(void)
{
	uint8_t b[64];
	int at[16];

	printf("a click faster than a sample:\n");
	mouse_reset_enable(3);
	kbd_input_mouse(3, 0, 0, 1);
	kbd_input_mouse(2, 0, 0, 0);
	kbd_input_mouse(0, 0, 0, 2);
	kbd_input_mouse(0, 0, 0, 0);
	int n = run_ms(100, b, sizeof(b), at, 3);
	printf("  read:");
	for (int i = 0; i < n; i++)
		printf(" %02X", b[i]);
	printf("\n");
	CHECK(n == 12 && b[0] == 0x09 && b[1] == 3 && b[3] == 0x08 && b[4] == 2 &&
	      b[6] == 0x0A && b[9] == 0x08, "left press, release, right press, release");
	CHECK(n == 12 && at[1] - at[0] >= 9 && at[2] - at[1] >= 9 && at[3] - at[2] >= 9,
	      "packets closer than the 10 ms of 100 samples a second");
}

/* The guest's sample rate: 40 a second, so 25 ms between packets. */
static void test_rate(void)
{
	uint8_t b[256];
	int at[64];

	printf("sample rate 40:\n");
	mouse_reset_enable(3);
	mouse_cmd(0xF3);
	mouse_cmd(40);
	run_ms(30, b, sizeof(b), NULL, 1);      /* the ACKs */
	int n = 0, k = 0;
	for (int i = 0; i < 100; i++) {         /* 1,000 reports a second, 0.1 s */
		kbd_input_mouse(1, 1, 0, 0);
		n += run_ms(1, b + n, sizeof(b) - n, NULL, 3);
	}
	n += run_ms(300, b + n, sizeof(b) - n, NULL, 3);
	(void) at;
	int sx = 0, sy = 0;
	for (int i = 0; i + 3 <= n; i += 3) {
		sx += (int8_t) b[i + 1];
		sy += (int8_t) b[i + 2];
		k++;
	}
	printf("  100 reports in 100 ms: %d packets, %d right, %d down\n", k, sx, -sy);
	CHECK(framed(b, n, 3, 0) && sx == 100 && sy == -100, "movement kept whole");
	CHECK(k <= 6, "%d packets in 100 ms at 40 a second", k);
}

/* Remote mode: nothing until the guest polls; then ACK and one packet. */
static void test_remote(void)
{
	uint8_t b[64];

	printf("remote mode:\n");
	mouse_reset_enable(3);
	mouse_cmd(0xF0);
	int n = run_ms(20, b, sizeof(b), NULL, 1);
	CHECK(n == 1 && b[0] == 0xFA, "set remote: %d bytes", n);
	kbd_input_mouse(5, 0, 0, 1);
	n = run_ms(50, b, sizeof(b), NULL, 3);
	CHECK(n == 0, "remote mode sent %d bytes unasked", n);
	mouse_cmd(0xEB);
	n = run_ms(5, b, sizeof(b), NULL, 1);
	printf("  poll:");
	for (int i = 0; i < n; i++)
		printf(" %02X", b[i]);
	printf("\n");
	CHECK(n == 4 && b[0] == 0xFA && b[1] == 0x09 && b[2] == 5 && b[3] == 0,
	      "poll answers FA 09 05 00");
}

/* A reset drops what the guest had not had; the mouse off keeps nothing. */
static void test_reset_and_off(void)
{
	uint8_t b[512];

	printf("reset and off:\n");
	mouse_reset_enable(3);
	mouse_cmd(0xF3);
	mouse_cmd(10);                          /* 100 ms a packet */
	run_ms(20, b, sizeof(b), NULL, 1);
	kbd_input_mouse(500, 0, 0, 0);
	int n = run_ms(20, b, sizeof(b), NULL, 3);
	CHECK(n == 3 && b[1] == 127, "first packet of a big move: %d bytes", n);
	mouse_cmd(0xFF);
	n = run_ms(20, b, sizeof(b), NULL, 1);
	CHECK(n == 3 && b[0] == 0xFA && b[1] == 0xAA && b[2] == 0x00, "reset answer");
	mouse_cmd(0xF4);
	n = run_ms(500, b, sizeof(b), NULL, 1);
	CHECK(n == 1 && b[0] == 0xFA, "after the reset, %d bytes (the old move came back)", n);
	mouse_cmd(0xF5);                        /* off */
	run_ms(20, b, sizeof(b), NULL, 1);
	kbd_input_mouse(7, 7, 0, 1);
	run_ms(50, b, sizeof(b), NULL, 3);
	mouse_cmd(0xF4);
	n = run_ms(100, b, sizeof(b), NULL, 1);
	CHECK(n == 1 && b[0] == 0xFA, "a move while the mouse was off came out: %d bytes", n);
}

int main(void)
{
	ctrl = i8042_init(&kbd, &mouse, 1, 12, NULL, set_irq, NULL, sys_reset);
	kbd_input_init();
	kbd_input_set_target(kbd, mouse);

	test_backlog(3);
	test_backlog(4);
	test_click();
	test_rate();
	test_remote();
	test_reset_and_off();

	printf("test_ps2_mouse: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

#else /* TWO_THREADS */
/* ==== two threads: a reporting task and the emulator ==== */

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct HostMutex {
	pthread_mutex_t m;
	volatile int held;
};
int host_mutex_errors;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	struct HostMutex *h = calloc(1, sizeof(*h));
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&h->m, &a);
	pthread_mutexattr_destroy(&a);
	return h;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks)
{
	(void) ticks;
	if (pthread_mutex_lock(&h->m)) {
		__atomic_add_fetch(&host_mutex_errors, 1, __ATOMIC_SEQ_CST);
		return pdFALSE;
	}
	h->held = 1;
	return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
	h->held = 0;
	if (pthread_mutex_unlock(&h->m)) {
		__atomic_add_fetch(&host_mutex_errors, 1, __ATOMIC_SEQ_CST);
		return pdFALSE;
	}
	return pdTRUE;
}

int host_mutex_held(SemaphoreHandle_t h)
{
	return h->held;
}

/* kbd_input.c's, which this program does not link */
void ps2_kbd_leds_changed(int ps2_leds) { (void) ps2_leds; }
void ps2_kbd_rate_changed(int rate) { (void) rate; }

uint32_t get_uticks(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t) (ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

#define REPORTS 4000
static volatile int s_done;
static int sent_dx, sent_dy;

/* a USB task: 4,000 reports, one every 300 us or so (about 3,000 a
   second, far more than any mouse), the left or the right button pressed
   for 200 reports in every 400 */
static void *reporter(void *arg)
{
	(void) arg;
	for (int i = 0; i < REPORTS; i++) {
		int buttons = (i / 200) % 2 ? ((i / 400) % 2 ? 2 : 1) : 0;
		ps2_mouse_event(mouse, 1, 2, 0, buttons);
		sent_dx += 1;
		sent_dy += 2;
		usleep(300);
	}
	ps2_mouse_event(mouse, 0, 0, 0, 0);     /* the last press let go */
	__atomic_store_n(&s_done, 1, __ATOMIC_RELEASE);
	return NULL;
}

int main(void)
{
	static uint8_t b[1 << 20];
	int n = 0;
	pthread_t t;

	printf("two threads: a reporting task and the emulator:\n");
	ctrl = i8042_init(&kbd, &mouse, 1, 12, NULL, set_irq, NULL, sys_reset);
	mouse_cmd(0xF3);
	mouse_cmd(200);                 /* 5 ms a packet */
	mouse_cmd(0xF4);
	kbd_step(ctrl);
	n = guest_read(b, sizeof(b));
	CHECK(n == 3 && b[0] == 0xFA && b[1] == 0xFA && b[2] == 0xFA, "three ACKs: %d bytes", n);
	n = 0;

	pthread_create(&t, NULL, reporter, NULL);
	uint32_t quiet_since = 0;
	for (;;) {
		kbd_step(ctrl);
		int got = guest_read(b + n, sizeof(b) - n);
		n += got;
		if (__atomic_load_n(&s_done, __ATOMIC_ACQUIRE)) {
			uint32_t now = get_uticks();
			if (got)
				quiet_since = 0;
			else if (!quiet_since)
				quiet_since = now;
			else if (now - quiet_since > 300000)
				break;          /* nothing for 0.3 s after the last report */
		}
		usleep(50);
	}
	pthread_join(t, NULL);

	int sx = 0, sy = 0, presses = 0, last_buttons = 0;
	for (int i = 0; i + 3 <= n; i += 3) {
		sx += (int8_t) b[i + 1];
		sy += (int8_t) b[i + 2];
		if ((b[i] & 3) && !(last_buttons & 3))
			presses++;
		last_buttons = b[i] & 3;
	}
	printf("  %d reports in, %d packets out; %d right, %d down (sent %d, %d); "
	       "%d presses seen\n", REPORTS, n / 3, sx, -sy, sent_dx, sent_dy, presses);
	CHECK(framed(b, n, 3, 3), "the stream is not whole 3-byte packets");
	CHECK(sx == sent_dx && -sy == sent_dy, "movement lost or doubled");
	CHECK(last_buttons == 0, "the buttons end held: %d", last_buttons);
	/* ten presses went in, each held for 60 ms; one could merge into the
	   ring's overflow only if this thread stopped for that long */
	CHECK(presses >= 8 && presses <= 10, "%d presses came through, 10 went in", presses);
	CHECK(host_mutex_errors == 0, "%d mutex errors", host_mutex_errors);

	printf("test_ps2_mouse (two threads): %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
#endif /* TWO_THREADS */
