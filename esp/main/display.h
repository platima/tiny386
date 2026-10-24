/*
 * The drawing loop's settings (display.c, for boards whose VGA code draws
 * straight into the panel's frame buffer, LCD_ZERO_COPY).
 *
 * v4.4: pictures taken at the guest's quiet moments ([esp] pace_quiet and
 * pace_wait_ms, Ctrl+Alt+F10; vga.h says what quiet is).  With
 * vga_pace_quiet not 0, vga_task waits before each picture until the
 * emulator's core finds the guest quiet (display_pace_step()), or at most
 * the wait, then draws.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/* [esp] pace_wait_ms without a line: the most a picture is held back.  The
   wait is counted in FreeRTOS's ticks of 10 ms, rounded up: 15 ms is two
   ticks, between 10 and 20 ms as the tick falls (BUILD-NOTES 29) */
#define DISPLAY_PACE_WAIT_MS 15
/* the guest's instructions with no store into video memory that make a
   quiet moment when Ctrl+Alt+F10 turns the pacing on and the ini gave
   none: on the PC harness, Plat's session with the pointer's byte changed
   had 2, 0 and 1 per cent of its pictures taken part way through an update
   while typing, scrolling and moving a window, where it had 7, 4 and 4
   without the pacing (BUILD-NOTES 29) */
#define DISPLAY_PACE_QUIET_ON 5000

void display_pace_set_wait(int ms);
int display_pace_wait(void);
/* pictures drawn at a quiet moment, and at the end of the wait, since the
   PC started */
void display_pace_counts(uint32_t *quiet, uint32_t *late);
/* on the emulator's core after each step: the guest's instruction count,
   and whether the step ran none */
void display_pace_step(uint32_t insns, bool halted);

#endif /* DISPLAY_H */
