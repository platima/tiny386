/*
 * uclock.h: v5.3 (QW_UCLK), the emulator's clock on core 1 from that
 * core's cycle counter (the S31 boards; uclock.c starts it).
 *
 * get_uticks() is read once a step of 512 instructions by
 * i8254_update_irq(), and each read went through esp_system_get_time() to
 * the ROM's systimer_hal_get_counter_value(), which has the systimer latch
 * its count and waits for it: 300 to 500 cycles, about 0.6% of the time of
 * fc /b and BENCH on the board and 0.7% of Windows' (v52-test3's
 * profiles).  Here core 1 reads its own cycle counter instead (csrr cycle,
 * as ESP-IDF's esp_cpu_get_cycle_count() does on the S31) and counts on
 * from the last true read:
 *
 *     us + (cycles now - cyc) / mhz
 *
 * the division a multiply by 2^32 / mhz rounded up, which gives the floor
 * exactly for every difference below UCLK_SPAN (uclock_setup() checks
 * that for the frequency it is given).  The clock is read again, and the
 * pair set from it (uclock_sync()), when the difference reaches
 * UCLK_SPAN, 2^22 cycles (13 ms at 320 MHz); after core 1 has been idle,
 * since FreeRTOS's idle hook (uclock_idle()) marks the pair stale before
 * each wait for an interrupt there; and on the first read.  Core 1's tick
 * (uclock_tick(), every 10 ms) marks it stale too once the span is over,
 * so that a pair is never used again after the counter's wrap (13.4 s),
 * however long core 1 goes without reading the clock.
 *
 * The idle hook and the wait are not one step: ESP-IDF's idle loop runs
 * the hooks, then more code, then the wait, and an interrupt in between
 * can wake a task that reads the clock and sets the pair again, with the
 * wait still to come.  Where the counter stops in waits, that wait would
 * go uncounted (the review of batch 4).  So the clock is used only where
 * uclock_start()'s test finds that the counter keeps counting through a
 * wait (uclock_refuse()), and there the hook only guards.
 *
 * Why the guest's time stays right:
 * - It never runs ahead of the true clock.  us is read before cyc, so us
 *   is no later than the true time at cyc; the floor of the cycles since
 *   is no more than the true time since, while the counter runs at mhz.
 * - It never goes back.  Each sync gives at least what the old pair would
 *   have given at that moment (want, below), and so at least every value
 *   given before it.
 * - It cannot drift: each sync puts it back on the true clock, and while
 *   the counter runs at mhz, waits included, it is behind by under 3 us
 *   (the 1 us of each reading, and the time from the systimer's latch to
 *   the cycle read).
 * - At each sync that ends a whole span with no wait in it, the cycles
 *   counted are held against the time gone by.  More than UCLK_SLACK us
 *   apart either way (a counter stopped, or a frequency changed) and the
 *   clock goes back to the true read for good, without going back in time.
 *   A span the tick or the idle hook ended has no such check, but its floor
 *   is what the pair gave or could have given, never above the true clock
 *   while the counter keeps time: a floor more than UCLK_SLACK us above the
 *   true read means the counter ran fast, and the clock goes off the same
 *   way (the review of batch 4; before, the new pair started at the true
 *   read, below what the old one had given).
 *
 * A read in between on the same core, from an interrupt or another task,
 * cannot take a half-set pair: gen changes at every set, a read that finds
 * it changed, or odd, takes the slow path, and a sync masks interrupts.
 * Core 0 reads the true clock as before; the two cores' clocks then differ
 * by under 3 us, and every comparison of times across the cores is a
 * signed difference (after_eq(), (int32_t)).
 *
 * The includer gives UCLK_CYCLES() (the cycle count, 32 bits),
 * UCLK_TRUE_US() (the true clock in us, 32 bits), UCLK_LOCK() and
 * UCLK_UNLOCK(state) (interrupts masked on this core).  UCLK_HOOK(n) is a
 * point at which tests/host/test_uclock.c reads the clock again, as an
 * interrupt could; nothing on the board.
 */
#ifndef UCLOCK_H
#define UCLOCK_H

#include <stdbool.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) && !defined(UCLK_CYCLES)
#include "freertos/FreeRTOS.h"
#include "esp_cpu.h"
#include "esp_private/system_internal.h"
#define UCLK_CYCLES() ((uint32_t) esp_cpu_get_cycle_count())
#define UCLK_TRUE_US() ((uint32_t) esp_system_get_time())
#define UCLK_LOCK() ((uint32_t) portSET_INTERRUPT_MASK_FROM_ISR())
#define UCLK_UNLOCK(s) portCLEAR_INTERRUPT_MASK_FROM_ISR(s)
#endif

#ifndef UCLK_SPAN
#define UCLK_SPAN (1u << 22)    /* cycles from one true read to the next at most */
#endif
#define UCLK_SLACK 8u           /* us: the counter against the clock, either way */
#ifndef UCLK_HOOK
#define UCLK_HOOK(n) do { } while (0)
#endif
#define UCLK_BARRIER() __asm__ __volatile__("" ::: "memory")

struct uclock {
	volatile uint32_t gen;  /* even: cyc and us belong together; odd: read the clock */
	uint32_t cyc;           /* the cycle count just after us was read */
	uint32_t us;            /* the true clock then */
	uint32_t recip;         /* 2^32 / mhz rounded up; 0: off, the true read every time */
	uint32_t mhz;
	uint32_t floor;         /* no sync gives less, when within UCLK_SLACK of it */
	uint32_t syncs;         /* true reads taken */
	uint32_t waits;         /* times marked stale by the idle hook */
	int32_t err;            /* off: the time gone by less the cycles' (us) */
	uint8_t off;            /* 1: the counter ran ahead, 2: behind */
};

/* uclock_start()'s verdict on its test, the time gone by less the
   cycles' time over about 20 ms busy and 20 ms idle (us): NULL when the
   clock can be used, or why not */
static inline const char *uclock_refuse(int32_t busy, int32_t idle)
{
	if (busy < -(int32_t) UCLK_SLACK || busy > (int32_t) UCLK_SLACK)
		return "the cycle counter does not keep time with the clock";
#ifndef UCLK_FAULT_IDLEOK
	if (idle < -(int32_t) UCLK_SLACK || idle > (int32_t) UCLK_SLACK)
		return "the cycle counter does not count through a wait for an interrupt";
#else
	(void) idle;
#endif
	return NULL;
}

/* the pair for mhz, or false when that frequency is no good here */
static inline bool uclock_setup(struct uclock *k, uint32_t mhz)
{
	if (mhz < 16 || mhz > 1000)
		return false;
	uint64_t r = ((1ull << 32) + mhz - 1) / mhz;
	uint64_t e = r * mhz - (1ull << 32);     /* below mhz */
	/* exact for every difference below UCLK_SPAN: (SPAN - 1) * e < 2^32 */
#ifndef UCLK_FAULT_SPAN
	if ((uint64_t) (UCLK_SPAN - 1) * e >= (1ull << 32))
		return false;
#endif
	(void) e;
	k->mhz = mhz;
	k->recip = (uint32_t) r;
	k->gen = 1;             /* the first read syncs */
	return true;
}

static inline uint32_t uclock_scale(const struct uclock *k, uint32_t dc)
{
	return (uint32_t) (((uint64_t) dc * k->recip) >> 32);
}

/* the fast read: true with *t set, or false for uclock_sync() */
static inline __attribute__((always_inline)) bool uclock_fast(struct uclock *k, uint32_t *t)
{
	uint32_t g = k->gen;
	UCLK_HOOK(1);
	UCLK_BARRIER();
	uint32_t b = k->cyc;
	UCLK_HOOK(2);
	uint32_t u = k->us;
	UCLK_HOOK(3);
	uint32_t dc = UCLK_CYCLES() - b;
	UCLK_HOOK(4);
	UCLK_BARRIER();
#ifndef UCLK_FAULT_NOGEN
	if ((g & 1) | (k->gen ^ g))
		return false;
#else
	if (g & 1)
		return false;
#endif
	if (dc >= UCLK_SPAN)
		return false;
	*t = u + uclock_scale(k, dc);
	return true;
}

/* the true read, and the pair set from it while the clock is on; in one
   file, which defines UCLK_DEFINE_SYNC (uclock.c), out of line */
uint32_t uclock_sync(struct uclock *k);
#ifdef UCLK_DEFINE_SYNC
uint32_t __attribute__((noinline)) uclock_sync(struct uclock *k)
{
	uint32_t s = UCLK_LOCK();
#ifndef UCLK_FAULT_ORDER
	uint32_t t = UCLK_TRUE_US();
	uint32_t c = UCLK_CYCLES();
#else
	uint32_t c = UCLK_CYCLES();
	uint32_t t = UCLK_TRUE_US();
#endif
	uint32_t g = k->gen;
	k->syncs++;
	if (k->recip && !(g & 1)) {
		/* the pair held since it was set: what it gives now, and, after a
		   span or two with no wait, the cycles against the time */
		uint32_t dc = c - k->cyc;
		uint32_t want = k->us + (dc < UCLK_SPAN ? uclock_scale(k, dc) : dc / k->mhz);
		int32_t err = (int32_t) (t - want);
#ifndef UCLK_FAULT_NORATE
		if (dc < 2 * UCLK_SPAN && (err < -(int32_t) UCLK_SLACK || err > (int32_t) UCLK_SLACK)) {
			k->recip = 0;
			k->err = err;
			k->off = err < 0 ? 1 : 2;
			/* ahead: wait for the true clock to pass what was given */
			while ((int32_t) (t - want) < 0)
				t = UCLK_TRUE_US();
		}
#endif
		(void) err;
		k->floor = want;
	}
#ifndef UCLK_FAULT_NOFLOOR
	if (k->recip && (int32_t) (k->floor - t) > (int32_t) UCLK_SLACK) {
		k->recip = 0;
		k->err = (int32_t) (t - k->floor);
		k->off = 1;
		while ((int32_t) (t - k->floor) < 0)
			t = UCLK_TRUE_US();
	}
#endif
	/* never less than what was given before, when the two are close */
	if ((uint32_t) (k->floor - t) - 1u < UCLK_SLACK)
		t = k->floor;
	if (k->recip) {
		k->gen = g | 1;
		UCLK_BARRIER();
		k->cyc = c;
		k->us = t;
		UCLK_BARRIER();
		k->gen = (g | 1) + 1;
	} else {
		k->gen = g | 1;         /* off: every read comes here */
	}
	UCLK_UNLOCK(s);
	return t;
}
#endif

/* FreeRTOS's idle hook on the clock's core, before each wait for an
   interrupt: the pair is stale after it, in case the counter stops in the
   wait after all.  What the pair gives now is kept as the floor for the
   next sync. */
static inline void uclock_idle(struct uclock *k)
{
#ifndef UCLK_FAULT_NOIDLE
	uint32_t g = k->gen;
	if (!(g & 1) && k->recip) {
		uint32_t dc = UCLK_CYCLES() - k->cyc;
		k->floor = k->us + uclock_scale(k, dc < UCLK_SPAN ? dc : UCLK_SPAN - 1);
		k->waits++;
	}
	k->gen = g | 1;
#else
	(void) k;
#endif
}

/* FreeRTOS's tick hook on the clock's core, every tick: a pair whose span
   is over is marked stale, with the most it could have given as the floor */
static inline void uclock_tick(struct uclock *k)
{
#ifndef UCLK_FAULT_NOTICK
	uint32_t g = k->gen;
	if (!(g & 1) && k->recip && UCLK_CYCLES() - k->cyc >= UCLK_SPAN) {
		k->floor = k->us + uclock_scale(k, UCLK_SPAN - 1);
		k->gen = g | 1;
	}
#else
	(void) k;
#endif
}

static inline uint32_t uclock_get(struct uclock *k)
{
	uint32_t t;
	if (uclock_fast(k, &t))
		return t;
	return uclock_sync(k);
}

#ifdef ESP_PLATFORM
extern struct uclock uclock_c1;         /* core 1's (uclock.c) */
void uclock_start(void);                /* on core 1, before the PC runs */
void uclock_say(void);                  /* the line when it has gone off */
void uclock_totals(uint32_t run_s);     /* the line at the run's end */
#endif

#endif
