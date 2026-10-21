/*
 * Host test of esp/main/hid_parse.c (FIRMWARE-PLAN 3.3): report
 * descriptors of the kinds a keyboard, a mouse and a wireless receiver
 * give, and reports read with what was found.
 *
 * The descriptors are written here from the HID 1.11 specification's item
 * format: the boot keyboard and mouse of its appendix B (the mouse with a
 * wheel added, as most have), a receiver that puts keyboard, mouse and
 * media keys on one interface under report IDs with 12-bit X and Y, an
 * N-key rollover bitmap keyboard, a vendor interface, and a tablet with
 * absolute X and Y.  They are typical layouts, not dumps of real devices.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hid_parse.h"

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

static const uint8_t boot_keyboard[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,             /* Desktop, Keyboard, App */
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             /* 8 modifier bits */
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             /* reserved byte */
	0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
	0x91, 0x02,                                     /* 5 LEDs (output) */
	0x95, 0x01, 0x75, 0x03, 0x91, 0x01,             /* padding (output) */
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
	0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, /* 6 key bytes */
	0xC0,
};

static const uint8_t wheel_mouse[] = {
	0xFE, 0x02, 0x00, 0xAA, 0xBB,                   /* a long item, skipped */
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x95, 0x03, 0x75, 0x01, 0x81, 0x02,             /* 3 buttons */
	0x95, 0x01, 0x75, 0x05, 0x81, 0x01,             /* padding */
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
	0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06,                                     /* X, Y, wheel: relative */
	0xC0, 0xC0,
};

static const uint8_t receiver[] = {
	/* keyboard, report 1 */
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00,
	0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
	0xC0,
	/* mouse, report 2: 16 buttons, 12-bit X and Y, wheel, AC pan */
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
	0x95, 0x10, 0x75, 0x01, 0x81, 0x02,
	0x05, 0x01, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02,
	0x09, 0x30, 0x09, 0x31, 0x81, 0x06,
	0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06,
	0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06,
	0xC0, 0xC0,
	/* media keys, report 3 */
	0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03,
	0x75, 0x10, 0x95, 0x02, 0x15, 0x01, 0x26, 0xFF, 0x02,
	0x19, 0x01, 0x2A, 0xFF, 0x02, 0x81, 0x00,
	0xC0,
};

static const uint8_t nkro[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x04,
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             /* modifiers */
	0x19, 0x00, 0x29, 0x77, 0x95, 0x78, 0x81, 0x02, /* usages 0 to 0x77, a bit each */
	0xC0,
};

static const uint8_t vendor[] = {
	0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x10,
	0x75, 0x08, 0x95, 0x06, 0x15, 0x00, 0x26, 0xFF, 0x00,
	0x09, 0x01, 0x81, 0x00, 0x09, 0x01, 0x91, 0x00,
	0xC0,
};

static const uint8_t tablet[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x7F,
	0x75, 0x10, 0x95, 0x02, 0x81, 0x02,             /* absolute */
	0xC0, 0xC0,
};

static void show(const char *name, const struct hid_layout *l)
{
	printf("  %-14s %s%s", name, hid_layout_what(l), l->uses_ids ? ", report IDs" : "");
	if (l->kbd)
		printf("; keyboard id %d: mods bit %d%s, keys %dx%d at bit %d, bitmap %d "
		       "from %02X at bit %d", l->kbd_id, l->mods.bit, l->mods.size ? "" : " (none)",
		       l->keys.count, l->keys.size, l->keys.bit, l->bitmap.count,
		       l->bitmap_first, l->bitmap.bit);
	if (l->mouse)
		printf("; mouse id %d: %d buttons at bit %d, X %d bits at %d, Y %d at %d, "
		       "wheel %d at %d", l->mouse_id, l->buttons.count, l->buttons.bit,
		       l->x.size, l->x.bit, l->y.size, l->y.bit, l->wheel.size, l->wheel.bit);
	printf("\n");
}

static bool same8(const uint8_t *a, const uint8_t *b)
{
	return !memcmp(a, b, 8);
}

int main(void)
{
	struct hid_layout l;
	uint8_t boot[8];
	int dx, dy, wheel, buttons;

	printf("descriptors:\n");

	/* boot keyboard */
	CHECK(hid_parse(boot_keyboard, sizeof(boot_keyboard), &l), "boot keyboard: nothing found");
	show("boot keyboard", &l);
	CHECK(l.kbd && !l.mouse && !l.uses_ids && l.mods.size == 1 && l.mods.bit == 0 &&
	      l.keys.bit == 16 && l.keys.count == 6 && l.keys.size == 8,
	      "boot keyboard layout");
	{
		static const uint8_t r[8] = { 0x02, 0, 0x04, 0x05, 0, 0, 0, 0 };
		CHECK(hid_report_keyboard(&l, r, 8, boot) && same8(boot, r),
		      "boot keyboard report not passed through");
		CHECK(!hid_report_mouse(&l, r, 8, &dx, &dy, &wheel, &buttons),
		      "boot keyboard read as a mouse");
	}

	/* wheel mouse */
	CHECK(hid_parse(wheel_mouse, sizeof(wheel_mouse), &l), "wheel mouse: nothing found");
	show("wheel mouse", &l);
	CHECK(l.mouse && !l.kbd && l.buttons.bit == 0 && l.buttons.count == 3 &&
	      l.x.bit == 8 && l.x.size == 8 && l.x.sign && l.y.bit == 16 &&
	      l.wheel.bit == 24, "wheel mouse layout");
	{
		static const uint8_t r[4] = { 0x01, 0xFB, 0x07, 0xFF };
		CHECK(hid_report_mouse(&l, r, 4, &dx, &dy, &wheel, &buttons) &&
		      buttons == 1 && dx == -5 && dy == 7 && wheel == -1,
		      "wheel mouse report: buttons %d dx %d dy %d wheel %d", buttons, dx, dy, wheel);
		printf("    01 FB 07 FF -> buttons %d, dx %d, dy %d, wheel %d\n", buttons, dx,
		       dy, wheel);
	}

	/* receiver: keyboard and mouse on one interface */
	CHECK(hid_parse(receiver, sizeof(receiver), &l), "receiver: nothing found");
	show("receiver", &l);
	CHECK(l.kbd && l.mouse && l.uses_ids && l.kbd_id == 1 && l.mouse_id == 2 &&
	      l.keys.bit == 16 && l.buttons.count == 8 && l.x.bit == 16 && l.x.size == 12 &&
	      l.x.sign && l.y.bit == 28 && l.wheel.bit == 40, "receiver layout");
	{
		static const uint8_t k[9] = { 0x01, 0x02, 0, 0x04, 0, 0, 0, 0, 0 };
		static const uint8_t want[8] = { 0x02, 0, 0x04, 0, 0, 0, 0, 0 };
		CHECK(hid_report_keyboard(&l, k, 9, boot) && same8(boot, want),
		      "receiver keyboard report");
		CHECK(!hid_report_mouse(&l, k, 9, &dx, &dy, &wheel, &buttons),
		      "receiver keyboard report read as the mouse");
		/* X -3 (0xFFD) and Y +10 (0x00A) packed: FD AF 00 */
		static const uint8_t m[8] = { 0x02, 0x05, 0x00, 0xFD, 0xAF, 0x00, 0x01, 0x00 };
		CHECK(hid_report_mouse(&l, m, 8, &dx, &dy, &wheel, &buttons) &&
		      buttons == 5 && dx == -3 && dy == 10 && wheel == 1,
		      "receiver mouse report: buttons %d dx %d dy %d wheel %d", buttons, dx,
		      dy, wheel);
		printf("    02 05 00 FD AF 00 01 00 -> buttons %d, dx %d, dy %d, wheel %d\n",
		       buttons, dx, dy, wheel);
		CHECK(!hid_report_keyboard(&l, m, 8, boot), "mouse report read as the keyboard");
		static const uint8_t c[5] = { 0x03, 0xE9, 0x00, 0x00, 0x00 };
		CHECK(!hid_report_keyboard(&l, c, 5, boot) &&
		      !hid_report_mouse(&l, c, 5, &dx, &dy, &wheel, &buttons),
		      "media key report used");
		/* big movement: X +2047, Y -2047 */
		static const uint8_t big[8] = { 0x02, 0, 0, 0xFF, 0x17, 0x80, 0, 0 };
		CHECK(hid_report_mouse(&l, big, 8, &dx, &dy, &wheel, &buttons) &&
		      dx == 2047 && dy == -2047, "big movement: dx %d dy %d", dx, dy);
	}

	/* N-key rollover bitmap */
	CHECK(hid_parse(nkro, sizeof(nkro), &l), "nkro: nothing found");
	show("NKRO bitmap", &l);
	CHECK(l.kbd && l.kbd_id == 4 && l.mods.bit == 0 && l.bitmap.bit == 8 &&
	      l.bitmap.count == 120 && l.bitmap_first == 0 && !l.keys.size, "nkro layout");
	{
		uint8_t r[17] = { 0x04, 0x20 };
		r[2] = 0x10;                    /* usage 4, A */
		r[2 + 3] = 0x60;                /* usages 0x1D Z and 0x1E 1 */
		static const uint8_t want[8] = { 0x20, 0, 0x04, 0x1D, 0x1E, 0, 0, 0 };
		CHECK(hid_report_keyboard(&l, r, sizeof(r), boot) && same8(boot, want),
		      "nkro report: %02X %02X %02X %02X %02X", boot[0], boot[2], boot[3],
		      boot[4], boot[5]);
		printf("    Right Shift, A, Z, 1 -> %02X %02X %02X %02X %02X %02X %02X %02X\n",
		       boot[0], boot[1], boot[2], boot[3], boot[4], boot[5], boot[6], boot[7]);
		r[2] = 0xF0;                    /* usages 4 to 8 with Z and 1: 7 keys */
		r[3] = 0x01;
		static const uint8_t roll[8] = { 0x20, 0, 1, 1, 1, 1, 1, 1 };
		CHECK(hid_report_keyboard(&l, r, sizeof(r), boot) && same8(boot, roll),
		      "seven keys: not ErrorRollOver");
	}

	CHECK(!hid_parse(vendor, sizeof(vendor), &l), "vendor interface taken for something");
	show("vendor", &l);
	CHECK(!hid_parse(tablet, sizeof(tablet), &l), "tablet (absolute) taken for a mouse");
	show("tablet", &l);
	/* cut short anywhere: no crash, no reading past the end */
	for (size_t n = 0; n < sizeof(receiver); n++)
		hid_parse(receiver, n, &l);

	printf("hid_parse: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
