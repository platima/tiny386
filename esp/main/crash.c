/*
 * The crash screen.
 *
 * The link wraps ESP-IDF's esp_panic_handler() (-Wl,-wrap=esp_panic_handler
 * in CMakeLists.txt), so every panic, abort, failed assert and interrupt
 * watchdog timeout comes here first.  The task watchdog is not among them:
 * without CONFIG_ESP_TASK_WDT_PANIC (not set in sdkconfig.korvo1 or
 * sdkconfig.dosboard) a task watchdog timeout only prints its report on
 * the UART and goes on (esp_system/task_wdt/task_wdt.c), so a starved
 * core 0 shows no crash screen; setting it is Plat's choice (BUILD-NOTES,
 * concurrency review round 2, note 4).
 *
 * By then the port's handler has stopped the
 * other core, turned the cache back on if it was off, turned off the timer
 * group watchdogs and set the RTC watchdog to 10 s (ESP-IDF
 * components/esp_system/port/panic_handler.c).
 *
 * Here: write the whole data cache back, draw the report through the
 * frame buffer's cache-bypass address (+SOC_NON_CACHEABLE_OFFSET_PSRAM),
 * so that every store goes straight to PSRAM where the panel's DMA reads
 * it, note it in the log ring for the next boot, then hand over to
 * ESP-IDF's own handler, which prints the full report on the UART.  With
 * CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT it then halts without resetting the
 * LCD or its DMA (components/esp_system/port/soc/esp32s31/system_internal.c,
 * esp_system_reset_modules_on_exit()), so the screen stays until RESET.
 *
 * The status LED (status_led.c): its first crash frame goes out on the way
 * in, before the screen is drawn (a panic before app_main() has no screen,
 * and the LED is all there is); then, with -Wl,--wrap of
 * esp_system_reset_modules_on_exit() too, the halt loop runs the LED's
 * crash pattern where ESP-IDF's halt stub would loop doing nothing.  The
 * stub calls it from panic.c, another object, so the wrap reaches that call;
 * the restart paths call it inside system_internal.c, where it does not.
 */
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "riscv/rvruntime-frames.h"
#include "hal/cache_ll.h"
#include "soc/soc.h"

#include "console.h"
#include "status_led.h"

void __real_esp_panic_handler(panic_info_t *info);
void __real_esp_system_reset_modules_on_exit(void);

/* linker symbols: the code in internal RAM, and in flash */
extern int _iram_text_start, _iram_text_end, _text_start, _text_end;

static bool is_code(uint32_t a)
{
	return (a >= (uint32_t) &_iram_text_start && a < (uint32_t) &_iram_text_end) ||
	       (a >= (uint32_t) &_text_start && a < (uint32_t) &_text_end);
}

static bool is_stack(uint32_t a)
{
	return a >= SOC_DRAM_LOW && a + 4 <= SOC_DRAM_HIGH && !(a & 3);
}

static struct crash_report s_report;

static void crash_screen(panic_info_t *info)
{
	struct crash_report *r = &s_report;
	const RvExcFrame *f = info->frame;

	r->core = info->core;
	if (g_panic_abort) {
		r->what = "abort() was called";
		r->detail = g_panic_abort_details;
	} else {
		r->what = info->reason;
		r->detail = info->description;
	}
	if (f) {
		r->mepc = f->mepc;
		r->ra = f->ra;
		r->sp = f->sp;
		r->mcause = f->mcause;
		r->mtval = f->mtval;
		/* Return addresses: words on the stack that point into code.
		   Without frame pointers this is a guess, but a good one: the
		   real ones are all there, newest first, with a few stale
		   ones between. */
		uint32_t sp = f->sp;
		if (is_code(f->ra))
			r->bt[r->nbt++] = f->ra;
		for (int i = 0; i < 256 && r->nbt < CRASH_BT_MAX; i++) {
			uint32_t a = sp + 4 * i;
			if (!is_stack(a))
				break;
			uint32_t v = *(const uint32_t *) a;
			if (is_code(v) && v != r->bt[r->nbt ? r->nbt - 1 : 0])
				r->bt[r->nbt++] = v;
		}
	}

	int w, h;
	uint16_t *fb = console_fb(&w, &h);
	uint16_t *base = NULL;
	if (fb && (uint32_t) fb >= SOC_EXTRAM_LOW && (uint32_t) fb < SOC_EXTRAM_HIGH) {
		/* nothing dirty left in the cache to land on the screen later */
		cache_ll_writeback_all(CACHE_LL_LEVEL_ALL, CACHE_TYPE_DATA, CACHE_LL_ID_ALL);
		base = (uint16_t *) ((uint32_t) fb + SOC_NON_CACHEABLE_OFFSET_PSRAM);
	}
	console_draw_crash(base, w, h, r);
}

void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
	static int entered;
	if (!entered++) {
		status_led_panic();
		crash_screen(info);
	}
	__real_esp_panic_handler(info);
}

/* ESP-IDF's halt stub (CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT): the modules'
   reset on exit as before, then, after a panic, the status LED's loop for
   ever instead of the stub's own empty one. */
void IRAM_ATTR __wrap_esp_system_reset_modules_on_exit(void)
{
	__real_esp_system_reset_modules_on_exit();
#ifdef STATUS_LED_GPIO
	if (status_led_panicking())
		status_led_halt();
#endif
}
