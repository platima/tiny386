/*
 * WCH CH9350L in lower mode: USB keyboard and mouse in, UART frames out.
 *
 * Written for tiny386's DOS board from the protocol in the CH9350L manual
 * V2.6 (DS26) pages 10 to 14, as collected in
 * tiny386-hdmi-board/research/ch9350.md section 6.
 *
 * The frame parser is plain C (no ESP-IDF) so that it runs on a PC
 * (tests/host/dosboard/ch9350_test.c).  The UART task, the reset and state pins and the
 * keyboard LED frames are at the end of ch9350.c, for the DOS board only.
 */
#ifndef CH9350_H
#define CH9350_H

#include <stdint.h>

typedef struct {
	void *ctx;
	/* 8-byte boot keyboard report: modifiers, reserved, six key codes */
	void (*kbd)(void *ctx, const uint8_t report[8]);
	/* relative mouse as HID reports it: dy positive = down, wheel positive = up */
	void (*mouse)(void *ctx, int buttons, int dx, int dy, int wheel);
	/* 0x86: a device was attached or removed; release everything */
	void (*release_all)(void *ctx);
	/* other frames: 0x80, 0x82, 0x85 (arg = their byte), 0x84, 0x87, 0x89 (arg 0),
	   0x81 (arg = port) */
	void (*status)(void *ctx, uint8_t op, uint8_t arg);
} ch9350_sink_t;

#define CH9350_MAX_BODY 80       /* state 0 and 1 data frames are under 72 bytes (DS26 p. 12) */

typedef struct {
	ch9350_sink_t sink;
	int mode;                    /* working state of the chip: 2, or 0 for the fallback */
	uint8_t st;
	uint8_t op;
	uint8_t hunting;
	uint8_t sum;
	uint16_t need;
	uint16_t have;
	uint8_t body[CH9350_MAX_BODY];
	uint32_t last_us;
	uint32_t timeout_us;         /* a frame that stops halfway is dropped after this */
	uint32_t last_mouse_us;
	uint32_t zero_guard_us;      /* 0 = off; see ch9350_parser_init() */
	/* counts, for the log */
	uint32_t frames;
	uint32_t kbd_frames;
	uint32_t mouse_frames;
	uint32_t resyncs;            /* times the parser had to hunt for 57 AB */
	uint32_t junk_bytes;         /* bytes thrown away while hunting */
	uint32_t unknown_ops;
	uint32_t timeouts;
	uint32_t bad_frames;         /* wrong length byte, reserved byte not zero, short data */
	uint32_t bad_sums;           /* state 0 and 1 frames, and 0x81 */
	uint32_t zero_ignored;       /* all-zero keyboard frames dropped by the guard */
} ch9350_parser_t;

/*
 * mode: 2 or 0.  timeout_us: 0 = never drop a half frame on time alone.
 * zero_guard_us: 0 = off; otherwise an all-zero keyboard frame that comes
 * within this time of a mouse frame is ignored (ch9350.md 6.6: such frames
 * were seen during mouse motion in state 2; turn this on only if the bench
 * shows held keys being let go while the mouse moves).
 */
void ch9350_parser_init(ch9350_parser_t *p, int mode, const ch9350_sink_t *sink,
			uint32_t timeout_us, uint32_t zero_guard_us);
void ch9350_parser_feed(ch9350_parser_t *p, const uint8_t *data, int n, uint32_t now_us);

/* The answer status frame, which also carries the keyboard LEDs (DS26 7.2.4.12):
   57 AB 12 00 00 00 00 LED x AC 20, x = 07 in state 2, 00 in state 0.
   usb_leds: bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock. */
#define CH9350_LED_FRAME_LEN 11
void ch9350_led_frame(uint8_t out[CH9350_LED_FRAME_LEN], int usb_leds, int mode);

/* PS/2 "set LEDs" byte (bit 0 Scroll, bit 1 Num, bit 2 Caps) to the USB order. */
int ch9350_ps2_leds_to_usb(int ps2_leds);

/* Sink callbacks into kbd_input.c, the keyboard and mouse layer the USB
   host uses too: a keyboard report to kbd_input_report() (typematic repeat,
   the lights, the boot menu and the Ctrl+Alt+F12 key come from there), the
   mouse to kbd_input_mouse(), frame 0x86 to kbd_input_release_all(). */
typedef struct {
	int dev;                     /* kbd_input_open()'s slot for the chip's keyboards */
	uint8_t buttons;             /* mouse buttons held */
} ch9350_input_t;
void ch9350_input_sink(ch9350_sink_t *sink, ch9350_input_t *t);

/* DOS board only (ch9350.c, ESP-IDF part): resets the chip into state
   mode (2, or 0 for the fallback) and starts its UART task, once.  Its
   keyboards and mice reach kbd_input.c from then on: the boot menu before
   the PC exists, the PC once kbd_input_set_target() names it. */
void ch9350_setup(int mode, int zero_guard_ms);

#endif /* CH9350_H */
