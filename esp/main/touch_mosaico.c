#ifdef USE_LCD_QSPI_MOSAICO
/*
 * ESP-Mosaico: the CST9220 touch panel (I2C 0x5A on the board's bus) as the
 * guest's mouse, the way a laptop's touchpad is one: Windows only takes
 * movement from a PS/2 mouse, and a fingertip covers about 100 of the
 * panel's pixels, so pointing straight at things would not be finer.
 *
 *   - a finger moving: the pointer moves with it (one and a half times);
 *   - a tap (down and up within 200 ms, hardly moving): a left click;
 *   - a tap, then down again within 300 ms and moving: the left button held
 *     while the finger moves (dragging), let go with the finger;
 *   - a tap with two fingers: a right click.
 * Read every 15 ms on core 0 through Espressif's esp_lcd_touch_cst9220.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9220.h"

#include "console.h"
#include "kbd_input.h"
#include "touch_mosaico.h"

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp;

#define POLL_MS 15
#define TAP_US 200000
#define AGAIN_US 300000
#define TAP_MOVE 10             /* pixels: more is not a tap */
#define GAIN_NUM 3              /* movement times 3/2 */
#define GAIN_DEN 2

static void touch_task(void *arg)
{
	(void) arg;
	bool down = false, drag = false;
	int lx = 0, ly = 0, moved = 0, maxf = 0;
	int ax = 0, ay = 0;             /* the fractions left over */
	int64_t t_down = 0, t_tap = -1000000;
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(POLL_MS));
		esp_lcd_touch_point_data_t pt[2];
		uint8_t n = 0;
		if (esp_lcd_touch_read_data(s_tp) != ESP_OK)
			continue;
		bool got = esp_lcd_touch_get_data(s_tp, pt, &n, 2) == ESP_OK;
		int64_t now = esp_timer_get_time();
		if (got && n > 0) {
			if (!down) {
				down = true;
				t_down = now;
				lx = pt[0].x;
				ly = pt[0].y;
				moved = 0;
				maxf = n;
				ax = ay = 0;
				/* down again soon after a tap: dragging */
				if (now - t_tap < AGAIN_US) {
					drag = true;
					kbd_input_mouse(0, 0, 0, 1);
				}
				continue;
			}
			if (n > maxf)
				maxf = n;
			int dx = (int) pt[0].x - lx, dy = (int) pt[0].y - ly;
			lx = pt[0].x;
			ly = pt[0].y;
			moved += abs(dx) + abs(dy);
			if (n > 1)
				continue;       /* two fingers: no movement */
			ax += dx * GAIN_NUM;
			ay += dy * GAIN_NUM;
			int mx = ax / GAIN_DEN, my = ay / GAIN_DEN;
			ax -= mx * GAIN_DEN;
			ay -= my * GAIN_DEN;
			if (mx || my)
				kbd_input_mouse(mx, my, 0, drag ? 1 : 0);
		} else if (down) {
			down = false;
			if (drag) {
				drag = false;
				kbd_input_mouse(0, 0, 0, 0);
				t_tap = -1000000;
			} else if (now - t_down < TAP_US && moved < TAP_MOVE) {
				int b = maxf > 1 ? 2 : 1;
				kbd_input_mouse(0, 0, 0, b);
				kbd_input_mouse(0, 0, 0, 0);
				t_tap = b == 1 ? now : -1000000;
			}
		}
	}
}

void touch_mosaico_start(void *i2c_bus)
{
	esp_lcd_panel_io_handle_t io = NULL;
	esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
	esp_err_t err = esp_lcd_new_panel_io_i2c((i2c_master_bus_handle_t) i2c_bus, &io_cfg, &io);
	if (err == ESP_OK) {
		const esp_lcd_touch_config_t cfg = {
			.x_max = MOSAICO_PANEL_W,
			.y_max = MOSAICO_PANEL_H,
			.rst_gpio_num = -1,
			.int_gpio_num = -1,
		};
		err = esp_lcd_touch_new_i2c_cst9220(io, &cfg, &s_tp);
	}
	if (err != ESP_OK) {
		console_printf(CON_WARN, "Touch: the CST9220 did not answer (%s): no touch "
			       "mouse.", esp_err_to_name(err));
		return;
	}
	if (xTaskCreatePinnedToCore(touch_task, "touch", 3072, NULL, 3, NULL, 0) != pdPASS) {
		console_puts(CON_WARN, "Touch: no memory for its task: no touch mouse.");
		return;
	}
	ESP_LOGI(TAG, "CST9220 as the mouse");
	console_puts(CON_INFO, "Touch: the panel is a touchpad for the mouse (tap to "
		     "click, two fingers to right-click, tap then drag to hold).");
}

#endif /* USE_LCD_QSPI_MOSAICO */
