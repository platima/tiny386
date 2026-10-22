/*
 * The status LED: one WS2812-protocol RGB LED on STATUS_LED_GPIO (GPIO37 on
 * the Korvo-1 and on the DOS board from rev A2).  Boot stages by colour,
 * errors by colour and number of blinks, the crash in red and white, and
 * after a reset by a crash, a watchdog or a brownout, the last run's code
 * first.  status_code.h has the codes, STATUS-LED.md beside the images the
 * table.
 *
 * On a board without STATUS_LED_GPIO every call is an empty inline.
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "status_code.h"

#ifdef STATUS_LED_GPIO

/* First thing in app_main(): the record of the last run, the RMT channel,
   the first colour (white), and the LED's task. */
void status_led_init(void);
/* Once the boot console is up: says on it what the LED does, and what the
   last run's record held. */
void status_led_report(void);

void status_led_stage(int stage);
/* An error that may clear itself (no card yet, no monitor): on or off. */
void status_led_error(int err, bool on);
/* tiny386 has stopped: the first such error stays for good. */
void status_led_fatal(int err);

/* the emulator's beat: run_pc() once a step; the LED's task notices when it
   stops (STATUS_ERR_STALLED) */
extern volatile uint32_t status_led_beats;
static inline void status_led_heartbeat(void)
{
	__atomic_store_n(&status_led_beats, status_led_beats + 1, __ATOMIC_RELAXED);
}

/* The panic handler (crash.c): status_led_panic() on its way in, with
   interrupts off, bit-bangs the first frame of the crash pattern and notes
   the crash in the record; status_led_halt() is the halt loop that keeps
   the pattern going in place of ESP-IDF's empty one. */
void status_led_panic(void);
bool status_led_panicking(void);
void status_led_halt(void) __attribute__((noreturn));
/* crash_test = 5, in a -DCRASH_TEST=1 build only (esp_main.c): the next
   panic is shown as one before app_main() (yellow 1) */
void status_led_test_early(void);

#else

static inline void status_led_init(void) { }
static inline void status_led_report(void) { }
static inline void status_led_stage(int stage) { (void) stage; }
static inline void status_led_error(int err, bool on) { (void) err; (void) on; }
static inline void status_led_fatal(int err) { (void) err; }
static inline void status_led_heartbeat(void) { }
static inline void status_led_panic(void) { }
static inline bool status_led_panicking(void) { return false; }
static inline void status_led_test_early(void) { }

#endif

/* The board's audio, where it has a line out with a mute pin (i2s.c on the
   DOS board): muted, and the I2S output stopped, from the panic handler.
   Empty elsewhere. */
void audio_panic_quiet(void);

#endif /* STATUS_LED_H */
