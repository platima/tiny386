/*
 * How fast the emulator runs the guest (FIRMWARE-PLAN 3.5).
 *
 * After every pc_step() the emulator's core hands in the clock, the CPU's
 * instruction count (cpui386_get_cycle(), one a guest instruction) and the
 * byte at 0000:04F0.  From these:
 *
 *   each second, thousand guest instructions a second of running (time in
 *     which the guest executed nothing, halted waiting for an interrupt,
 *     is counted apart, as the Linux route does);
 *   BENCH.COM's marks, as the Linux route's -marks has them: the guest
 *     writes 0xA1 to 0xAE to 0000:04F0 as it starts each piece of work and
 *     0xAF when it is done; each piece is timed from its mark to the next,
 *     and the lines printed have the Linux route's own form, so the two
 *     can be laid side by side.
 *
 * On the board, Ctrl+Alt+F12 (or holding the board's USER key, where there
 * is one) shows the boot console over the picture, with the figures on its
 * title row, and again gives the picture back.  Every 10 s a line goes to
 * the UART.  When BENCH finishes, its lines go to the console, the UART and
 * the log ring, and the console is shown.
 *
 * The counting (speed_*) is plain C for tests/host/test_speed.c.
 */
#ifndef SPEED_H
#define SPEED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPEED_MAX_MARKS 16

struct speed_mark {
	uint8_t mark;           /* 0xA1 to 0xAE: the piece that ended */
	uint64_t insns, us;
};

struct speed {
	uint32_t last_us, last_cycle;
	uint64_t insns, run_us, halt_us;        /* since speed_init() */
	/* the second being counted, and the last whole one */
	uint32_t win_start;
	uint64_t win_insns, win_run, win_halt;
	uint32_t kips;          /* thousand instructions a second of running */
	uint32_t halt_pct;      /* of the second, halted */
	uint32_t seconds;       /* whole seconds counted */
	/* BENCH.COM */
	uint8_t mark;
	bool marks_done;
	uint64_t mark_insns, mark_us;
	struct speed_mark marks[SPEED_MAX_MARKS];
	int nmarks;
};

#define SPEED_SECOND     1      /* kips and halt_pct are new */
#define SPEED_MARKS_DONE 2      /* the guest wrote 0xAF: marks[] complete */

void speed_init(struct speed *s, uint32_t now_us, uint32_t cycle);
int speed_step(struct speed *s, uint32_t now_us, uint32_t cycle, uint8_t mark_byte);

/* "tiny386: a1 16-bit arithmetic in registers  2097152 instructions in
   5.12 s:   410 thousand a second", as the Linux route prints it */
void speed_mark_line(const struct speed *s, int i, char *buf, size_t size);
/* "tiny386: N instructions in X s of running: Y million a second" for
   insns and us */
void speed_total_line(uint64_t insns, uint64_t us, const char *what,
		      char *buf, size_t size);
const char *speed_mark_name(int mark);

#ifndef SPEED_HOST
/* the firmware's part: speed.c (pc is the PC of pc.h) */
void speed_run_init(void *pc);          /* on the emulator's core, PC built */
void speed_run_step(void *pc, uint32_t now_us);        /* after each pc_step() */
/* time since the last step that was not the guest's (the card): left
   out, as the Linux route leaves it out */
void speed_run_aside(uint32_t now_us);
void speed_hotkey(void);                /* Ctrl+Alt+F12, from the USB task */
void speed_set_title(const char *normal);       /* the console's usual title */
void speed_screen_poll(void *pc, uint32_t pictures_per_s);     /* vga_task */
#endif

#endif /* SPEED_H */
