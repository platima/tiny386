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

#include "../../pc.h"
#include "common.h"
#include "console.h"
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
   it is gone. */
static void stacks_line(void)
{
	static const char *const names[] = {
		"i386_main", "vga_task", "usb_events", "USB HID Host", "usb_task",
		"kbd_repeat", "ch9350", "hdmi", "i2s_task", "status_led", "uart_log",
		"IDLE0", "IDLE1", NULL
	};
	char line[288];
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
	console_uart_printf("%s", line);
}

void vga_task(void *arg)
{
	(void) arg;
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
		display_lock();
		if (!console_owns_display()) {
			pc_vga_step(globals.pc);
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
