/*
 * The status LED's codes, patterns and wire format: plain C, no ESP-IDF, so
 * that tests/host/test_status.c runs the same code on a PC.  status_led.c
 * drives the LED with it (RMT in normal running, a bit-banged loop in the
 * panic handler).  STATUS-LED.md beside the images is the table for people.
 *
 * Both boards have one WS2812-protocol RGB LED on GPIO37 behind a BSS138
 * level shifter to 5 V: the Korvo-1 a WS2812B (U3, Q2 LBSS138LT1G, Korvo-1
 * schematic v1.1 page 3), the DOS board from rev A2 an Inolux IN-PI554FCH
 * (D201, Q201 BSS138, CHANGES-A2.md section 4).  The shifter does not
 * invert: GPIO37 high is 5 V at the LED.
 */
#ifndef STATUS_CODE_H
#define STATUS_CODE_H

#include <stdbool.h>
#include <stdint.h>

#if defined(STATUS_HOST) || !defined(ESP_PLATFORM)
#define STATUS_IRAM
#define STATUS_DRAM
#else
#include "esp_attr.h"
/* everything the panic handler's loop calls: in internal RAM */
#define STATUS_IRAM IRAM_ATTR
#define STATUS_DRAM DRAM_ATTR
#endif

/* ==== how far the boot has come: a steady colour each ==== */
enum status_stage {
	STATUS_STAGE_NONE = 0,  /* app_main() not reached: the LED is dark */
	STATUS_STAGE_STARTED,   /* white: app_main() runs, so the PSRAM chip was found
				   (and on the DOS board passed its memory test) */
	STATUS_STAGE_SCREEN,    /* blue: the panel or the HDMI picture is up */
	STATUS_STAGE_CARD,      /* cyan: the card is mounted */
	STATUS_STAGE_INI,       /* magenta: an ini was found (the menu may wait) */
	STATUS_STAGE_BIOS,      /* green: BIOS loaded, the console's hold */
	STATUS_STAGE_RUNNING,   /* green, the PC runs; blue for a moment each time the
				   guest reads or writes a disk (v3.8: the disk light) */
	STATUS_STAGE_PC_OFF,    /* white, 1 s on and 1 s off: the guest switched off */
	STATUS_STAGE_COUNT
};

/* ==== what went wrong: a colour and a number of blinks ==== */
enum status_error {
	STATUS_OK = 0,
	/* red: the card and the files on it */
	STATUS_ERR_NO_CARD,     /* red 1: no card can be read (tried again every 2 s) */
	STATUS_ERR_NO_INI,      /* red 2: no ini file on the card */
	STATUS_ERR_BIOS,        /* red 3: bios.bin or vgabios.bin missing or unreadable */
	STATUS_ERR_INI,         /* red 4: other problems in the ini (a disk image missing...) */
	STATUS_ERR_STOPPED,     /* red 5: tiny386 stopped for another reason (see the screen) */
	/* yellow: the board */
	STATUS_ERR_PSRAM,       /* yellow 1: stopped before tiny386 started: most likely the
				   PSRAM not found, or (DOS board) failing its memory test;
				   a PSRAM found but failing its set-up does not stop the
				   start-up, and ends later as red 5 */
	STATUS_ERR_HDMI_CHIP,   /* yellow 2: the MS7210 not found, or not answering */
	STATUS_ERR_NO_MONITOR,  /* yellow 3: no monitor (HDMI hot plug low) */
	STATUS_ERR_KEYBOARD,    /* yellow 4: the USB host or the CH9350L not working */
	STATUS_ERR_STALLED,     /* yellow 5: the emulator has made no progress for 10 s */
	/* red and white by turns: the crash screen (a panic) */
	STATUS_ERR_PANIC,
	STATUS_ERR_COUNT
};

/* why the last run ended, from the reset reason: the replay's lead-in */
enum status_cause {
	STATUS_CAUSE_NONE = 0,  /* power-on, the reset pin, a restart */
	STATUS_CAUSE_CRASH,     /* 1 white flash: a panic (the crash screen) */
	STATUS_CAUSE_WATCHDOG,  /* 2: a watchdog or a CPU lock-up */
	STATUS_CAUSE_POWER,     /* 3: a brownout or a power glitch */
};

const char *status_stage_name(int stage);
const char *status_error_name(int err);

/* ==== the state the pattern is made from ==== */
#define STATUS_STALL_MS   10000 /* no progress for this long: STATUS_ERR_STALLED */
/* How often the LED task looks (v3.8: 20 ms, was 50): each look is one
   step of the disk light, short enough to flicker as a PC's hard disk
   light does.  A whole number of FreeRTOS ticks (10 ms on both boards). */
#define STATUS_TICK_MS    20

struct status_state {
	uint8_t stage;          /* enum status_stage */
	uint8_t fatal;          /* the first error tiny386 stopped on, or 0 */
	uint16_t active;        /* bit e set: error e is on now (it may clear) */
	/* the run before this reset, shown first, from 0 ms to replay_end_ms */
	uint8_t prev_cause;     /* enum status_cause */
	uint8_t prev_stage;
	uint8_t prev_error;
	/* the disk light: 1 for a look in which the guest read or wrote a disk
	   (status_disk_step()); the LED task sets it on its own copy, so it
	   lasts one look.  Blue in place of the running green; nothing in any
	   other stage, and an error or the replay goes over it. */
	uint8_t disk;
	uint32_t replay_end_ms; /* 0: no replay */
};

/* Sets up the replay of the last run (cause, stage, error), or none. */
void status_replay_setup(struct status_state *s, int cause, int stage, int error);

/* The changes status_led.c makes, under its lock: the stage; an error that
   may clear itself, on or off; an error tiny386 stopped on (the first one
   stays).  Each returns the error the LED now shows. */
int status_state_stage(struct status_state *s, int stage);
int status_state_error(struct status_state *s, int err, bool on);
int status_state_fatal(struct status_state *s, int err);

/* The error the LED shows now, ahead of the stage: the fatal one, else the
   first active one in enum order; 0 for none. */
int status_shown_error(const struct status_state *s);

/* The LED's colour at now_ms (ms since status_led_init()), 0x00RRGGBB, at
   full scale (the brightness is applied by status_frame_grb()).  The PC
   running is a steady green, blue while s->disk is set. */
uint32_t status_pattern(const struct status_state *s, uint32_t now_ms);

/* The colour of the panic loop at ms since the panic: red and white by
   turns, or yellow 1 for a panic before app_main(). */
uint32_t status_panic_pattern(bool early, uint32_t ms);

/* One error's pattern on its own (the table's colour, blinks), for tests
   and for the replay. */
uint32_t status_error_pattern(int err, uint32_t ms);
/* the length of one cycle of it: the blinks and the pause after them */
uint32_t status_error_cycle_ms(int err);
/* its colour and its number of blinks */
uint32_t status_error_rgb(int err);
int status_error_blinks(int err);
uint32_t status_stage_rgb(int stage);

/* WS2812 order on the wire: green, red, blue, most significant bit first. */
static inline uint32_t status_grb(uint32_t rgb)
{
	return ((rgb >> 8) & 0xff) << 16 | ((rgb >> 16) & 0xff) << 8 | (rgb & 0xff);
}

/* ==== brightness ====
 * The colours above (status_pattern() and the rest) are at full scale:
 * red, the brightest, at 255.  One level, 0 to 255, scales every colour
 * where it becomes a frame, for the RMT and for the panic bit-bang alike:
 * status_frame_grb().  Each channel comes out as channel * level / 255 to
 * the nearest step, except that a channel lit in the colour stays at 1 or
 * more at any level above 0, so no colour turns into another; 0 is dark,
 * a crash included, and 255 the colours as they are.  The level is the
 * default from power-on until the ini's [esp] led_brightness is read
 * (esp_main.c). */
#define STATUS_BRIGHTNESS_DEFAULT 9     /* red 9 of 255; v3.6 had 70: about an eighth */
#define STATUS_BRIGHTNESS_MAX     255

/* rgb with each channel scaled to level (above 255 taken as 255) */
uint32_t status_scale(uint32_t rgb, uint32_t level);
/* the level for every frame from now on; below 0 is 0, above 255 is 255 */
void status_brightness_set(int level);
uint32_t status_brightness(void);
/* rgb at the level set, in wire order: what every frame is made from */
uint32_t status_frame_grb(uint32_t rgb);

/* ==== no progress: the emulator's beat ==== */
struct status_watch {
	uint32_t last_beats;
	uint32_t last_change_ms;
	bool stalled;
};

/* Called every STATUS_TICK_MS with the beat count; returns true while the
   count has not moved for STATUS_STALL_MS with the PC running.  The beat
   is the emulator's own (run_pc() once a step), so this needs no blink on
   the LED: a running green that stays green while the emulator has stopped
   turns to yellow 5. */
bool status_watch_step(struct status_watch *w, uint32_t beats, uint32_t now_ms,
		       bool running);

/* ==== the disk light (v3.8) ====
 * The emulator counts the guest's reads and writes of its disk, CD and
 * floppy images (ide.c, misc.c: one store each, nothing more), and the LED
 * task, at each look, asks whether the count has moved since the last look:
 * if so, that look is blue (status_state.disk).  So one access lights it
 * for one look, 20 ms; accesses a look or less apart keep it lit, and the
 * gaps between bursts show green, as a PC's hard disk light flickers. */
struct status_disk {
	uint32_t last;          /* the count at the last look */
};

/* true if count differs from the last look's (a wrap counts as a move) */
bool status_disk_step(struct status_disk *d, uint32_t count);

/* ==== the last run, kept in memory a reset does not clear ==== */
#define STATUS_RECORD_MAGIC 0x4c454453u /* "SDEL" */

struct status_record {
	uint32_t magic;
	uint8_t stage;
	uint8_t error;          /* what the LED showed (status_shown_error()) */
	uint8_t spare[2];
	uint32_t check;         /* the two words above, mixed: not just noise */
};

void status_record_put(struct status_record *r, int stage, int error);
bool status_record_valid(const struct status_record *r);

/* ==== the bits on the wire ====
 * One timing for both LEDs.  Inolux IN-PI554FCH datasheet v2.2 p. 11: T0H
 * 0.3, T1H 0.6, T0L 0.9, T1L 0.6 us, each +-0.15 us, a bit 1.25 +-0.6 us,
 * reset 80 us or more.  Worldsemi WS2812B-2020-V6 p. 5 (the nearest
 * Worldsemi datasheet to hand; which WS2812B revision the Korvo-1 carries
 * is not known): T0H 0.22 to 0.38, T1H 0.58 to 1, T0L and T1L 0.58 to 1 us,
 * a bit 1.25 us or more, reset 280 us or more.  Together: T0H 220 to 380
 * ns, T0L 750 to 1000, T1H 580 to 750, T1L 580 to 750, a bit 1250 to 1850.
 *
 * The level shifter (a BSS138 and a 1k pull-up to 5 V on both boards)
 * slows each rise at the LED by about 38 ns (CHANGES-A2.md section 4, an
 * estimate), so the LED sees each high time that much shorter and each low
 * time that much longer.  The LED tells a 0 from a 1 by the high time, so
 * the high times are set to be inside both windows as sent and as the LED
 * sees them, and the T1H is also inside an older WS2812B datasheet's 0.8
 * +-0.15 us as the LED sees it (reported, not read at Worldsemi): 700 ns
 * sent, about 662 ns at the LED.  The low times are inside both windows as
 * sent; T0L at the LED comes to about the WS2812B-2020's 1 us maximum.
 *
 * The bit-bang's edges land up to one pass round its waiting loop late (3
 * instructions; 37.5 ns at the 80 MHz the second-stage bootloader runs the
 * CPU at, 9.4 ns at 320 MHz), the same for a rise as for a fall
 * (status_bb.h), so a time can come out that much shorter or longer; T1's
 * period is 1325 ns so that T1L stays above 580 ns with that.  In steps of
 * 12.5 ns, the RMT's tick.  In picoseconds: */
#define STATUS_T0H_PS   337500
#define STATUS_T0_PS    1300000 /* so T0L 962.5 ns */
#define STATUS_T1H_PS   700000
#define STATUS_T1_PS    1325000 /* so T1L 625 ns */
#define STATUS_RESET_US 300

/* RMT: 80 MHz (REF_F80M undivided), 12.5 ns a tick.  A frame is one reset
   symbol (the line low for STATUS_RESET_US, so that the LED starts a new
   frame whatever the line did before) and 24 bit symbols, in the packed
   form of ESP-IDF's rmt_symbol_word_t: duration0 bits 0 to 14, level0 bit
   15, duration1 bits 16 to 30, level1 bit 31.  The line stays low after it
   (the end-of-transmission level), and the LED shows the colour once it
   has been low for its reset time. */
#define STATUS_RMT_HZ      80000000
#define STATUS_RMT_SYMBOLS 25
void status_rmt_frame(uint32_t grb, uint32_t out[STATUS_RMT_SYMBOLS]);

/* The panic handler's bit-bang, in CPU cycles (status_bb.h). */
struct status_bb {
	uint32_t t0h, t0, t1h, t1, reset;
};
void status_bb_timing(struct status_bb *t, uint32_t cycles_per_us);

#endif /* STATUS_CODE_H */
