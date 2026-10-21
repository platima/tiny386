#ifdef USE_LCD_RGB_KORVO1
/*
 * ESP32-S31-Korvo-1: 800x480 RGB565 panel on the LCD_CAM RGB interface.
 *
 * Timings and pins are copied from the esp32-s31-linux loader
 * (bootloader/main/display.c at be5b725), where they run the panel at
 * 18 MHz, about 35 Hz.  GPIO33 and GPIO34 are LCD data pins and also the
 * USB Serial/JTAG pins, so the console must stay on UART0 with no secondary
 * console (sdkconfig.korvo1).
 *
 * The driver runs the panel in stream mode: its DMA reads the whole frame
 * buffer from PSRAM over and over with no CPU help, so the picture stays
 * even when both cores have stopped (the crash screen relies on that).
 * PSRAM is behind ESP-IDF's write-back cache, so lcd_flush() writes back
 * the rows that were drawn.  Everything that is not about this panel (the
 * drawing loop, the console) is elsewhere; see lcd.h.
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cache.h"
#include "esp_cpu.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "common.h"
#include "lcd.h"

static const char *TAG = "lcd";

#define LCD_PCLK_HZ 18000000

static uint16_t *s_fb;
static bool s_invalidate;
static struct lcd_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

void *lcd_init(void)
{
	const esp_lcd_rgb_panel_config_t panel_cfg = {
		.clk_src = LCD_CLK_SRC_PLL160M,
		.timings = {
			.pclk_hz = LCD_PCLK_HZ,
			.h_res = LCD_WIDTH,
			.v_res = LCD_HEIGHT,
			.hsync_pulse_width = 40,
			.hsync_back_porch = 40,
			.hsync_front_porch = 48,
			.vsync_pulse_width = 23,
			.vsync_back_porch = 32,
			.vsync_front_porch = 13,
			.flags.pclk_active_neg = true,
		},
		.data_width = 16,
		.in_color_format = LCD_COLOR_FMT_RGB565,
		.out_color_format = LCD_COLOR_FMT_RGB565,
		.num_fbs = 1,
		.dma_burst_size = 64,
		.hsync_gpio_num = 44,
		.vsync_gpio_num = 45,
		.de_gpio_num = 43,
		.pclk_gpio_num = 40,
		.disp_gpio_num = 38,
		.data_gpio_nums = {
			8, 9, 10, 11, 12, 13, 14, 15,
			16, 17, 18, 19, 33, 34, 35, 36,
		},
		.flags = {
			.fb_in_psram = true,
		},
	};

	esp_lcd_panel_handle_t panel = NULL;
	esp_err_t err = esp_lcd_new_rgb_panel(&panel_cfg, &panel);
	if (err == ESP_OK)
		err = esp_lcd_panel_init(panel);
	void *fb_ptr = NULL;
	if (err == ESP_OK)
		err = esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb_ptr);
	if (err != ESP_OK || !fb_ptr) {
		/* only the UART can say so: there is no panel */
		ESP_LOGE(TAG, "the panel did not start (%s)", esp_err_to_name(err));
		return NULL;
	}

	globals.panel = panel;
	globals.panel_fb = fb_ptr;
	s_fb = fb_ptr;
	return fb_ptr;
}

void lcd_set_invalidate(bool on)
{
	s_invalidate = on;
}

void lcd_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	if (!s_fb)
		return;
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > LCD_HEIGHT)
		h = LCD_HEIGHT - y;
	if (h <= 0)
		return;

	int flags = ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED;
	if (s_invalidate)
		flags |= ESP_CACHE_MSYNC_FLAG_INVALIDATE;
	uint32_t t0 = esp_cpu_get_cycle_count();
	esp_cache_msync(s_fb + y * LCD_WIDTH, (size_t) h * LCD_WIDTH * 2, flags);
	uint32_t dt = esp_cpu_get_cycle_count() - t0;

	portENTER_CRITICAL_SAFE(&s_stats_lock);
	s_stats.flushes++;
	s_stats.rows += h;
	s_stats.cycles += dt;
	if (dt > s_stats.cycles_max)
		s_stats.cycles_max = dt;
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
}

void lcd_get_stats(struct lcd_stats *st, bool reset)
{
	portENTER_CRITICAL_SAFE(&s_stats_lock);
	*st = s_stats;
	if (reset)
		s_stats = (struct lcd_stats) { 0 };
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
}

#endif /* USE_LCD_RGB_KORVO1 */
