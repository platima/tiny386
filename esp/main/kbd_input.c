/*
 * Keyboard and mouse into the emulated PC: see kbd_input.h.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "kbd_input.h"
#include "../../i8042.h"

uint32_t get_uticks();

#ifdef KBD_INPUT_HOST
#define LOCK() do { } while (0)
#define UNLOCK() do { } while (0)
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
/* the state below: keyboards report from the USB task, repeats come from
   their own task, the lights from the emulator's core */
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buf;
#define LOCK() xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)
#endif

static PS2KbdState *s_kbd;
static PS2MouseState *s_mouse;
static kbd_input_key_fn *s_sink;
static uint8_t s_hotkey;
static void (*s_hotkey_fn)(void);
static kbd_input_leds_fn *s_leds_fn;
static void (*s_wake)(void);
static uint8_t s_leds;          /* USB order */

static struct {
	bool used;
	uint8_t last[8];
} s_dev[KBD_INPUT_DEVS];

static uint8_t s_down[256];     /* on how many keyboards each usage is down */
static uint8_t s_swallowed[32]; /* pressed without reaching the PC */

static uint8_t s_rep_usage;     /* the key repeating, 0 for none */
static uint32_t s_rep_due;
static uint32_t s_rep_delay = 500000, s_rep_period = 91674;

/* ==== usages to scancode set 1 ==== */

#define E0(x) (0xE000 | (x))

static const uint16_t set1_of[0x66] = {
	/* 0x04 to 0x1D: A to Z */
	[0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
	[0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
	[0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
	[0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
	[0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14,
	[0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
	[0x1C] = 0x15, [0x1D] = 0x2C,
	/* 1 to 9, 0 */
	[0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
	[0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
	[0x26] = 0x0A, [0x27] = 0x0B,
	/* Enter, Esc, Backspace, Tab, Space, - = [ ] \ (and the ISO # key,
	   which is the same key position) ; ' ` , . / Caps Lock */
	[0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
	[0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A,
	[0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
	[0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34,
	[0x38] = 0x35, [0x39] = 0x3A,
	/* F1 to F12 */
	[0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E,
	[0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
	[0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,
	/* Print Screen (0x46) and Pause (0x48) are sequences; Scroll Lock */
	[0x47] = 0x46,
	/* Insert, Home, Page Up, Delete, End, Page Down, the arrows */
	[0x49] = E0(0x52), [0x4A] = E0(0x47), [0x4B] = E0(0x49),
	[0x4C] = E0(0x53), [0x4D] = E0(0x4F), [0x4E] = E0(0x51),
	[0x4F] = E0(0x4D), [0x50] = E0(0x4B), [0x51] = E0(0x50),
	[0x52] = E0(0x48),
	/* Num Lock, the keypad */
	[0x53] = 0x45, [0x54] = E0(0x35), [0x55] = 0x37, [0x56] = 0x4A,
	[0x57] = 0x4E, [0x58] = E0(0x1C), [0x59] = 0x4F, [0x5A] = 0x50,
	[0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C, [0x5E] = 0x4D,
	[0x5F] = 0x47, [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52,
	[0x63] = 0x53,
	/* the ISO \ key beside the left Shift, the Menu (Application) key */
	[0x64] = 0x56, [0x65] = E0(0x5D),
};

static const uint16_t set1_mod[8] = {
	0x1D, 0x2A, 0x38, E0(0x5B),             /* left Ctrl, Shift, Alt, GUI */
	E0(0x1D), 0x36, E0(0x38), E0(0x5C),     /* right ones */
};

int kbd_input_set1(uint8_t usage)
{
	if (usage < sizeof(set1_of) / sizeof(set1_of[0]))
		return set1_of[usage];
	if (usage >= 0xE0 && usage <= 0xE7)
		return set1_mod[usage - 0xE0];
	return 0;
}

void kbd_input_typematic(int rate, uint32_t *delay_us, uint32_t *period_us)
{
	/* bits 5 and 6: (1 + D) x 250 ms; bits 0 to 4: (8 + A) x 2^B x 4.17
	   ms, A bits 0 to 2, B bits 3 and 4 */
	*delay_us = (((rate >> 5) & 3) + 1) * 250000u;
	*period_us = (8 + (rate & 7)) * (1u << ((rate >> 3) & 3)) * 4167u;
}

/* ==== keys ==== */

static bool is_modifier(uint8_t u)
{
	return u >= 0xE0 && u <= 0xE7;
}

/* What a PS/2 keyboard does not repeat here: the modifiers and the lock
   keys (a real one would; nothing gains by it), Print Screen and Pause. */
static bool repeats(uint8_t u)
{
	return !is_modifier(u) && u != 0x39 && u != 0x47 && u != 0x53 &&
		u != 0x46 && u != 0x48 && kbd_input_set1(u) != 0;
}

static uint8_t mods_down(void)
{
	uint8_t m = 0;
	for (int b = 0; b < 8; b++)
		if (s_down[0xE0 + b])
			m |= 1 << b;
	return m;
}

static void put(int down, int code)
{
	ps2_put_keycode(s_kbd, down, code);
}

/* the key's make or break code(s), to the PC */
static void send(uint8_t u, bool down, uint8_t mods)
{
	if (u == 0x46) {
		/* Print Screen: SysRq with Alt, a plain E0 37 with Ctrl or
		   Shift, else E0 2A E0 37 and the reverse */
		if (mods & KBD_MOD_ALT) {
			put(down, 0x54);
		} else if (mods & (KBD_MOD_CTRL | KBD_MOD_SHIFT)) {
			put(down, E0(0x37));
		} else if (down) {
			put(1, E0(0x2A));
			put(1, E0(0x37));
		} else {
			put(0, E0(0x37));
			put(0, E0(0x2A));
		}
		return;
	}
	if (u == 0x48) {
		/* Pause: E1 1D 45 E1 9D C5 when pressed and nothing when let
		   go; with Ctrl it is Break, E0 46 E0 C6 */
		if (!down)
			return;
		if (mods & KBD_MOD_CTRL) {
			put(1, E0(0x46));
			put(0, E0(0x46));
		} else {
			put(1, 0xE11D);
			put(1, 0x45);
			put(0, 0xE11D);
			put(0, 0x45);
		}
		return;
	}
	int code = kbd_input_set1(u);
	if (code)
		put(down, code);
}

static bool swallowed(uint8_t u)
{
	return s_swallowed[u >> 3] & (1 << (u & 7));
}

static void set_swallowed(uint8_t u, bool on)
{
	if (on)
		s_swallowed[u >> 3] |= 1 << (u & 7);
	else
		s_swallowed[u >> 3] &= ~(1 << (u & 7));
}

static void key_event(uint8_t u, bool down)
{
	if (down) {
		if (s_down[u]++)
			return;         /* already down on another keyboard */
		uint8_t mods = mods_down();
		/* a real keyboard repeats the last key pressed, and only it */
		s_rep_usage = 0;
		if (s_hotkey_fn && u == s_hotkey && (mods & KBD_MOD_CTRL) &&
		    (mods & KBD_MOD_ALT)) {
			set_swallowed(u, true);
			s_hotkey_fn();
			return;
		}
		if (s_sink) {
			set_swallowed(u, true);
			s_sink(u, mods);
			return;
		}
		if (!s_kbd) {
			set_swallowed(u, true);
			return;
		}
		send(u, true, mods);
		if (repeats(u)) {
			s_rep_usage = u;
			s_rep_due = get_uticks() + s_rep_delay;
			if (s_wake)
				s_wake();
		}
	} else {
		if (!s_down[u] || --s_down[u])
			return;         /* not down, or still down elsewhere */
		if (s_rep_usage == u)
			s_rep_usage = 0;
		if (swallowed(u)) {
			set_swallowed(u, false);
			return;
		}
		if (s_kbd)
			send(u, false, mods_down());
	}
}

static bool has_key(const uint8_t *r, uint8_t u)
{
	for (int i = 2; i < 8; i++)
		if (r[i] == u)
			return true;
	return false;
}

/* the same usage earlier in the report: one event for it, not two */
static bool dup_before(const uint8_t *r, int i)
{
	for (int j = 2; j < i; j++)
		if (r[j] == r[i])
			return true;
	return false;
}

/* usages 0 (none) to 3 (errors) are not keys */
#define IS_KEY(u) ((u) > 3)

static void apply(int dev, const uint8_t *r)
{
	uint8_t *old = s_dev[dev].last;

	/* releases first, keys then modifiers; then presses, modifiers then
	   keys: Shift+A pressed together is Shift then A, and A let go as B
	   is pressed with Shift let go is a b, not a B */
	for (int i = 2; i < 8; i++)
		if (IS_KEY(old[i]) && !has_key(r, old[i]) && !dup_before(old, i))
			key_event(old[i], false);
	for (int b = 0; b < 8; b++)
		if ((old[0] & (1 << b)) && !(r[0] & (1 << b)))
			key_event(0xE0 + b, false);
	for (int b = 0; b < 8; b++)
		if (!(old[0] & (1 << b)) && (r[0] & (1 << b)))
			key_event(0xE0 + b, true);
	for (int i = 2; i < 8; i++)
		if (IS_KEY(r[i]) && !has_key(old, r[i]) && !dup_before(r, i))
			key_event(r[i], true);
	memcpy(old, r, 8);
}

/* ==== the interface ==== */

void kbd_input_init(void)
{
#ifndef KBD_INPUT_HOST
	if (!s_mutex)
		s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
#endif
	kbd_input_typematic(KBD_TYPEMATIC_DEFAULT, &s_rep_delay, &s_rep_period);
}

void kbd_input_set_target(PS2KbdState *kbd, PS2MouseState *mouse)
{
	LOCK();
	s_kbd = kbd;
	s_mouse = mouse;
	s_rep_usage = 0;
	UNLOCK();
}

void kbd_input_set_sink(kbd_input_key_fn *fn)
{
	LOCK();
	s_sink = fn;
	s_rep_usage = 0;
	UNLOCK();
}

void kbd_input_set_hotkey(uint8_t usage, void (*fn)(void))
{
	LOCK();
	s_hotkey = usage;
	s_hotkey_fn = fn;
	UNLOCK();
}

void kbd_input_set_leds_fn(kbd_input_leds_fn *fn)
{
	LOCK();
	s_leds_fn = fn;
	UNLOCK();
}

uint8_t kbd_input_leds(void)
{
	return s_leds;
}

void kbd_input_set_wake(void (*fn)(void))
{
	LOCK();
	s_wake = fn;
	UNLOCK();
}

int kbd_input_open(void)
{
	int dev = -1;
	LOCK();
	for (int i = 0; i < KBD_INPUT_DEVS; i++) {
		if (!s_dev[i].used) {
			s_dev[i].used = true;
			memset(s_dev[i].last, 0, sizeof(s_dev[i].last));
			dev = i;
			break;
		}
	}
	UNLOCK();
	return dev;
}

static const uint8_t no_keys[8];

void kbd_input_close(int dev)
{
	if (dev < 0 || dev >= KBD_INPUT_DEVS)
		return;
	LOCK();
	if (s_dev[dev].used) {
		apply(dev, no_keys);
		s_dev[dev].used = false;
	}
	UNLOCK();
}

void kbd_input_report(int dev, const uint8_t report[8])
{
	if (dev < 0 || dev >= KBD_INPUT_DEVS)
		return;
	/* ErrorRollOver: too many keys down to say which; keep the state */
	for (int i = 2; i < 8; i++)
		if (report[i] == 0x01)
			return;
	LOCK();
	if (s_dev[dev].used)
		apply(dev, report);
	UNLOCK();
}

void kbd_input_release_all(void)
{
	LOCK();
	for (int i = 0; i < KBD_INPUT_DEVS; i++)
		if (s_dev[i].used)
			apply(i, no_keys);
	UNLOCK();
}

int32_t kbd_input_poll(void)
{
	int32_t wait = -1;
	LOCK();
	if (s_rep_usage && s_kbd && !s_sink) {
		uint32_t now = get_uticks();
		int32_t d = (int32_t) (s_rep_due - now);
		if (d <= 0) {
			if (ps2_kbd_pending(s_kbd) < KBD_REPEAT_MAX_PENDING)
				send(s_rep_usage, true, mods_down());
			s_rep_due += s_rep_period;
			/* far behind (the poller was held up): no burst */
			if ((int32_t) (s_rep_due - now) <= 0)
				s_rep_due = now + s_rep_period;
			d = (int32_t) (s_rep_due - now);
		}
		wait = d;
	}
	UNLOCK();
	return wait;
}

#define MOUSE_CLAMP 512

void kbd_input_mouse(int dx, int dy, int wheel, int buttons)
{
	/* a big movement is real (a fast flick): kept, but no more than a few
	   PS/2 packets' worth, so that one report cannot fill the queue */
	if (dx > MOUSE_CLAMP)
		dx = MOUSE_CLAMP;
	else if (dx < -MOUSE_CLAMP)
		dx = -MOUSE_CLAMP;
	if (dy > MOUSE_CLAMP)
		dy = MOUSE_CLAMP;
	else if (dy < -MOUSE_CLAMP)
		dy = -MOUSE_CLAMP;
	LOCK();
	/* ps2_mouse_event() takes dy down-positive, as here, and the wheel
	   as a PS/2 mouse counts it: positive towards the user */
	if (s_mouse && !s_sink)
		ps2_mouse_event(s_mouse, dx, dy, -wheel, buttons & 0x1f);
	UNLOCK();
}

/* ==== the repeat task ==== */

#ifndef KBD_INPUT_HOST
#include "freertos/task.h"
#include "console.h"

static TaskHandle_t s_repeat_task;

static void repeat_task(void *arg)
{
	(void) arg;
	for (;;) {
		int32_t wait = kbd_input_poll();
		TickType_t ticks = portMAX_DELAY;
		if (wait >= 0) {
			/* to the tick (10 ms at 100 Hz): a repeat may come up to
			   a tick late, the next is timed from when it was due */
			uint32_t ms = (uint32_t) wait / 1000 + 1;
			ticks = (ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS;
			if (ticks < 1)
				ticks = 1;
		}
		ulTaskNotifyTake(pdTRUE, ticks);
	}
}

static void wake_repeat(void)
{
	if (s_repeat_task)
		xTaskNotifyGive(s_repeat_task);
}

void kbd_input_start(void)
{
	if (s_repeat_task)
		return;
	if (xTaskCreatePinnedToCore(repeat_task, "kbd_repeat", 2048, NULL, 4,
				    &s_repeat_task, 0) != pdPASS)
		console_fatal("The task kbd_repeat could not be started (no memory).");
	kbd_input_set_wake(wake_repeat);
}
#endif

/* ==== from i8042.c: what the guest told the keyboard ==== */

void ps2_kbd_leds_changed(int ps2_leds)
{
	/* PS/2: bit 0 Scroll, 1 Num, 2 Caps.  USB: bit 0 Num, 1 Caps, 2 Scroll */
	uint8_t usb = ((ps2_leds & 2) ? KBD_LED_NUM : 0) |
		((ps2_leds & 4) ? KBD_LED_CAPS : 0) |
		((ps2_leds & 1) ? KBD_LED_SCROLL : 0);
	LOCK();
	bool changed = usb != s_leds;
	s_leds = usb;
	if (changed && s_leds_fn)
		s_leds_fn(usb);
	UNLOCK();
}

void ps2_kbd_rate_changed(int rate)
{
	LOCK();
	kbd_input_typematic(rate, &s_rep_delay, &s_rep_period);
	UNLOCK();
}
