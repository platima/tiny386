/*
 * Host test of the boot menu's state machine (esp/main/bootmenu.c, built
 * with BOOTMENU_HOST): the countdown, the keys, the wrap, the digits.
 */
#include <stdio.h>

#include "bootmenu.h"
#include "kbd_input.h"

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

#define S 1000000u

int main(void)
{
	struct menu m;
	uint32_t t0 = 4292000000u;      /* the 32-bit clock wraps 2.97 s later */

	/* no key: the default after the countdown */
	menu_start(&m, 3, 1, t0, BOOT_MENU_S * S);
	CHECK(menu_seconds_left(&m, t0) == BOOT_MENU_S, "left at start: %d",
	      menu_seconds_left(&m, t0));
	CHECK(menu_seconds_left(&m, t0 + 1) == BOOT_MENU_S, "left just after start");
	CHECK(!menu_tick(&m, t0 + BOOT_MENU_S * S - 1), "finished early");
	CHECK(menu_seconds_left(&m, t0 + BOOT_MENU_S * S - 1) == 1, "last second");
	CHECK(menu_tick(&m, t0 + BOOT_MENU_S * S) && m.how == MENU_TIMEOUT && m.sel == 1,
	      "timeout: how %d sel %d", m.how, m.sel);
	printf("  no key: entry 2 (the default) after %d s, across the clock's wrap\n",
	       BOOT_MENU_S);

	/* moving stops the countdown; the selection wraps */
	menu_start(&m, 3, 1, t0, BOOT_MENU_S * S);
	CHECK(!menu_key(&m, HID_KEY_DOWN, 0) && m.sel == 2, "down: %d", m.sel);
	CHECK(menu_seconds_left(&m, t0) == -1, "countdown not stopped by a key");
	CHECK(!menu_tick(&m, t0 + 100 * S), "finished by time after a key");
	CHECK(!menu_key(&m, HID_KEY_DOWN, 0) && m.sel == 0, "down wraps: %d", m.sel);
	CHECK(!menu_key(&m, HID_KEY_UP, 0) && m.sel == 2, "up wraps: %d", m.sel);
	CHECK(!menu_key(&m, HID_KEY_LEFT, 0) && m.sel == 1, "left: %d", m.sel);
	CHECK(!menu_key(&m, HID_KEY_RIGHT, 0) && m.sel == 2, "right: %d", m.sel);
	CHECK(menu_key(&m, HID_KEY_ENTER, 0) && m.how == MENU_KEY && m.sel == 2,
	      "enter: how %d sel %d", m.how, m.sel);
	printf("  Down, Down (wraps), Up (wraps), Left, Right, Enter: entry 3\n");

	/* digits */
	menu_start(&m, 3, 0, t0, BOOT_MENU_S * S);
	CHECK(menu_key(&m, 0x20, 0) && m.sel == 2 && m.how == MENU_KEY, "3: sel %d", m.sel);
	menu_start(&m, 3, 0, t0, BOOT_MENU_S * S);
	CHECK(menu_key(&m, 0x5A, 0) && m.sel == 1, "keypad 2: sel %d", m.sel);
	menu_start(&m, 3, 0, t0, BOOT_MENU_S * S);
	CHECK(!menu_key(&m, 0x23, 0) && m.sel == 0 && menu_seconds_left(&m, t0) == -1,
	      "6 of 3: should only stop the countdown");
	CHECK(menu_key(&m, HID_KEY_KP_ENTER, 0) && m.sel == 0, "keypad Enter");
	printf("  3 starts entry 3, keypad 2 entry 2; 6 of 3 only stops the countdown\n");

	/* other keys */
	menu_start(&m, 2, 0, t0, BOOT_MENU_S * S);
	CHECK(!menu_key(&m, 0xE1, KBD_MOD_LSHIFT) && menu_seconds_left(&m, t0) > 0,
	      "a modifier alone stopped the countdown");
	CHECK(!menu_key(&m, HID_KEY_ESC, 0) && menu_seconds_left(&m, t0) == -1,
	      "Esc did not stop the countdown");
	CHECK(menu_key(&m, HID_KEY_ENTER, 0) && m.sel == 0, "enter after Esc");
	printf("  Shift alone changes nothing; Esc stops the countdown\n");

	/* one entry, a bad default */
	menu_start(&m, 1, 5, t0, BOOT_MENU_S * S);
	CHECK(m.sel == 0, "default out of range: %d", m.sel);
	CHECK(!menu_key(&m, HID_KEY_DOWN, 0) && m.sel == 0, "one entry: down %d", m.sel);

	printf("bootmenu: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
