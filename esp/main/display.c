#ifdef LCD_ZERO_COPY
/*
 * The drawing loop for boards whose VGA code draws straight into the
 * panel's frame buffer (lcd.h): a picture every LCD_FRAME_MS on core 0,
 * while the boot console does not have the panel.
 *
 * With VGA_RETRACE_REALTIME the guest's retrace bits follow the clock
 * (vga.c), so how often this loop runs no longer matters to the guest's
 * timing: it only sets how many pictures a second are drawn.
 */
#include <inttypes.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "../../pc.h"
#include "../../vga.h"
#include "common.h"
#include "console.h"
#include "display.h"
#include "lcd.h"
#include "speed.h"

#ifndef LCD_FRAME_MS
#define LCD_FRAME_MS 30
#endif
/* how often the drawing's cost goes to the UART */
#ifndef DISPLAY_STATS_S
#define DISPLAY_STATS_S 10
#endif

/* The least stack each task has had free since it started, in bytes (the
   ESP-IDF FreeRTOS counts stacks in bytes), to the UART with the drawing's
   cost.  Under the display lock, which the PC's own task needs to switch
   off and end itself (console_hold()), so that it is never looked at once
   it is gone.  v5.1: then internal RAM, in bytes too: the least free since
   the board started, and the largest free block now.  ESP-IDF's least is
   the sum of each of its heaps' own least (HP SRAM in two pieces, and LP
   SRAM), so it is never above the true least. */
static void stacks_line(void)
{
	static const char *const names[] = {
		"i386_main", "vga_task", "usb_events", "USB HID Host", "usb_task",
		"kbd_repeat", "ch9350", "hdmi", "i2s_task", "status_led", "uart_log",
		"IDLE0", "IDLE1", NULL
	};
	char line[352];
	int o = snprintf(line, sizeof(line), "stacks, least free since start (bytes):");

	display_lock();
	if (!console_held()) {
		for (int i = 0; names[i] && o < (int) sizeof(line); i++) {
			TaskHandle_t t = xTaskGetHandle(names[i]);
			if (t)
				o += snprintf(line + o, sizeof(line) - o, " %s %u", names[i],
					      (unsigned) uxTaskGetStackHighWaterMark(t));
		}
	}
	display_unlock();
	if (o < (int) sizeof(line))
		snprintf(line + o, sizeof(line) - o, "; internal RAM least free since start %u, "
			 "largest free block %u",
			 (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
	console_uart_printf("%s", line);
}

/* ==== v4.4: pictures at the guest's quiet moments (vga.h, display.h) ==== */

static TaskHandle_t s_vga_task;
/* a picture is due and waits for a quiet step: set by vga_task, cleared by
   whichever core ends the wait */
static uint8_t s_pace_want;
static int s_pace_wait_ms = DISPLAY_PACE_WAIT_MS;
/* vga_task's own counts: pictures drawn at a quiet step, and at the end of
   the wait; and the time waited in all, in ticks */
static uint32_t s_pace_quiet, s_pace_late, s_pace_ticks;

void display_pace_set_wait(int ms)
{
	s_pace_wait_ms = ms > 0 ? ms : 1;
}

int display_pace_wait(void)
{
	return s_pace_wait_ms;
}

void display_pace_counts(uint32_t *quiet, uint32_t *late)
{
	*quiet = __atomic_load_n(&s_pace_quiet, __ATOMIC_RELAXED);
	*late = __atomic_load_n(&s_pace_late, __ATOMIC_RELAXED);
}

void display_pace_step(uint32_t insns, bool halted)
{
	/* every step, so that the last store is noted whether or not a
	   picture waits */
	if (vga_pace_after_step(insns, halted) &&
	    __atomic_load_n(&s_pace_want, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&s_pace_want, 0, __ATOMIC_RELAXED);
		xTaskNotifyGive(s_vga_task);
	}
}

/* Before a picture, with pacing on: wait until the emulator's core finds
   the guest quiet, at most the wait.  A notice left from a wait that ran
   out just as the core found it quiet is taken first. */
static void pace_wait(void)
{
	/* the ticks, rounded up (pdMS_TO_TICKS() rounds down) */
	TickType_t wait = ((TickType_t) s_pace_wait_ms * configTICK_RATE_HZ + 999) / 1000;
	if (!wait)
		wait = 1;
	ulTaskNotifyTake(pdTRUE, 0);
	__atomic_store_n(&s_pace_want, 1, __ATOMIC_RELEASE);
	TickType_t t0 = xTaskGetTickCount();
	uint32_t got = ulTaskNotifyTake(pdTRUE, wait);
	__atomic_store_n(&s_pace_want, 0, __ATOMIC_RELEASE);
	s_pace_ticks += xTaskGetTickCount() - t0;
	if (got)
		__atomic_store_n(&s_pace_quiet, s_pace_quiet + 1, __ATOMIC_RELAXED);
	else
		__atomic_store_n(&s_pace_late, s_pace_late + 1, __ATOMIC_RELAXED);
}

void vga_task(void *arg)
{
	(void) arg;
	s_vga_task = xTaskGetCurrentTaskHandle();
	/* the PC exists */
	xEventGroupWaitBits(global_event_group, BIT0, pdFALSE, pdFALSE,
			    portMAX_DELAY);

	const TickType_t period = pdMS_TO_TICKS(LCD_FRAME_MS) ? pdMS_TO_TICKS(LCD_FRAME_MS) : 1;
	TickType_t last = xTaskGetTickCount();
	int64_t stats_t0 = esp_timer_get_time();
	uint32_t pictures = 0;
	/* pictures in the last whole second, for the speed screen */
	int64_t pps_t0 = stats_t0;
	uint32_t pps_count = 0, pps = 0;
	struct lcd_stats st;
	lcd_get_stats(&st, true);

	for (;;) {
		/* v4.4: [esp] pace_quiet, Ctrl+Alt+F10 */
		if (__atomic_load_n(&vga_pace_quiet, __ATOMIC_RELAXED) && !console_owns_display())
			pace_wait();
		display_lock();
		if (!console_owns_display()) {
			pc_vga_step(globals.pc);
#ifdef LCD_DOUBLE_BUFFER
			/* v4.4: a picture that changed rows goes to the panel at
			   its next frame, and is copied into the new back buffer,
			   where the next one is drawn.  v4.6: only once both
			   buffers are made.  Else the VGA code keeps the buffer it
			   was built with, its own when there is no panel, where
			   lcd_vga_present() is NULL: v4.5 handed that over, and
			   the next text cell drawn was a load access fault at
			   0xFA50 (tests/host/display).  v4.5 lost its panel when
			   it could not start it again for double buffering; v4.6
			   never starts it again, but a panel that does not start
			   at power-on still needs this */
			if (lcd_double_made())
				vga_set_fb(((PC *) globals.pc)->vga, lcd_vga_present());
#endif
			pictures++;
			pps_count++;
		}
		display_unlock();

		int64_t now = esp_timer_get_time();
		if (now - pps_t0 >= 1000000) {
			pps = pps_count;
			pps_count = 0;
			pps_t0 = now;
		}
		/* Ctrl+Alt+F12 and the end of BENCH: the console over the
		   picture, or the picture back */
		speed_screen_poll(globals.pc, pps);
		if (now - stats_t0 >= DISPLAY_STATS_S * 1000000LL) {
			lcd_get_stats(&st, true);
			uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
			console_uart_printf("display: %" PRIu32 " pictures in %d s, %" PRIu32
				" write-backs of %" PRIu32 " rows, %" PRIu32
				" us each on average, %" PRIu32 " us the longest",
				pictures, DISPLAY_STATS_S, st.flushes, st.rows,
				st.flushes ? (uint32_t) (st.cycles / st.flushes / mhz) : 0,
				st.cycles_max / mhz);
#ifdef LCD_DOUBLE_BUFFER
			if (lcd_double_made())
				console_uart_printf("display: double buffering %s: %" PRIu32 " swaps, %" PRIu32
					" rows copied, %" PRIu32 " us copying and %" PRIu32 " us waiting for the "
					"panel on average, %" PRIu32 " swaps the panel did not report",
					lcd_double_on() ? "on" : "off", st.flips, st.copied_rows,
					st.flips ? (uint32_t) (st.copy_cycles / st.flips / mhz) : 0,
					st.flips ? (uint32_t) (st.wait_cycles / st.flips / mhz) : 0,
					st.flip_timeouts);
#endif
			if (vga_pace_quiet)
				console_uart_printf("display: paced (pace_quiet %" PRIu32 ", wait %d ms): "
					"%" PRIu32 " pictures at a quiet moment, %" PRIu32 " late, %" PRIu32
					" ms waited, since the PC started", vga_pace_quiet, s_pace_wait_ms,
					s_pace_quiet, s_pace_late, (uint32_t) (s_pace_ticks * portTICK_PERIOD_MS));
			stacks_line();
			pictures = 0;
			stats_t0 = now;
		}

		if (xTaskDelayUntil(&last, period) == pdFALSE) {
			/* behind time: start again from now, and let the idle
			   task run */
			vTaskDelay(1);
			last = xTaskGetTickCount();
		}
	}
}

#endif /* LCD_ZERO_COPY */
