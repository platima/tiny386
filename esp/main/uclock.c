/*
 * uclock.c: v5.3 (QW_UCLK), core 1's clock from its cycle counter: the
 * true read it falls back to, its start and its idle hook.  uclock.h has
 * the reasoning; get_uticks() (esp_main.c) reads it on core 1.
 */
#include "sdkconfig.h"
#include "../../qw.h"

#if QW_UCLK && CONFIG_IDF_TARGET_ESP32S31

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "esp_private/esp_clk.h"
#include "esp_attr.h"
#define UCLK_DEFINE_SYNC
#include "uclock.h"
#include "console.h"

struct uclock uclock_c1 = { .gen = 1 };

static bool uclock_idle_hook(void)
{
	uclock_idle(&uclock_c1);
	return true;
}

/* in the tick's interrupt, which may come while the cache is off */
static void IRAM_ATTR uclock_tick_hook(void)
{
	uclock_tick(&uclock_c1);
}

/* the time gone by less the cycles' time over about 20 ms, busy or idle,
   each end read with interrupts masked */
static int32_t uclock_try(uint32_t mhz, bool idle)
{
	uint32_t s = UCLK_LOCK();
	uint32_t t0 = UCLK_TRUE_US(), c0 = UCLK_CYCLES();
	UCLK_UNLOCK(s);
	if (idle)
		vTaskDelay(pdMS_TO_TICKS(20) + 1);
	else
		while (UCLK_TRUE_US() - t0 < 20000)
			;
	s = UCLK_LOCK();
	uint32_t t1 = UCLK_TRUE_US(), c1 = UCLK_CYCLES();
	UCLK_UNLOCK(s);
	return (int32_t) ((t1 - t0) - (c1 - c0) / mhz);
}

void uclock_start(void)
{
	struct uclock k = { .gen = 1 };
	int hz = esp_clk_cpu_freq();
	uint32_t mhz = (uint32_t) hz / 1000000;
	int32_t busy = 0, idle = 0;
	const char *why = NULL;

	if (esp_cpu_get_core_id() != 1)
		why = "not started on core 1";
	else if (hz <= 0 || hz % 1000000)
		why = "the CPU's clock is not a whole number of MHz";
	else if (!uclock_setup(&k, mhz))
		why = "no exact reciprocal for the CPU's clock";
	if (!why) {
		busy = uclock_try(mhz, false);
		idle = uclock_try(mhz, true);
		/* (the review of batch 4: the idle part too, uclock.h) */
		why = uclock_refuse(busy, idle);
	}
	if (!why) {
		if (esp_register_freertos_idle_hook_for_cpu(uclock_idle_hook, 1) != ESP_OK)
			why = "no room for its idle hook";
		else if (esp_register_freertos_tick_hook_for_cpu(uclock_tick_hook, 1) != ESP_OK) {
			esp_deregister_freertos_idle_hook_for_cpu(uclock_idle_hook, 1);
			why = "no room for its tick hook";
		}
	}
	if (why) {
		console_printf(CON_WARN, "Clock: the true read each time (QW_UCLK off: %s; "
			       "%lu MHz, against the clock over 20 ms: busy %+ld us, idle %+ld us).",
			       why, (unsigned long) mhz, (long) busy, (long) idle);
		return;
	}
	k.floor = UCLK_TRUE_US();
	uint32_t s = UCLK_LOCK();
	uclock_c1.mhz = k.mhz;
	uclock_c1.floor = k.floor;
	uclock_c1.recip = k.recip;
	uclock_c1.syncs = 0;
	uclock_c1.waits = 0;
	uclock_c1.gen = 1;      /* odd: the next read syncs */
	UCLK_UNLOCK(s);
	console_printf(CON_INFO, "Clock: core 1's cycle counter (%lu MHz) between true reads; "
		       "against the clock over 20 ms: busy %+ld us, idle %+ld us (it counts "
		       "through waits).", (unsigned long) mhz, (long) busy, (long) idle);
}

/* the run loop's line when the clock has gone back to the true read */
void uclock_say(void)
{
	static uint8_t said;
	if (said)
		return;
	said = 1;
	console_printf(CON_WARN, "Clock: back to the true read each time: the cycle counter ran %s "
		       "the clock by %ld us in a span (after %lu true reads, %lu waits).",
		       uclock_c1.off == 1 ? "ahead of" : "behind", (long) uclock_c1.err,
		       (unsigned long) uclock_c1.syncs, (unsigned long) uclock_c1.waits);
}

/* the end of the run (with the clock off its true reads are not counted:
   uclock_slow()) */
void uclock_totals(uint32_t run_s)
{
	if (!uclock_c1.recip && !uclock_c1.off) {
		console_printf(CON_INFO, "Clock: the true read each time in %lu s of run.",
			       (unsigned long) run_s);
		return;
	}
	console_printf(CON_INFO, "Clock: %lu true reads in %lu s of run, %lu of them after waits; %s.",
		       (unsigned long) uclock_c1.syncs, (unsigned long) run_s,
		       (unsigned long) uclock_c1.waits,
		       uclock_c1.recip ? "still on the cycle counter" : "then the true read each time");
}

#endif
