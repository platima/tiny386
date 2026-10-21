/*
 * Just enough of a HID report descriptor parser to read a keyboard or a
 * mouse that does not offer the boot protocol: a wireless receiver whose
 * mouse is not a boot interface, or keyboard and mouse sharing one
 * interface under report IDs.
 *
 * From the descriptor it finds, for the first keyboard and the first mouse
 * in it: the report ID, the modifier bits, the key array (or a bitmap of
 * keys, as N-key rollover keyboards send), the buttons, X, Y and the wheel
 * (relative ones only; a tablet's absolute X and Y are left alone).  Then
 * each input report is turned into an 8-byte boot keyboard report, or a
 * mouse movement.
 *
 * Plain C, no ESP-IDF: tests/host/test_hid_parse.c builds it on a PC.
 */
#ifndef HID_PARSE_H
#define HID_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hid_field {
	uint16_t bit;           /* from the first byte after the report ID */
	uint8_t size;           /* bits per item; 0: not there */
	uint8_t count;
	bool sign;
};

struct hid_layout {
	bool uses_ids;          /* every report starts with its ID */
	bool kbd, mouse;        /* what was found */
	uint8_t kbd_id, mouse_id;
	struct hid_field mods;          /* 8 x 1 bit, Left Ctrl first */
	struct hid_field keys;          /* array of 8-bit usages */
	struct hid_field bitmap;        /* 1 bit a usage ... */
	uint8_t bitmap_first;           /* ... from this usage */
	struct hid_field buttons;       /* 1 bit a button, button 1 first */
	struct hid_field x, y, wheel;
};

/* true if the descriptor has a keyboard or a mouse this can read */
bool hid_parse(const uint8_t *desc, size_t len, struct hid_layout *out);

/* A report of the keyboard as a boot report (modifiers, 0, six usages):
   false if this report is not the keyboard's. */
bool hid_report_keyboard(const struct hid_layout *l, const uint8_t *r, size_t n,
			 uint8_t boot[8]);

/* A report of the mouse: false if it is not the mouse's. */
bool hid_report_mouse(const struct hid_layout *l, const uint8_t *r, size_t n,
		      int *dx, int *dy, int *wheel, int *buttons);

/* For the console: "keyboard", "mouse", "keyboard and mouse" */
const char *hid_layout_what(const struct hid_layout *l);

#endif /* HID_PARSE_H */
