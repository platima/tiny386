/*
 * The boot menu: with more than one ini in the card's tiny386 folder, they
 * are listed on the boot console and the keyboard picks one.
 *
 * Up and Down (or Left and Right) move, Enter starts the one selected, a
 * digit starts that one at once.  Without a key, the default (the last one
 * chosen, else tiny386.ini, else freedos.ini) starts after BOOT_MENU_S
 * seconds; any key stops that countdown, so there is time to read.  The
 * selection and the countdown are on the console's title row.
 *
 * The choice is kept in lastboot.txt beside the inis (setup.c), written
 * only when a key chose something other than what is there.
 *
 * The state machine (menu_*) is plain C for tests/host/test_bootmenu.c;
 * boot_menu() is the firmware's part.
 */
#ifndef BOOTMENU_H
#define BOOTMENU_H

#include <stdbool.h>
#include <stdint.h>

#ifndef BOOT_MENU_S
#define BOOT_MENU_S 6
#endif

enum menu_how {
	MENU_RUNNING,
	MENU_TIMEOUT,           /* the countdown ran out */
	MENU_KEY,               /* Enter or a digit */
};

struct menu {
	int n, sel;
	bool counting;          /* the countdown is running */
	uint32_t deadline;      /* when it runs out, microseconds */
	enum menu_how how;
};

void menu_start(struct menu *m, int n, int deflt, uint32_t now, uint32_t timeout_us);
/* A key (HID usage, modifiers).  True once the menu is finished. */
bool menu_key(struct menu *m, uint8_t usage, uint8_t mods);
/* The time now.  True once the menu is finished. */
bool menu_tick(struct menu *m, uint32_t now);
/* Whole seconds left in the countdown, rounded up; -1 if it was stopped. */
int menu_seconds_left(const struct menu *m, uint32_t now);

#ifndef BOOTMENU_HOST
struct setup_list;
/* Shows the list, lets the keyboard (or the countdown) choose, writes
   lastboot.txt if a key chose; returns the index.  The USB host must have
   been started.  title_after: the console's title to put back. */
int boot_menu(const struct setup_list *l, int deflt, const char *title_after);
#endif

#endif /* BOOTMENU_H */
