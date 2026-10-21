/*
 * A USB boot keyboard report into tiny386's PS/2 keyboard.
 *
 * Stand-in for the boot report half of FIRMWARE-PLAN 3.3's kbd_input.c,
 * which is being written in the shared tree: when that is merged, the two
 * calls in ch9350.c (bootkbd_report, bootkbd_release_all) go to it instead
 * and this file goes.  No typematic repeat here.
 */
#ifndef CH9350_KBD_H
#define CH9350_KBD_H

#include <stdint.h>

typedef struct {
	uint8_t prev[8];             /* last report taken */
	uint32_t rollover_ignored;   /* ErrorRollOver reports (every key 0x01) */
} bootkbd_t;

/* ps2kbd is the emulator's PS2KbdState (globals.kbd); NULL drops the event. */
void bootkbd_report(bootkbd_t *k, void *ps2kbd, const uint8_t report[8]);
void bootkbd_release_all(bootkbd_t *k, void *ps2kbd);

#endif /* CH9350_KBD_H */
