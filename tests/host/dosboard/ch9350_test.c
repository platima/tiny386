/*
 * Host test for esp/main/ch9350.c (frame parser and its adapter into
 * kbd_input.c), through esp/main/kbd_input.c and tiny386's own i8042.c built
 * for the host, as tests/host/test_kbd_input.c builds them (KBD_INPUT_HOST,
 * PS2_KBD_HOOKS).  What comes out is what a DOS program would read from
 * ports 0x60 and 0x64.  No kbd_input_poll() is called, so no typematic
 * repeat comes out: that is test_kbd_input's business.
 *
 * Frames: the two examples in the CH9350L manual V2.6 (DS26 p. 13), state 2
 * keyboard and mouse frames as DS26 pp. 12 to 13 lay them out, state 0 data
 * frames (DS26 p. 12), the attach sequence KVMS reports (ch9350.md 6.4), and
 * frames that are split, cut short, corrupted or buried in noise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ch9350.h"
#include "kbd_input.h"
#include "i8042.h"

static int failures, checks;
#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ---- the emulator's side: clock, PIC, i8042 ---- */

static uint32_t now_us = 1000000;
uint32_t get_uticks(void) { return now_us; }
static void set_irq(void *pic, int irq, int level) { (void)pic; (void)irq; (void)level; }
static void sys_reset(void *sys) { (void)sys; }

static KBDState *kc;
static PS2KbdState *kbd;
static PS2MouseState *mouse;

/* Everything the guest can read now, as text: "39 B9", mouse bytes as "m08". */
static char out[4096];
static void drain(void)
{
	char *p = out;

	out[0] = 0;
	for (int k = 0; k < 40; k++) {
		now_us += 1500;            /* lets the 0xE0 prefix's second byte out */
		kbd_step(kc);
		while (kbd_read_status(kc, 0x64) & 0x01) {
			int aux = kbd_read_status(kc, 0x64) & 0x20;
			unsigned v = kbd_read_data(kc, 0x60);
			p += sprintf(p, "%s%s%02X", p == out ? "" : " ", aux ? "m" : "", v);
		}
	}
}

static void guest_mouse_cmd(int b)
{
	kbd_write_command(kc, 0x64, 0xD4);
	kbd_write_data(kc, 0x60, b);
}

/* ---- parser under test ---- */

static ch9350_parser_t p;
static ch9350_input_t input = { .dev = -1 };
static int status_80, status_82, status_81, status_other;

static void status(void *ctx, uint8_t op, uint8_t arg)
{
	(void)ctx; (void)arg;
	if (op == 0x80) status_80++;
	else if (op == 0x82) status_82++;
	else if (op == 0x81) status_81++;
	else status_other++;
}

static void start(int mode, uint32_t guard_us)
{
	ch9350_sink_t sink = { 0 };

	/* one kbd_input.c keyboard for the chip, as ch9350_setup() opens it;
	   a fresh one each time, with nothing held */
	if (input.dev >= 0)
		kbd_input_close(input.dev);
	input.dev = kbd_input_open();
	input.buttons = 0;
	ch9350_input_sink(&sink, &input);
	sink.status = status;
	ch9350_parser_init(&p, mode, &sink, 100000, guard_us);
	status_80 = status_82 = status_81 = status_other = 0;
}

static void feed_hex(const char *hex)
{
	uint8_t b[512];
	int n = 0;
	unsigned v;
	const char *s = hex;
	int used;

	while (sscanf(s, "%x%n", &v, &used) == 1) {
		b[n++] = v;
		s += used;
	}
	ch9350_parser_feed(&p, b, n, now_us);
}

static void feed_bytes_one_by_one(const char *hex)
{
	unsigned v;
	const char *s = hex;
	int used;

	while (sscanf(s, "%x%n", &v, &used) == 1) {
		uint8_t b = v;
		now_us += 90;                /* about one byte time at 115200 */
		ch9350_parser_feed(&p, &b, 1, now_us);
		s += used;
	}
}

/* state 0 data frame: 57 AB 88 len label data seq sum */
static void feed_state0(uint8_t label, const uint8_t *data, int n, uint8_t seq, int corrupt)
{
	uint8_t f[80];
	int k = 0;
	uint8_t sum = seq;

	for (int i = 0; i < n; i++)
		sum += data[i];
	f[k++] = 0x57; f[k++] = 0xAB; f[k++] = 0x88;
	f[k++] = n + 3;
	f[k++] = label;
	memcpy(f + k, data, n);
	k += n;
	f[k++] = seq;
	f[k++] = sum + (corrupt ? 1 : 0);
	ch9350_parser_feed(&p, f, k, now_us);
}

#define EXPECT(want, what) do { drain(); \
	CHECK(strcmp(out, want) == 0, "%s: got \"%s\", want \"%s\"", what, out, want); \
	printf("  %-52s %s\n", what, out[0] ? out : "(nothing)"); } while (0)

int main(void)
{
	kc = i8042_init(&kbd, &mouse, 1, 12, NULL, set_irq, NULL, sys_reset);
	kbd_input_init();
	kbd_input_set_target(kbd, mouse);

	printf("state 2, keyboard:\n");
	start(2, 0);
	/* DS26 p. 13: "57 AB 01 00 00 2C 00 00 00 00 00" space pressed, then released */
	feed_hex("57 AB 01 00 00 2C 00 00 00 00 00");
	EXPECT("39", "DS26 example: space pressed");
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("B9", "DS26 example: all keys released");

	/* the chip sends each frame 3 or 4 times (ch9350.md 6.6) */
	for (int i = 0; i < 4; i++)
		feed_hex("57 AB 01 00 00 04 00 00 00 00 00");
	EXPECT("1E", "'a' down, frame sent four times");
	for (int i = 0; i < 3; i++)
		feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("9E", "'a' up, three times");

	feed_hex("57 AB 01 02 00 04 00 00 00 00 00");
	EXPECT("2A 1E", "left shift and 'a' in one frame: shift first");
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("9E AA", "both let go: 'a' then shift");

	feed_hex("57 AB 01 00 00 4F 00 00 00 00 00");
	EXPECT("E0 4D", "right arrow (0xE0 prefix)");
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("E0 CD", "right arrow up");

	feed_hex("57 AB 01 00 00 04 05 00 00 00 00");
	EXPECT("1E 30", "'a' and 'b'");
	feed_hex("57 AB 01 00 00 01 01 01 01 01 01");
	EXPECT("", "ErrorRollOver report ignored, keys stay down");
	feed_hex("57 AB 01 00 00 05 00 00 00 00 00");
	EXPECT("9E", "'a' up, 'b' still down");
	/* 0x86 at attach and detach: let go of everything */
	feed_hex("57 AB 86");
	EXPECT("B0", "0x86: 'b' released");

	printf("state 2, split and noisy input:\n");
	feed_bytes_one_by_one("57 AB 01 00 00 2C 00 00 00 00 00");
	EXPECT("39", "frame fed one byte at a time");
	feed_hex("57 AB 01 00 00");
	now_us += 3000;
	feed_hex("00 00 00 00 00 00");
	EXPECT("B9", "frame split over two reads 3 ms apart");
	feed_hex("00 FF 13 57 57 AB 01 00 00 04 00 00 00 00 00");
	EXPECT("1E", "junk, then a doubled 57, then a frame");
	feed_hex("57 AB 33 12 34 57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("9E", "unknown opcode 33: resync on the next 57 AB");
	CHECK(p.unknown_ops == 1, "unknown opcode counted");

	/* a frame that stops halfway, then silence: dropped on time */
	feed_hex("57 AB 01 00 00 2C");
	now_us += 200000;
	feed_hex("57 AB 01 00 00 04 00 00 00 00 00");
	EXPECT("1E", "cut-short frame, 200 ms gap, next frame");
	CHECK(p.timeouts == 1, "timeout counted (%u)", p.timeouts);
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("9E", "release");
	/* cut short with no gap: the next header lands inside the body; the
	   key code 0xAB is not a boot key, so the frame is thrown away */
	uint32_t bad0 = p.bad_frames;
	feed_hex("57 AB 01 00 00 2C 57 AB 01 00 00 04 00 00 00 00 00");
	EXPECT("", "cut-short frame, no gap: nothing pressed");
	CHECK(p.bad_frames == bad0 + 1, "bad frame counted");
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("", "parser back in step (all up, nothing was down)");
	feed_hex("57 AB 01 00 07 04 00 00 00 00 00");
	EXPECT("", "reserved byte not zero: thrown away");

	printf("state 2, mouse:\n");
	guest_mouse_cmd(0xF4);                       /* enable data reporting */
	EXPECT("mFA", "guest enables the mouse");
	/* dx 0x57, dy 0xAB (-85): a 57 AB inside a mouse frame is data */
	feed_hex("57 AB 02 00 57 AB 00 57 AB 01 00 00 04 00 00 00 00 00");
	/* the i8042 hands out keyboard bytes before mouse bytes */
	EXPECT("1E m08 m57 m55", "mouse dx +87 dy -85 (57 AB as data), then 'a'");
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	drain();
	feed_hex("57 AB 02 01 05 FB 00");
	EXPECT("m09 m05 m05", "left button, dx +5, dy -5 (up): PS/2 dy +5");
	feed_hex("57 AB 02 00 00 00 00");
	EXPECT("m08 m00 m00", "button up");
	feed_hex("57 AB 02 02 00 00 00 57 AB 86");
	EXPECT("m0A m00 m00 m08 m00 m00", "right button down, then 0x86 lets it go");
	/* IntelliMouse: 200, 100, 80 sample rates, then four-byte packets */
	guest_mouse_cmd(0xF3); guest_mouse_cmd(200);
	guest_mouse_cmd(0xF3); guest_mouse_cmd(100);
	guest_mouse_cmd(0xF3); guest_mouse_cmd(80);
	EXPECT("mFA mFA mFA mFA mFA mFA", "guest switches to IMPS/2");
	feed_hex("57 AB 02 00 00 00 01");
	EXPECT("m08 m00 m00 mFF", "wheel +1 (up) is PS/2 z -1, as sdl/main.c");
	feed_hex("57 AB 02 00 00 00 FE");
	EXPECT("m08 m00 m00 m02", "wheel -2 (down) is PS/2 z +2");

	printf("state 2, attach sequence and the zero-frame guard:\n");
	feed_hex("57 AB 86 57 AB 80 FF 57 AB 89 57 AB 80 FF 57 AB 87");
	CHECK(status_80 == 2 && status_other == 2, "attach: two 0x80 (each wants an LED frame), "
	      "0x89 and 0x87 (got %d, %d)", status_80, status_other);
	drain();
	start(2, 5000);
	feed_hex("57 AB 01 00 00 1A 00 00 00 00 00");     /* 'w' held */
	EXPECT("11", "'w' down");
	feed_hex("57 AB 02 00 03 00 00");
	now_us += 1000;
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");     /* zero frame 1 ms after a mouse frame */
	EXPECT("m08 m03 m00 m00", "guard on: zero frame 1 ms after mouse motion ignored");
	CHECK(p.zero_ignored == 1, "counted");
	now_us += 20000;
	feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
	EXPECT("91", "a zero frame 20 ms later is taken: 'w' up");

	printf("state 0 frames (fallback):\n");
	start(0, 0);
	{
		static const uint8_t a_down[8] = { 0, 0, 0x04 }, none[8] = { 0 };
		static const uint8_t id_a_down[9] = { 0x01, 0, 0, 0x04 }, id_none[9] = { 0x01 };
		static const uint8_t m_right[4] = { 0x00, 0x10, 0x00, 0x00 };
		static const uint8_t id_m_left[5] = { 0x02, 0x00, 0xF0, 0x00, 0x00 };
		feed_state0(0x11, a_down, 8, 1, 0);
		EXPECT("1E", "label 11 (keyboard, port 2): 'a'");
		feed_state0(0x11, none, 8, 2, 1);
		EXPECT("", "same with a bad sum: dropped");
		CHECK(p.bad_sums == 1, "bad sum counted");
		feed_state0(0x11, none, 8, 3, 0);
		EXPECT("9E", "release");
		feed_state0(0x13, id_a_down, 9, 4, 0);
		EXPECT("1E", "label 13 (HID, report ID first)");
		feed_state0(0x13, id_none, 9, 5, 0);
		EXPECT("9E", "release");
		feed_state0(0x20, m_right, 4, 6, 0);
		EXPECT("m08 m10 m00 m00", "label 20 mouse, dx +16 (IMPS/2 still on)");
		feed_state0(0x22, id_m_left, 5, 7, 0);
		EXPECT("m18 mF0 m00 m00", "label 22 mouse with report ID, dx -16");
		feed_hex("57 AB 82 A3");
		CHECK(status_82 == 1, "0x82 status request seen (wants an answer)");
		/* 0x81 device connection: port, length 5 (low byte first), payload, id, sum */
		/* 05 + 01 + 09 + 06 + A1 + 34 + 12 = 0xFC */
		feed_hex("57 AB 81 01 05 00 05 01 09 06 A1 34 12 FC");
		CHECK(status_81 == 1 && p.bad_sums == 1, "0x81 with a right sum (bad sums %u)", p.bad_sums);
		feed_hex("57 AB 81 01 05 00 05 01 09 06 A1 34 12 00");
		CHECK(status_81 == 1 && p.bad_sums == 2, "0x81 with a wrong sum");
		feed_state0(0x11, a_down, 8, 8, 0);
		EXPECT("1E", "in step after the 0x81 frames");
		feed_state0(0x11, none, 8, 9, 0);
		drain();
	}

	printf("LED frames:\n");
	{
		uint8_t f[CH9350_LED_FRAME_LEN];
		static const uint8_t caps2[11] = { 0x57, 0xAB, 0x12, 0, 0, 0, 0, 0x02, 0x07, 0xAC, 0x20 };
		static const uint8_t num0[11] = { 0x57, 0xAB, 0x12, 0, 0, 0, 0, 0x01, 0x00, 0xAC, 0x20 };
		ch9350_led_frame(f, ch9350_ps2_leds_to_usb(0x04), 2);
		CHECK(memcmp(f, caps2, 11) == 0, "Caps Lock, state 2");
		ch9350_led_frame(f, ch9350_ps2_leds_to_usb(0x02), 0);
		CHECK(memcmp(f, num0, 11) == 0, "Num Lock, state 0");
		CHECK(ch9350_ps2_leds_to_usb(0x01) == 0x04, "Scroll Lock: PS/2 bit 0 to USB bit 2");
		CHECK(ch9350_ps2_leds_to_usb(0x07) == 0x07, "all three");
		printf("  Caps Lock frame: ");
		ch9350_led_frame(f, ch9350_ps2_leds_to_usb(0x04), 2);
		for (int i = 0; i < 11; i++)
			printf("%02X ", f[i]);
		printf("\n");
	}

	printf("noise:\n");
	{
		/* a million random bytes: no crash, and a clean frame after a pause
		   is still understood */
		uint8_t buf[4096];
		uint32_t seed = 12345;
		start(2, 0);
		guest_mouse_cmd(0xF5);                   /* mouse reports off */
		drain();
		for (int round = 0; round < 256; round++) {
			for (int i = 0; i < (int)sizeof(buf); i++) {
				seed = seed * 1103515245u + 12345u;
				buf[i] = seed >> 16;
			}
			ch9350_parser_feed(&p, buf, sizeof(buf), now_us);
			now_us += 1000;
			drain();
		}
		now_us += 200000;
		feed_hex("57 AB 86");
		drain();
		feed_hex("57 AB 01 00 00 2C 00 00 00 00 00");
		EXPECT("39", "after 1 MiB of noise and a pause: space");
		feed_hex("57 AB 01 00 00 00 00 00 00 00 00");
		EXPECT("B9", "and up");
		printf("  noise counts: frames %u, resyncs %u, junk %u, unknown %u, bad %u\n",
		       p.frames, p.resyncs, p.junk_bytes, p.unknown_ops, p.bad_frames);
	}

	printf("ch9350_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
