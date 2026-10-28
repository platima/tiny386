/*
 * The keyboard and mouse as the emulated PC sees them, whatever they come
 * through: USB HID boot reports (usb_input.c), or the CH9350L's frames on
 * the DOS board, which carry the same 8-byte report.
 *
 * A keyboard hands in a whole report each time (modifier byte, a reserved
 * byte, six key usages); this layer compares it with that keyboard's last
 * one and makes PS/2 scancode set 1 key presses and releases from the
 * difference, through i8042.c's ps2_put_keycode().  It also does what a PS/2
 * keyboard does by itself and a USB one does not: typematic repeat, at the
 * rate and delay the guest sets with command 0xF3, and the lights the guest
 * sets with 0xED (handed on to the board's code to send to the keyboard).
 *
 * Several keyboards at once (a wireless receiver and a wired keyboard, say)
 * each have their own last report; a key held on two of them is pressed
 * once and released when both have let go.  Closing a keyboard releases
 * everything it still holds, so nothing sticks when it is unplugged.
 *
 * Before the PC exists, key presses can go to a function instead (the boot
 * menu), and Ctrl+Alt with one chosen key calls a function and never
 * reaches the PC (the speed screen).
 *
 * Built on a PC for tests/host/test_kbd_input.c with KBD_INPUT_HOST
 * defined: no locks then.
 */
#ifndef KBD_INPUT_H
#define KBD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PS2KbdState PS2KbdState;
typedef struct PS2MouseState PS2MouseState;

#define KBD_INPUT_DEVS 4        /* keyboards at once */

/* the modifier byte of a boot report */
#define KBD_MOD_LCTRL   0x01
#define KBD_MOD_LSHIFT  0x02
#define KBD_MOD_LALT    0x04
#define KBD_MOD_LGUI    0x08
#define KBD_MOD_RCTRL   0x10
#define KBD_MOD_RSHIFT  0x20
#define KBD_MOD_RALT    0x40
#define KBD_MOD_RGUI    0x80
#define KBD_MOD_CTRL    (KBD_MOD_LCTRL | KBD_MOD_RCTRL)
#define KBD_MOD_SHIFT   (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)
#define KBD_MOD_ALT     (KBD_MOD_LALT | KBD_MOD_RALT)

/* keyboard lights, in the USB (and CH9350L) order */
#define KBD_LED_NUM     0x01
#define KBD_LED_CAPS    0x02
#define KBD_LED_SCROLL  0x04

/* a few HID usages (keyboard page) */
#define HID_KEY_ENTER   0x28
#define HID_KEY_ESC     0x29
#define HID_KEY_F8      0x41
#define HID_KEY_F9      0x42
#define HID_KEY_F10     0x43
#define HID_KEY_F11     0x44
#define HID_KEY_F12     0x45
#define HID_KEY_RIGHT   0x4F
#define HID_KEY_LEFT    0x50
#define HID_KEY_DOWN    0x51
#define HID_KEY_UP      0x52
#define HID_KEY_KP_ENTER 0x58

/* the typematic setting a PS/2 keyboard starts with: 500 ms, 10.9 a second */
#define KBD_TYPEMATIC_DEFAULT 0x2B
/* no repeat while this many bytes wait in the PS/2 queue: a slow guest is
   not buried in repeats it will still be typing seconds later */
#define KBD_REPEAT_MAX_PENDING 8

void kbd_input_init(void);

/* Where key presses and mouse movement go: the PC's PS/2 keyboard and
   mouse.  NULL, NULL until the PC is built; input is then dropped. */
void kbd_input_set_target(PS2KbdState *kbd, PS2MouseState *mouse);

/* While set, every key press goes to fn (with the modifiers held) and not
   to the PC; nor do the releases of those keys, later.  NULL ends it. */
typedef void kbd_input_key_fn(uint8_t usage, uint8_t mods);
void kbd_input_set_sink(kbd_input_key_fn *fn);

/* Ctrl+Alt+usage calls fn; that key's press and release never reach the
   PC (Ctrl and Alt do).  Up to KBD_INPUT_HOTKEYS keys at once: setting a
   key again changes its fn, and fn NULL frees its place.  (v5.1: 6, for
   the profiler's F8 beside F9 to F12.) */
#define KBD_INPUT_HOTKEYS 6
void kbd_input_set_hotkey(uint8_t usage, void (*fn)(void));

/* The lights the guest asked for, in the USB order: fn is called when they
   change (from the emulator's core: it should only note them).  The
   board's code sends them to its keyboards; kbd_input_leds() for a
   keyboard just plugged in. */
typedef void kbd_input_leds_fn(uint8_t usb_leds);
void kbd_input_set_leds_fn(kbd_input_leds_fn *fn);
uint8_t kbd_input_leds(void);

/* Called when a key starts to repeat, so that whatever calls
   kbd_input_poll() looks again sooner. */
void kbd_input_set_wake(void (*fn)(void));

/* A keyboard: a slot of its own, or -1 if all are taken. */
int kbd_input_open(void);
/* Releases every key it still holds, then frees the slot. */
void kbd_input_close(int dev);
/* A boot report: modifiers, reserved, six usages.  A report of
   ErrorRollOver (usage 1) is ignored, as the HID specification says. */
void kbd_input_report(int dev, const uint8_t report[8]);
/* Every key of every keyboard released (the CH9350L's frame 86). */
void kbd_input_release_all(void);

/* Sends a repeat if one is due.  Returns the microseconds until the next
   one, or -1 if no key is repeating. */
int32_t kbd_input_poll(void);

/* On the board: starts the task that calls kbd_input_poll() (core 0), once.
   Every keyboard source calls it: the USB host (usb_input.c) and the
   DOS board's CH9350L. */
void kbd_input_start(void);

/* Mouse movement in the USB sense: dy positive is down, wheel positive is
   away from the user.  buttons: bit 0 left, 1 right, 2 middle, 3 and 4 the
   side buttons. */
void kbd_input_mouse(int dx, int dy, int wheel, int buttons);

/* The scancode set 1 code of a usage: 0x01 to 0x58, 0xE0xx for the keys
   with an E0 prefix, 0 for none.  Print Screen and Pause, which are
   sequences, give 0 here (kbd_input.c makes them itself). */
int kbd_input_set1(uint8_t usage);

/* typematic byte (command 0xF3) to delay and period, microseconds */
void kbd_input_typematic(int rate, uint32_t *delay_us, uint32_t *period_us);

#endif /* KBD_INPUT_H */
