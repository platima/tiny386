/*
 * Host test of esp/main/kbd_input.c (FIRMWARE-PLAN 3.3) with i8042.c as
 * the emulator builds it, without BUILD_ESP32 (no locks) and with
 * PS2_KBD_HOOKS: boot reports in, the bytes the guest reads out.
 *
 *   scancode set 1 for every key, against a table written out here and
 *     against what v2 sent through ps2_put_keycode() for the same key
 *     (usb_hid.c's table), apart from the keys v2 got wrong or dropped;
 *   Print Screen, Pause, Ctrl+Pause, Alt+Print Screen;
 *   order of presses and releases within one report;
 *   typematic repeat with a fake clock: the default, rates the guest sets
 *     with 0xF3, the reset, which key repeats, no repeat while the guest
 *     is not reading;
 *   two keyboards, release on close, release-all, ErrorRollOver;
 *   the lights (0xED) in USB order; the Ctrl+Alt hotkey; the boot menu's
 *     sink; no PC yet;
 *   the mouse: packets, the wheel's sign under IMPS/2, the clamp.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i8042.h"
#include "kbd_input.h"

int hid_usage_to_linux_keycode(int hid);        /* usb_hid.c, as v2 used it */

static uint32_t now_us = 1000000;
uint32_t get_uticks(void)
{
	return now_us;
}

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static void set_irq(void *pic, int irq, int level) { }
static void reset_request(void *sys) { }

static KBDState *ctrl;
static PS2KbdState *kbd;
static PS2MouseState *mouse;

static const char *hex(const uint8_t *b, int n)
{
	static char s[512];
	int o = 0;
	s[0] = 0;
	for (int i = 0; i < n && o < (int) sizeof(s) - 4; i++)
		o += sprintf(s + o, "%s%02X", i ? " " : "", b[i]);
	if (!n)
		strcpy(s, "(nothing)");
	return s;
}

/* What the guest reads: the controller's step runs (it sends the second
   byte of an E0 pair 1 to 10 ms after the first), and everything waiting is
   read.  Moves the clock on by 80 ms. */
static int drain(uint8_t *out, int max)
{
	int n = 0;
	for (int round = 0; round < 4; round++) {
		now_us += 20000;
		kbd_step(ctrl);
		while (ps2_kbd_pending(kbd) > 0 && n < max)
			out[n++] = ps2_read_data(kbd);
	}
	return n;
}

static bool expect(const char *what, const uint8_t *want, int nwant)
{
	uint8_t got[128];
	int n = drain(got, sizeof(got));
	bool ok = n == nwant && (!n || !memcmp(got, want, n));
	CHECK(ok, "%s: got %s, want %s", what, hex(got, n), hex(want, nwant));
	return ok;
}
#define EXPECT(what, ...) do { \
	static const uint8_t w_[] = { __VA_ARGS__ }; \
	if (expect(what, w_, sizeof(w_))) \
		printf("  %-44s %s\n", what, hex(w_, sizeof(w_))); \
} while (0)
#define EXPECT_NONE(what) do { \
	if (expect(what, NULL, 0)) \
		printf("  %-44s (nothing)\n", what); \
} while (0)

static void report(int dev, uint8_t mods, int nkeys, ...)
{
	uint8_t r[8] = { mods, 0 };
	va_list ap;
	va_start(ap, nkeys);
	for (int i = 0; i < nkeys && i < 6; i++)
		r[2 + i] = (uint8_t) va_arg(ap, int);
	va_end(ap);
	kbd_input_report(dev, r);
}

static void press(int dev, uint8_t mods, uint8_t u)
{
	report(dev, mods, 1, u);
}

static void up(int dev)
{
	report(dev, 0, 0);
}

/* ==== 1. every key ==== */

/* Scancode set 1, written out on its own here (usage, make code; E0 xx as
   0xE0xx), to catch a slip in kbd_input.c's table. */
static const struct { uint8_t usage; uint16_t code; const char *name; } table[] = {
	{0x04, 0x1E, "A"}, {0x05, 0x30, "B"}, {0x06, 0x2E, "C"}, {0x07, 0x20, "D"},
	{0x08, 0x12, "E"}, {0x09, 0x21, "F"}, {0x0A, 0x22, "G"}, {0x0B, 0x23, "H"},
	{0x0C, 0x17, "I"}, {0x0D, 0x24, "J"}, {0x0E, 0x25, "K"}, {0x0F, 0x26, "L"},
	{0x10, 0x32, "M"}, {0x11, 0x31, "N"}, {0x12, 0x18, "O"}, {0x13, 0x19, "P"},
	{0x14, 0x10, "Q"}, {0x15, 0x13, "R"}, {0x16, 0x1F, "S"}, {0x17, 0x14, "T"},
	{0x18, 0x16, "U"}, {0x19, 0x2F, "V"}, {0x1A, 0x11, "W"}, {0x1B, 0x2D, "X"},
	{0x1C, 0x15, "Y"}, {0x1D, 0x2C, "Z"},
	{0x1E, 0x02, "1"}, {0x1F, 0x03, "2"}, {0x20, 0x04, "3"}, {0x21, 0x05, "4"},
	{0x22, 0x06, "5"}, {0x23, 0x07, "6"}, {0x24, 0x08, "7"}, {0x25, 0x09, "8"},
	{0x26, 0x0A, "9"}, {0x27, 0x0B, "0"},
	{0x28, 0x1C, "Enter"}, {0x29, 0x01, "Esc"}, {0x2A, 0x0E, "Backspace"},
	{0x2B, 0x0F, "Tab"}, {0x2C, 0x39, "Space"}, {0x2D, 0x0C, "-"},
	{0x2E, 0x0D, "="}, {0x2F, 0x1A, "["}, {0x30, 0x1B, "]"}, {0x31, 0x2B, "\\"},
	{0x32, 0x2B, "ISO #"}, {0x33, 0x27, ";"}, {0x34, 0x28, "'"},
	{0x35, 0x29, "`"}, {0x36, 0x33, ","}, {0x37, 0x34, "."}, {0x38, 0x35, "/"},
	{0x39, 0x3A, "Caps Lock"},
	{0x3A, 0x3B, "F1"}, {0x3B, 0x3C, "F2"}, {0x3C, 0x3D, "F3"}, {0x3D, 0x3E, "F4"},
	{0x3E, 0x3F, "F5"}, {0x3F, 0x40, "F6"}, {0x40, 0x41, "F7"}, {0x41, 0x42, "F8"},
	{0x42, 0x43, "F9"}, {0x43, 0x44, "F10"}, {0x44, 0x57, "F11"}, {0x45, 0x58, "F12"},
	{0x47, 0x46, "Scroll Lock"},
	{0x49, 0xE052, "Insert"}, {0x4A, 0xE047, "Home"}, {0x4B, 0xE049, "Page Up"},
	{0x4C, 0xE053, "Delete"}, {0x4D, 0xE04F, "End"}, {0x4E, 0xE051, "Page Down"},
	{0x4F, 0xE04D, "Right"}, {0x50, 0xE04B, "Left"}, {0x51, 0xE050, "Down"},
	{0x52, 0xE048, "Up"},
	{0x53, 0x45, "Num Lock"}, {0x54, 0xE035, "KP /"}, {0x55, 0x37, "KP *"},
	{0x56, 0x4A, "KP -"}, {0x57, 0x4E, "KP +"}, {0x58, 0xE01C, "KP Enter"},
	{0x59, 0x4F, "KP 1"}, {0x5A, 0x50, "KP 2"}, {0x5B, 0x51, "KP 3"},
	{0x5C, 0x4B, "KP 4"}, {0x5D, 0x4C, "KP 5"}, {0x5E, 0x4D, "KP 6"},
	{0x5F, 0x47, "KP 7"}, {0x60, 0x48, "KP 8"}, {0x61, 0x49, "KP 9"},
	{0x62, 0x52, "KP 0"}, {0x63, 0x53, "KP ."},
	{0x64, 0x56, "ISO \\ (beside left Shift)"}, {0x65, 0xE05D, "Menu"},
	{0xE0, 0x1D, "Left Ctrl"}, {0xE1, 0x2A, "Left Shift"}, {0xE2, 0x38, "Left Alt"},
	{0xE3, 0xE05B, "Left Windows"}, {0xE4, 0xE01D, "Right Ctrl"},
	{0xE5, 0x36, "Right Shift"}, {0xE6, 0xE038, "Right Alt (AltGr)"},
	{0xE7, 0xE05C, "Right Windows"},
};

static int code_bytes(uint16_t code, bool down, uint8_t *b)
{
	int n = 0;
	if (code >= 0xE000)
		b[n++] = 0xE0;
	b[n++] = (code & 0xff) | (down ? 0 : 0x80);
	return n;
}

static void test_every_key(int dev)
{
	printf("every key, press and release (scancode set 1):\n");
	int bad = 0;
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		uint8_t u = table[i].usage, want[8], got[32];
		int nw = code_bytes(table[i].code, true, want);
		nw += code_bytes(table[i].code, false, want + nw);
		if (u >= 0xE0)
			report(dev, 1 << (u - 0xE0), 0);
		else
			press(dev, 0, u);
		int n = drain(got, sizeof(got));
		up(dev);
		n += drain(got + n, sizeof(got) - n);
		bool ok = n == nw && !memcmp(got, want, n);
		CHECK(ok, "usage %02X %s: got %s, want %s", u, table[i].name, hex(got, n),
		      hex(want, nw));
		if (!ok)
			bad++;
	}
	printf("  %d keys, %d wrong\n", (int) (sizeof(table) / sizeof(table[0])), bad);

	/* every usage of the keyboard page: what kbd_input.c sends against what
	   v2 sent (usb_hid.c's Linux codes through ps2_put_keycode()).  The
	   differences have to be exactly the keys v2 got wrong or dropped. */
	printf("against v2's path (usb_hid.c's table into ps2_put_keycode()):\n");
	int same = 0, differ = 0;
	for (int u = 0x04; u <= 0xE7; u++) {
		if (u > 0x65 && u < 0xE0)
			continue;
		uint8_t a[32], b[32];
		int na, nb;
		if (u >= 0xE0)
			report(dev, 1 << (u - 0xE0), 0);
		else
			press(dev, 0, u);
		na = drain(a, sizeof(a));
		up(dev);
		na += drain(a + na, sizeof(a) - na);
		int lc = hid_usage_to_linux_keycode(u);
		nb = 0;
		if (lc) {
			ps2_put_keycode(kbd, 1, lc);
			nb = drain(b, sizeof(b));
			ps2_put_keycode(kbd, 0, lc);
			nb += drain(b + nb, sizeof(b) - nb);
		}
		if (na == nb && !memcmp(a, b, na)) {
			same++;
			continue;
		}
		differ++;
		printf("  usage %02X: now %s;", u, hex(a, na));
		printf(" v2 %s\n", hex(b, nb));
		/* the ones that were wrong: ISO # sent ISO \'s code, ISO \ and
		   Menu were dropped, Print Screen and Pause were dropped */
		CHECK(u == 0x32 || u == 0x64 || u == 0x65 || u == 0x46 || u == 0x48,
		      "usage %02X differs from v2 and is not one of the known fixes", u);
	}
	printf("  %d usages the same as v2, %d different (all known fixes)\n", same, differ);
	CHECK(differ == 5, "%d usages differ from v2, 5 expected", differ);
}

static void test_sequences(int dev)
{
	printf("sequences:\n");
	press(dev, 0, 0x46);
	EXPECT("Print Screen pressed", 0xE0, 0x2A, 0xE0, 0x37);
	up(dev);
	EXPECT("Print Screen let go", 0xE0, 0xB7, 0xE0, 0xAA);
	press(dev, KBD_MOD_LALT, 0);
	EXPECT("Left Alt", 0x38);
	press(dev, KBD_MOD_LALT, 0x46);
	EXPECT("Alt+Print Screen (SysRq)", 0x54);
	up(dev);
	EXPECT("both let go", 0xD4, 0xB8);
	press(dev, KBD_MOD_LSHIFT, 0x46);
	EXPECT("Shift+Print Screen", 0x2A, 0xE0, 0x37);
	up(dev);
	EXPECT("both let go", 0xE0, 0xB7, 0xAA);
	press(dev, 0, 0x48);
	EXPECT("Pause pressed", 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5);
	up(dev);
	EXPECT_NONE("Pause let go");
	press(dev, KBD_MOD_LCTRL, 0x48);
	EXPECT("Ctrl+Pause (Break)", 0x1D, 0xE0, 0x46, 0xE0, 0xC6);
	up(dev);
	EXPECT("both let go", 0x9D);

	printf("order within one report:\n");
	press(dev, KBD_MOD_LSHIFT, 0x04);
	EXPECT("Shift and A in one report", 0x2A, 0x1E);
	up(dev);
	EXPECT("both let go in one", 0x9E, 0xAA);
	press(dev, KBD_MOD_LSHIFT, 0x04);
	drain((uint8_t[16]){0}, 16);
	press(dev, 0, 0x05);
	EXPECT("A and Shift up, B down, in one", 0x9E, 0xAA, 0x30);
	up(dev);
	EXPECT("B let go", 0xB0);
	report(dev, 0, 2, 0x04, 0x04);
	EXPECT("A twice in one report: once", 0x1E);
	up(dev);
	EXPECT("let go: once", 0x9E);
	report(dev, 0, 1, 0x03);
	EXPECT_NONE("usage 3 (ErrorUndefined): not a key");
}

/* ==== 2. typematic ==== */

/* holds the key from now for ms milliseconds, polling every millisecond
   as a fine-grained poller would; returns how many makes of code arrived
   after the first, and when (ms from the press) */
static int hold(uint8_t code, int ms, int *at, int max)
{
	uint32_t t0 = now_us;
	int n = 0;
	for (int t = 1; t <= ms; t++) {
		now_us = t0 + t * 1000;
		kbd_input_poll();
		while (ps2_kbd_pending(kbd) > 0) {
			uint8_t b = ps2_read_data(kbd);
			if (b == code && n < max)
				at[n++] = t;
		}
	}
	return n;
}

static void typematic_case(int dev, const char *what, int rate, int want_delay_ms,
			   double want_period_ms)
{
	uint8_t junk[16];
	if (rate >= 0) {
		ps2_write_keyboard(kbd, 0xF3);
		ps2_write_keyboard(kbd, rate);
		int n = drain(junk, sizeof(junk));
		CHECK(n == 2 && junk[0] == 0xFA && junk[1] == 0xFA,
		      "0xF3 0x%02X: %s, want FA FA", rate, hex(junk, n));
	}
	press(dev, 0, 0x04);
	uint8_t first = ps2_read_data(kbd);
	CHECK(first == 0x1E, "A pressed: %02X", first);
	int at[64];
	int span = want_delay_ms + 1000;
	int n = hold(0x1E, span, at, 64);
	up(dev);
	drain(junk, sizeof(junk));
	double period = n > 1 ? (double) (at[n - 1] - at[0]) / (n - 1) : 0;
	int want_n = 1 + (int) ((span - want_delay_ms) / want_period_ms);
	printf("  %-30s first repeat at %d ms, then every %.2f ms, %d in %d ms "
	       "(want %d ms, %.2f ms, %d)\n", what, n ? at[0] : -1, period, n, span,
	       want_delay_ms, want_period_ms, want_n);
	CHECK(n > 0 && at[0] >= want_delay_ms && at[0] <= want_delay_ms + 1,
	      "%s: first repeat at %d ms, want %d", what, n ? at[0] : -1, want_delay_ms);
	/* the polls are a millisecond apart, so the last repeat is seen up to
	   1 ms late: the mean period is good to 1 ms over the repeats */
	double tol = n > 1 ? 1.0 / (n - 1) + 0.001 : 0;
	CHECK(n >= 2 && period >= want_period_ms - tol && period <= want_period_ms + tol,
	      "%s: period %.2f ms, want %.2f", what, period, want_period_ms);
	CHECK(n == want_n, "%s: %d repeats, want %d", what, n, want_n);
}

static void test_typematic(int dev)
{
	uint8_t junk[64];
	int at[64];
	printf("typematic repeat (fake clock, polled every millisecond):\n");
	CHECK(kbd_input_poll() == -1, "nothing held: poll should say -1");
	typematic_case(dev, "default", -1, 500, 91.674);
	typematic_case(dev, "0xF3 0x00 (fastest)", 0x00, 250, 33.336);
	typematic_case(dev, "0xF3 0x7F (slowest)", 0x7F, 1000, 500.04);
	typematic_case(dev, "0xF3 0x2B (the default)", 0x2B, 500, 91.674);
	ps2_write_keyboard(kbd, 0xF3);
	ps2_write_keyboard(kbd, 0x00);
	drain(junk, sizeof(junk));
	ps2_write_keyboard(kbd, 0xFF);
	int n = drain(junk, sizeof(junk));
	CHECK(n == 2 && junk[0] == 0xFA && junk[1] == 0xAA, "reset: %s", hex(junk, n));
	typematic_case(dev, "after reset (0xFF)", -1, 500, 91.674);

	/* the last key pressed repeats, and only it */
	press(dev, 0, 0x04);
	drain(junk, sizeof(junk));
	report(dev, 0, 2, 0x04, 0x05);
	drain(junk, sizeof(junk));
	n = hold(0x1E, 1500, at, 64);
	int nb = 0;
	CHECK(n == 0, "A held while B pressed after it: A repeated %d times", n);
	report(dev, 0, 1, 0x04);       /* B let go, A still down */
	drain(junk, sizeof(junk));
	n = hold(0x1E, 1500, at, 64);
	CHECK(n == 0, "B let go, A still held: A repeated %d times (a real "
	      "keyboard does not go back to it)", n);
	up(dev);
	drain(junk, sizeof(junk));
	printf("  only the last key pressed repeats; letting it go does not "
	       "start the one before\n");

	report(dev, KBD_MOD_LSHIFT, 0);
	drain(junk, sizeof(junk));
	n = hold(0x2A, 1500, at, 64);
	CHECK(n == 0, "Shift repeated %d times", n);
	up(dev);
	drain(junk, sizeof(junk));
	press(dev, 0, 0x39);
	drain(junk, sizeof(junk));
	n = hold(0x3A, 1500, at, 64);
	CHECK(n == 0, "Caps Lock repeated %d times", n);
	up(dev);
	drain(junk, sizeof(junk));
	printf("  modifiers and lock keys do not repeat\n");

	/* an arrow repeats with its E0: pressed, then repeats at 500, 591.7
	   and 683.3 ms, each E0 at once and 4D 10 ms later */
	press(dev, 0, 0x4F);
	uint32_t t0 = now_us;
	uint8_t seq[64];
	int ns = 0;
	for (int t = 1; t <= 700; t++) {
		now_us = t0 + t * 1000;
		kbd_input_poll();
		kbd_step(ctrl);
		while (ps2_kbd_pending(kbd) > 0 && ns < 64)
			seq[ns++] = ps2_read_data(kbd);
	}
	up(dev);
	drain(junk, sizeof(junk));
	CHECK(ns == 8 && !memcmp(seq, "\xE0\x4D\xE0\x4D\xE0\x4D\xE0\x4D", 8),
	      "Right arrow held 700 ms: %s, want E0 4D four times", hex(seq, ns));
	printf("  Right held 700 ms: %s\n", hex(seq, ns));

	/* a guest that is not reading: no more than a few repeats wait */
	press(dev, 0, 0x04);
	t0 = now_us;
	for (int t = 1; t <= 3000; t++) {
		now_us = t0 + t * 1000;
		kbd_input_poll();
	}
	int pend = ps2_kbd_pending(kbd);
	CHECK(pend <= KBD_REPEAT_MAX_PENDING, "%d bytes waiting after 3 s unread", pend);
	printf("  guest not reading for 3 s: %d bytes waiting (at most %d)\n", pend,
	       KBD_REPEAT_MAX_PENDING);
	up(dev);
	drain(junk, sizeof(junk));
	(void) nb;
}

/* ==== 3. keyboards, lights, hotkey, sink ==== */

static int led_calls;
static uint8_t led_last;
static void leds(uint8_t usb)
{
	led_calls++;
	led_last = usb;
}

static int hotkeys;
static void hotkey(void)
{
	hotkeys++;
}

static int hotkeys2;
static void hotkey2(void)
{
	hotkeys2++;
}

static int hotkeys3;
static void hotkey3(void)
{
	hotkeys3++;
}

static int sunk;
static uint8_t sunk_last, sunk_mods;
static void sink(uint8_t u, uint8_t mods)
{
	sunk++;
	sunk_last = u;
	sunk_mods = mods;
}

static void test_keyboards(void)
{
	uint8_t junk[64];
	printf("two keyboards:\n");
	int a = kbd_input_open(), b = kbd_input_open();
	CHECK(a >= 0 && b >= 0 && a != b, "open: %d %d", a, b);
	press(a, 0, 0x04);
	EXPECT("A down on keyboard 1", 0x1E);
	press(b, 0, 0x04);
	EXPECT_NONE("A down on keyboard 2 as well");
	up(a);
	EXPECT_NONE("keyboard 1 lets go: still held on 2");
	up(b);
	EXPECT("keyboard 2 lets go", 0x9E);
	report(a, KBD_MOD_LSHIFT, 1, 0x05);
	EXPECT("keyboard 1: Shift and B", 0x2A, 0x30);
	press(b, 0, 0x06);
	EXPECT("keyboard 2: C", 0x2E);
	kbd_input_close(a);
	EXPECT("keyboard 1 unplugged: its keys go up", 0xB0, 0xAA);
	up(b);
	EXPECT("keyboard 2 lets go of C", 0xAE);
	a = kbd_input_open();
	press(a, 0, 0x06);
	press(b, 0, 0x07);
	drain(junk, sizeof(junk));
	kbd_input_release_all();
	EXPECT("release all (C on 1, D on 2)", 0xAE, 0xA0);
	press(a, 0, 0x04);
	drain(junk, sizeof(junk));
	report(a, 0, 6, 1, 1, 1, 1, 1, 1);
	EXPECT_NONE("ErrorRollOver report: ignored");
	up(a);
	EXPECT("then all keys up: A goes up", 0x9E);
	kbd_input_close(a);
	kbd_input_close(b);
	int all[KBD_INPUT_DEVS + 1], got = 0;
	for (int i = 0; i < KBD_INPUT_DEVS + 1; i++)
		if ((all[i] = kbd_input_open()) >= 0)
			got++;
	CHECK(got == KBD_INPUT_DEVS, "%d slots, want %d", got, KBD_INPUT_DEVS);
	for (int i = 0; i < KBD_INPUT_DEVS + 1; i++)
		kbd_input_close(all[i]);

	printf("lights (0xED from the guest, handed on in USB order):\n");
	kbd_input_set_leds_fn(leds);
	static const struct { int ps2; uint8_t usb; const char *what; } l[] = {
		{0x02, KBD_LED_NUM, "Num"}, {0x04, KBD_LED_CAPS, "Caps"},
		{0x01, KBD_LED_SCROLL, "Scroll"}, {0x07, 0x07, "all three"},
		{0x00, 0x00, "none"},
	};
	for (unsigned i = 0; i < sizeof(l) / sizeof(l[0]); i++) {
		int before = led_calls;
		ps2_write_keyboard(kbd, 0xED);
		ps2_write_keyboard(kbd, l[i].ps2);
		int n = drain(junk, sizeof(junk));
		CHECK(n == 2 && junk[0] == 0xFA && junk[1] == 0xFA, "ED: %s", hex(junk, n));
		CHECK(led_calls == before + 1 && led_last == l[i].usb,
		      "%s: PS/2 %02X gave USB %02X (%d calls), want %02X", l[i].what,
		      l[i].ps2, led_last, led_calls - before, l[i].usb);
		printf("  PS/2 0x%02X (%s) -> USB 0x%02X\n", l[i].ps2, l[i].what, led_last);
	}
	int before = led_calls;
	ps2_write_keyboard(kbd, 0xED);
	ps2_write_keyboard(kbd, 0x00);
	drain(junk, sizeof(junk));
	CHECK(led_calls == before, "the same lights again: handed on again");
	CHECK(kbd_input_leds() == 0, "kbd_input_leds() %02X", kbd_input_leds());

	printf("Ctrl+Alt+F12 (the speed screen):\n");
	kbd_input_set_hotkey(HID_KEY_F12, hotkey);
	a = kbd_input_open();
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 0);
	EXPECT("Ctrl and Alt down", 0x1D, 0x38);
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 1, HID_KEY_F12);
	EXPECT_NONE("F12 with them: not to the PC");
	CHECK(hotkeys == 1, "hotkey called %d times", hotkeys);
	up(a);
	EXPECT("all let go: F12's release not sent either", 0x9D, 0xB8);
	press(a, 0, HID_KEY_F12);
	EXPECT("F12 alone goes to the PC", 0x58);
	up(a);
	EXPECT("and its release", 0xD8);
	CHECK(hotkeys == 1, "hotkey called %d times", hotkeys);

	printf("Ctrl+Alt+F11 beside it (vbe_blocks, v4.2):\n");
	kbd_input_set_hotkey(HID_KEY_F11, hotkey2);
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 0);
	EXPECT("Ctrl and Alt down", 0x1D, 0x38);
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 1, HID_KEY_F11);
	EXPECT_NONE("F11 with them: not to the PC");
	CHECK(hotkeys2 == 1 && hotkeys == 1, "F11's hotkey called %d times, F12's %d",
	      hotkeys2, hotkeys);
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 0);
	EXPECT_NONE("F11 let go: its release not sent");
	report(a, KBD_MOD_LCTRL | KBD_MOD_LALT, 1, HID_KEY_F12);
	EXPECT_NONE("F12 with them: still not to the PC");
	CHECK(hotkeys2 == 1 && hotkeys == 2, "F11's hotkey called %d times, F12's %d",
	      hotkeys2, hotkeys);
	up(a);
	EXPECT("all let go", 0x9D, 0xB8);
	press(a, 0, HID_KEY_F11);
	EXPECT("F11 alone goes to the PC", 0x57);
	up(a);
	EXPECT("and its release", 0xD7);
	kbd_input_set_hotkey(HID_KEY_F11, NULL);
	press(a, KBD_MOD_LCTRL | KBD_MOD_LALT, HID_KEY_F11);
	EXPECT("F11's hotkey taken away: Ctrl+Alt+F11 goes to the PC", 0x1D, 0x38, 0x57);
	up(a);
	EXPECT("and its release", 0xD7, 0x9D, 0xB8);
	CHECK(hotkeys2 == 1, "F11's hotkey called %d times", hotkeys2);
	kbd_input_set_hotkey(HID_KEY_F11, hotkey2);
	press(a, KBD_MOD_LCTRL | KBD_MOD_LALT, HID_KEY_F11);
	up(a);
	CHECK(hotkeys2 == 2, "F11's hotkey set again: called %d times", hotkeys2);
	EXPECT("only Ctrl and Alt reach the PC", 0x1D, 0x38, 0x9D, 0xB8);

	/* v5.1: the Korvo-1 has F9 to F12 and the profiler's F8: five at once */
	printf("Ctrl+Alt+F8 beside F9 to F12 (the profiler, v5.1):\n");
	kbd_input_set_hotkey(HID_KEY_F10, hotkey2);
	kbd_input_set_hotkey(HID_KEY_F9, hotkey2);
	kbd_input_set_hotkey(HID_KEY_F8, hotkey3);
	press(a, KBD_MOD_LCTRL | KBD_MOD_LALT, HID_KEY_F8);
	up(a);
	CHECK(hotkeys3 == 1, "F8's hotkey called %d times", hotkeys3);
	EXPECT("only Ctrl and Alt reach the PC", 0x1D, 0x38, 0x9D, 0xB8);
	press(a, KBD_MOD_LCTRL | KBD_MOD_LALT, HID_KEY_F12);
	up(a);
	CHECK(hotkeys == 3 && hotkeys3 == 1, "F12 still its own with five keys: %d, F8 %d",
	      hotkeys, hotkeys3);
	EXPECT("only Ctrl and Alt reach the PC", 0x1D, 0x38, 0x9D, 0xB8);
	kbd_input_set_hotkey(HID_KEY_F10, NULL);
	kbd_input_set_hotkey(HID_KEY_F9, NULL);
	kbd_input_set_hotkey(HID_KEY_F8, NULL);
	press(a, KBD_MOD_LCTRL | KBD_MOD_LALT, HID_KEY_F8);
	EXPECT("F8's hotkey taken away: Ctrl+Alt+F8 goes to the PC", 0x1D, 0x38, 0x42);
	up(a);
	EXPECT("and its release", 0xC2, 0x9D, 0xB8);
	CHECK(hotkeys3 == 1, "F8's hotkey called %d times", hotkeys3);

	printf("the boot menu's sink, and no PC yet:\n");
	kbd_input_set_sink(sink);
	press(a, KBD_MOD_LSHIFT, 0x1F);
	EXPECT_NONE("Shift+2 while the menu has the keys");
	CHECK(sunk == 2 && sunk_last == 0x1F && sunk_mods == KBD_MOD_LSHIFT,
	      "sink: %d calls, last %02X mods %02X", sunk, sunk_last, sunk_mods);
	kbd_input_set_sink(NULL);
	up(a);
	EXPECT_NONE("let go after the menu: nothing reaches the PC");
	kbd_input_set_target(NULL, NULL);
	press(a, 0, 0x04);
	CHECK(kbd_input_poll() == -1, "no PC: nothing repeats");
	kbd_input_set_target(kbd, mouse);
	up(a);
	EXPECT_NONE("A pressed with no PC, let go with one: nothing");
	press(a, 0, 0x04);
	EXPECT("A again", 0x1E);
	up(a);
	EXPECT("let go", 0x9E);
	kbd_input_close(a);
}

/* ==== 4. the mouse ==== */

static int mouse_read(uint8_t *out, int max)
{
	int n = 0;
	/* the mouse queue: ps2_read_data on the mouse, as much as is there
	   (i8042.c keeps the count to itself: see below).  The packets are
	   made in the controller's step, at most one a sample (10 ms at the
	   default 100 a second), so the clock goes on by 20 ms a round, 400 ms
	   in all. */
	extern int ps2_mouse_pending_for_test(void);
	for (int round = 0; round < 20; round++) {
		now_us += 20000;
		kbd_step(ctrl);
		while (n < max && ps2_mouse_pending_for_test() > 0)
			out[n++] = ps2_read_data(mouse);
	}
	return n;
}

static void test_mouse(void)
{
	uint8_t b[128];
	int n;
	printf("the mouse:\n");
	kbd_input_mouse(5, 3, 0, 1);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 0, "before the guest enables the mouse: %s", hex(b, n));
	ps2_write_mouse(mouse, 0xF4);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 1 && b[0] == 0xFA, "enable: %s", hex(b, n));
	kbd_input_mouse(5, 3, 0, 1);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 3 && !memcmp(b, "\x29\x05\xFD", 3),
	      "right 5, down 3, left button: %s, want 29 05 FD", hex(b, n));
	printf("  right 5, down 3, left button:  %s (PS/2 Y is up-positive)\n", hex(b, n));
	kbd_input_mouse(0, 0, 0, 0);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 3 && !memcmp(b, "\x08\x00\x00", 3), "button up: %s", hex(b, n));
	/* IMPS/2: sample rates 200, 100, 80 */
	static const uint8_t imps[] = { 0xF3, 200, 0xF3, 100, 0xF3, 80 };
	for (unsigned i = 0; i < sizeof(imps); i++)
		ps2_write_mouse(mouse, imps[i]);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 6, "IMPS/2 knock: %s", hex(b, n));
	ps2_write_mouse(mouse, 0xF2);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 2 && b[1] == 3, "mouse ID after the knock: %s, want FA 03", hex(b, n));
	kbd_input_mouse(0, 0, 1, 0);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 4 && !memcmp(b, "\x08\x00\x00\xFF", 4),
	      "wheel turned away from the user: %s, want 08 00 00 FF", hex(b, n));
	printf("  IMPS/2, wheel away from user:  %s (PS/2 Z is negative that way)\n",
	       hex(b, n));
	kbd_input_mouse(1000, 0, 0, 0);
	n = mouse_read(b, sizeof(b));
	int sum = 0;
	for (int i = 0; i + 3 < n; i += 4)
		sum += (int8_t) b[i + 1];
	CHECK(n % 4 == 0 && sum == 512, "1000 right: %d bytes, sum %d, want 512", n, sum);
	printf("  1000 to the right, clamped:    %d packets, %d in all\n", n / 4, sum);
	kbd_input_mouse(-40, -40, 0, 0x02);
	n = mouse_read(b, sizeof(b));
	CHECK(n == 4 && b[0] == (0x08 | 0x10 | 0x02) && (int8_t) b[1] == -40 &&
	      (int8_t) b[2] == 40, "left and up 40, right button: %s", hex(b, n));
}

/* i8042.c keeps its queue to itself; this test reaches it through the
   struct layout the file has (PS2State first in PS2MouseState) */
struct test_ps2_queue {
	uint8_t data[256];
	int rptr, wptr, count;
};
int ps2_mouse_pending_for_test(void)
{
	return ((struct test_ps2_queue *) mouse)->count;
}

int main(void)
{
	ctrl = i8042_init(&kbd, &mouse, 1, 12, NULL, set_irq, NULL, reset_request);
	kbd_input_init();
	kbd_input_set_target(kbd, mouse);
	int dev = kbd_input_open();

	test_every_key(dev);
	test_sequences(dev);
	test_typematic(dev);
	kbd_input_close(dev);
	test_keyboards();
	test_mouse();

	printf("kbd_input: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
