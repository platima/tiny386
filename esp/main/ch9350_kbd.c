/*
 * A USB boot keyboard report into tiny386's PS/2 keyboard (see ch9350_kbd.h).
 *
 * A report is a snapshot of the keys held, so it is compared with the last
 * one and only the differences become make and break codes.  That also
 * makes the CH9350L's repeated frames (it sends each one 3 or 4 times,
 * ch9350.md 6.6) harmless.  Reports whose key slots all read 0x01
 * (ErrorRollOver, too many keys) are ignored rather than taken as "nothing
 * held", which would let go of every key.
 */
#include <string.h>
#include "ch9350_kbd.h"
#include "../../i8042.h"

int hid_usage_to_linux_keycode(int hid);   /* usb_hid.c */

static void key(void *ps2kbd, int down, int hid)
{
	int code = hid_usage_to_linux_keycode(hid);

	if (ps2kbd && code > 0)
		ps2_put_keycode(ps2kbd, down, code);
}

static int has(const uint8_t *keys, uint8_t k)
{
	for (int i = 0; i < 6; i++)
		if (keys[i] == k)
			return 1;
	return 0;
}

static int rollover(const uint8_t *keys)
{
	for (int i = 0; i < 6; i++)
		if (keys[i] != 0x01)
			return 0;
	return 1;
}

void bootkbd_report(bootkbd_t *k, void *ps2kbd, const uint8_t r[8])
{
	const uint8_t *now = r + 2, *was = k->prev + 2;
	uint8_t mod_down = r[0] & ~k->prev[0];
	uint8_t mod_up = k->prev[0] & ~r[0];

	if (rollover(now)) {
		k->rollover_ignored++;
		return;
	}
	/* modifiers pressed first, so that a shifted key comes out shifted */
	for (int b = 0; b < 8; b++)
		if (mod_down & (1 << b))
			key(ps2kbd, 1, 0xE0 + b);
	/* 0x00 to 0x03 are "no key" and the error codes */
	for (int i = 0; i < 6; i++)
		if (was[i] > 0x03 && !has(now, was[i]))
			key(ps2kbd, 0, was[i]);
	for (int i = 0; i < 6; i++)
		if (now[i] > 0x03 && !has(was, now[i]))
			key(ps2kbd, 1, now[i]);
	for (int b = 0; b < 8; b++)
		if (mod_up & (1 << b))
			key(ps2kbd, 0, 0xE0 + b);
	memcpy(k->prev, r, 8);
}

void bootkbd_release_all(bootkbd_t *k, void *ps2kbd)
{
	static const uint8_t none[8];

	bootkbd_report(k, ps2kbd, none);
}
