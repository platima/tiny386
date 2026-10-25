/*
 * The boot console: text on the panel from the first second of boot, a log
 * ring that survives a reset, and the crash screen's drawing.
 *
 * It knows nothing about the board beyond a frame buffer of RGB565 pixels
 * and a function that makes drawn rows visible (lcd_flush() on boards with
 * lcd.h).  8x16 characters from tiny386's own VGA font, code page 437: 100
 * columns on an 800-pixel panel, a title row at the top, and a colour card
 * along the bottom 16 pixels (eight bars and a grey ramp) to check the
 * panel's colour order.
 *
 * Every line goes to the UART, to the ring, and to the panel while the
 * console has it.  Once the emulator's picture takes the panel
 * (console_release_display()), lines go to the UART and the ring only.
 * While the PC runs (v3.8), the UART only with [esp] uart_log = 1, and then
 * through a queue; and lines that repeat are held back after the first few
 * (console_log_limit()).
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

enum console_level {
	CON_INFO,       /* light grey */
	CON_GOOD,       /* green: something worked */
	CON_WARN,       /* yellow */
	CON_ERROR,      /* red */
	CON_OLD,        /* cyan: lines from the run before this reset */
	CON_TITLE,      /* white on blue */
	CON_NLEVELS
};

typedef void console_flush_fn(int x, int y, int w, int h);

/* fb: width x height pixels, stride width.  fb may be NULL (no panel): the
   console then writes to the UART and the ring only. */
void console_init(uint16_t *fb, int width, int height, console_flush_fn *flush);

/* ESP_LOGx (esp_log_set_vprintf), printf and fprintf(stderr) (stdout and
   stderr made a tee), and esp_rom_printf (ROM printf channel 2, into the
   ring only).  Call once, after console_init(), before any task starts. */
void console_hooks_install(void);

/* The fixed top row. */
void console_set_title(const char *title);

void console_puts(int level, const char *s);    /* one line or several */
void console_printf(int level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void console_vprintf(int level, const char *fmt, va_list ap);
/* To the UART only: for numbers printed every few seconds, which would
   push the useful lines out of the ring. */
void console_uart_printf(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));
/* true: lines go to the ring and the panel but not to the UART (the PC
   running without [esp] uart_log = 1); false: to the UART too.  A fatal
   message turns the UART back on. */
void console_uart_quiet(bool quiet);
/* true (the PC running with [esp] uart_log = 1): lines for the UART go
   into a 4 KiB queue that a task on core 0 writes out as the UART takes
   them, so that no task waits for the UART (at 115200 baud, 87 us a byte);
   a line that does not fit is dropped from the UART only, and counted.
   false: what is queued is written out now, by the caller, and lines go
   straight to the UART again (an emulator abort). */
void console_uart_async(bool on);

/* true (the PC running without [esp] verbose_log = 1): of each kind of line
   that comes through printf, fprintf(stderr) and ESP_LOGx (a kind being
   the text with its numbers taken out), the first 5 in 5 s go out, and the
   rest are counted and said in one line at the end of the 5 s.
   console_printf() and the rest above are never held back.  false: every
   line again, the counts held so far said first. */
void console_log_limit(bool on);
/* says the counts whose 5 s are over; cheap when there are none (the
   speed screen's poll, every frame, on core 0) */
void console_log_tick(void);
/* lines held back by the limit, and lines dropped from the UART queue,
   since boot */
void console_log_stats(unsigned *held, unsigned *dropped);

/* Warnings and errors shown since boot. */
int console_problems(void);

/* Lines kept from the run before this reset.  console_init() takes them
   out of the ring (which then starts again empty) and remembers whether
   that run ended on the crash screen. */
int console_previous_lines(void);
bool console_previous_crashed(void);
/* Shows them, in cyan, under a heading that gives the reason. */
void console_show_previous(const char *why);

/* Who draws on the panel.  The console has it from console_init() until
   console_release_display(); the emulator's VGA code then draws, under
   display_lock(), while !console_owns_display().  console_take_display()
   takes it back and draws the whole console again. */
void console_release_display(void);
void console_take_display(void);
bool console_owns_display(void);
/* console_take_display() for good: after a fatal message or once the PC is
   switched off, the speed screen (Ctrl+Alt+F12, the USER key) may no
   longer give the panel back to the stopped PC's picture. */
void console_hold(void);
bool console_held(void);
void display_lock(void);
void display_unlock(void);

/* An error the firmware cannot go on from: the message in red, the panel
   given back to the console, and the calling task stopped for good.  For
   task context only. */
void console_fatal(const char *fmt, ...)
	__attribute__((noreturn, format(printf, 1, 2)));

/* ==== for the crash screen (crash.c); nothing here takes a lock ==== */

#define CRASH_BT_MAX 12
struct crash_report {
	int core;
	const char *what;       /* "Load access fault", "abort() was called" */
	const char *detail;     /* the abort or assert text, or NULL */
	uint32_t mepc, ra, sp, mcause, mtval;
	uint32_t bt[CRASH_BT_MAX];  /* code addresses seen on the stack */
	int nbt;
};

/* The frame buffer the console was given, and its size. */
uint16_t *console_fb(int *width, int *height);
/* v4.4: the frame buffer the console (and the crash screen) draws into
   from now on, the same size (the front one of the board's double
   buffering).  Drawn whole the next time the console draws, if it has the
   panel.  Never draws itself: the caller may hold the display lock. */
void console_set_fb(uint16_t *fb);

/* Draws the crash screen into base (width x height, stride width): with
   the frame buffer's cache-bypass address, every store goes straight to
   PSRAM.  Also notes the report in the ring and marks it for the next
   boot. */
void console_draw_crash(uint16_t *base, int width, int height,
			const struct crash_report *r);
/* v4.6, after its review: the same report again, after console_draw_crash(),
   into the board's second frame buffer (double buffering), with the log's
   lines that call read and nothing more into the ring: the two pictures
   are the same, and the ring has the report once. */
void console_draw_crash_again(uint16_t *base, int width, int height,
			      const struct crash_report *r);

/* The last 'lines' lines of the ring into out, '\n' after each, '\0' at
   the end; returns how many lines. */
int console_ring_tail(char *out, int size, int lines);

#endif /* CONSOLE_H */
