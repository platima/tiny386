/*
 * The boot menu: see bootmenu.h.
 */
#include <stdio.h>
#include <string.h>

#include "bootmenu.h"
#include "kbd_input.h"

/* ==== the state machine ==== */

void menu_start(struct menu *m, int n, int deflt, uint32_t now, uint32_t timeout_us)
{
	m->n = n;
	m->sel = deflt >= 0 && deflt < n ? deflt : 0;
	m->counting = true;
	m->deadline = now + timeout_us;
	m->how = MENU_RUNNING;
}

/* 1 to 9 on the top row or the keypad; 0 if not a digit */
static int digit_of(uint8_t u)
{
	if (u >= 0x1E && u <= 0x26)
		return u - 0x1E + 1;
	if (u >= 0x59 && u <= 0x61)
		return u - 0x59 + 1;
	return 0;
}

bool menu_key(struct menu *m, uint8_t usage, uint8_t mods)
{
	(void) mods;
	if (m->how != MENU_RUNNING)
		return true;
	if (usage >= 0xE0 && usage <= 0xE7)
		return false;           /* a modifier alone is not a choice */
	m->counting = false;            /* someone is there: no hurry */
	int d = digit_of(usage);
	if (d && d <= m->n) {
		m->sel = d - 1;
		m->how = MENU_KEY;
		return true;
	}
	switch (usage) {
	case HID_KEY_UP:
	case HID_KEY_LEFT:
		m->sel = (m->sel + m->n - 1) % m->n;
		break;
	case HID_KEY_DOWN:
	case HID_KEY_RIGHT:
		m->sel = (m->sel + 1) % m->n;
		break;
	case HID_KEY_ENTER:
	case HID_KEY_KP_ENTER:
		m->how = MENU_KEY;
		return true;
	default:
		break;
	}
	return false;
}

bool menu_tick(struct menu *m, uint32_t now)
{
	if (m->how != MENU_RUNNING)
		return true;
	if (m->counting && (int32_t) (now - m->deadline) >= 0) {
		m->how = MENU_TIMEOUT;
		return true;
	}
	return false;
}

int menu_seconds_left(const struct menu *m, uint32_t now)
{
	if (!m->counting)
		return -1;
	int32_t left = (int32_t) (m->deadline - now);
	if (left <= 0)
		return 0;
	return (left + 999999) / 1000000;
}

/* ==== on the board ==== */

#ifndef BOOTMENU_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "console.h"
#include "setup.h"

uint32_t get_uticks();

static QueueHandle_t s_keys;

/* from the USB task: a key for the menu */
static void menu_sink(uint8_t usage, uint8_t mods)
{
	uint8_t k[2] = { usage, mods };
	if (s_keys)
		xQueueSend(s_keys, k, 0);
}

static void show_title(const struct setup_list *l, const struct menu *m, uint32_t now)
{
	char t[128];
	int left = menu_seconds_left(m, now);
	char when[40];
	if (left >= 0)
		snprintf(when, sizeof(when), "starts in %d s", left);
	else
		snprintf(when, sizeof(when), "Enter starts it");
	snprintf(t, sizeof(t), " tiny386:  > %d %s   %s.  Up, Down, 1 to %d, Enter.",
		 m->sel + 1, l->ini[m->sel].name, when, l->n < 9 ? l->n : 9);
	console_set_title(t);
}

int boot_menu(const struct setup_list *l, int deflt, const char *title_after)
{
	s_keys = xQueueCreate(8, 2);
	kbd_input_set_sink(menu_sink);

	console_printf(CON_INFO, "%d setups in %s; choose one with the keyboard "
		       "(the title row shows which):", l->n, l->dir);
	for (int i = 0; i < l->n; i++) {
		char num[4] = "  ";
		if (i < 9)
			snprintf(num, sizeof(num), "%d", i + 1);
		console_printf(i == deflt ? CON_GOOD : CON_INFO, "  %2s  %-16s %s", num,
			       l->ini[i].name, l->ini[i].desc);
	}
	if (l->more)
		console_printf(CON_WARN, "  (%d more not listed: at most %d)", l->more,
			       SETUP_MAX_INIS);

	struct menu m;
	uint32_t now = get_uticks();
	menu_start(&m, l->n, deflt, now, BOOT_MENU_S * 1000000u);
	int shown_sel = -1, shown_left = -2;
	for (;;) {
		now = get_uticks();
		if (menu_tick(&m, now))
			break;
		int left = menu_seconds_left(&m, now);
		if (m.sel != shown_sel || left != shown_left) {
			show_title(l, &m, now);
			shown_sel = m.sel;
			shown_left = left;
		}
		uint8_t k[2];
		if (xQueueReceive(s_keys, k, pdMS_TO_TICKS(100)) == pdTRUE &&
		    menu_key(&m, k[0], k[1]))
			break;
	}
	kbd_input_set_sink(NULL);
	vQueueDelete(s_keys);
	s_keys = NULL;
	console_set_title(title_after);

	const char *name = l->ini[m.sel].name;
	if (m.how == MENU_KEY) {
		console_printf(CON_GOOD, "Chosen: %s.", name);
		if (!setup_last_write(l->dir, name))
			console_printf(CON_WARN, "Could not note the choice in %s/%s (is "
				       "the card write-protected?).", l->dir, SETUP_LAST_FILE);
	} else {
		console_printf(CON_GOOD, "Starting %s (no key in %d seconds).", name,
			       BOOT_MENU_S);
	}
	return m.sel;
}
#endif /* BOOTMENU_HOST */
