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

#ifndef LCD_FRAME_MS
#define LCD_FRAME_MS 30
#endif
/* how often the drawing's cost goes to the UART */
#ifndef DISPLAY_STATS_S
#define DISPLAY_STATS_S 10
#endif

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
	struct lcd_stats st;
	lcd_get_stats(&st, true);

	for (;;) {
		display_lock();
		if (!console_owns_display()) {
			pc_vga_step(globals.pc);
			pictures++;
		}
		display_unlock();

		int64_t now = esp_timer_get_time();
		if (now - stats_t0 >= DISPLAY_STATS_S * 1000000LL) {
			lcd_get_stats(&st, true);
			uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
			console_uart_printf("display: %" PRIu32 " pictures in %d s, %" PRIu32
				" write-backs of %" PRIu32 " rows, %" PRIu32
				" us each on average, %" PRIu32 " us the longest",
				pictures, DISPLAY_STATS_S, st.flushes, st.rows,
				st.flushes ? (uint32_t) (st.cycles / st.flushes / mhz) : 0,
				st.cycles_max / mhz);
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
