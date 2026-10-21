#ifdef USE_LCD_RGB_DOSBOARD
/*
 * The DOS board: 800x600 RGB565 at 60 Hz on the LCD_CAM RGB interface, into
 * the MS7210 HDMI transmitter.  VESA DMT timing: 40.000 MHz (PLL_F160M / 4),
 * H 800 active, 40 front porch, 128 sync, 88 back porch; V 600, 1, 4, 23;
 * both syncs positive (FIRMWARE-PLAN 5.2, values in board_dosboard.h).
 *
 * This file gives what lcd.h asks of a board, as lcd_korvo1.c does for the
 * Korvo-1: the frame buffer is in PSRAM, which ESP-IDF runs write-back, the
 * VGA code and the boot console draw straight into it, and lcd_flush()
 * writes the drawn rows back from the cache.  The drawing loop is
 * display.c's, shared with the Korvo-1.
 *
 * With bounce buffers (LCD_BOUNCE_BUFFER_PX or [dosboard] lcd_bounce_px) the
 * driver copies the frame buffer into two internal RAM buffers from an
 * interrupt on core 0, reading through the cache, so nothing needs writing
 * back and lcd_flush() does nothing.  The crash screen then stays only as
 * long as that interrupt runs.
 *
 * lcd_init() is where the HDMI side starts too, because the MS7210 meters
 * PCLK and locks its PLL to it: the [dosboard] settings (dosboard.ini on
 * the card), the panel, the test card, the MS7210 (dosboard.c), then the
 * test card's hold.  Only then does app_main() start the boot console.
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cache.h"
#include "esp_cpu.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "common.h"
#include "lcd.h"
#include "dosboard.h"

static const char *TAG = "lcd";

static uint16_t *s_fb;
static bool s_bounce;
static bool s_invalidate;
static struct lcd_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_lcd_panel_handle_t panel_start(void)
{
	static const int data_pins[16] = { LCD_DATA_GPIOS };
	esp_lcd_rgb_panel_config_t cfg = {
		.clk_src = LCD_CLK_SRC_PLL160M,
		.timings = {
			.pclk_hz = LCD_PCLK_HZ,
			.h_res = LCD_WIDTH,
			.v_res = LCD_HEIGHT,
			.hsync_pulse_width = LCD_HSYNC_WIDTH,
			.hsync_back_porch = LCD_HSYNC_BACK,
			.hsync_front_porch = LCD_HSYNC_FRONT,
			.vsync_pulse_width = LCD_VSYNC_WIDTH,
			.vsync_back_porch = LCD_VSYNC_BACK,
			.vsync_front_porch = LCD_VSYNC_FRONT,
			/* idle low, so the sync pulses are high (VESA: both positive) */
			.flags.hsync_idle_low = 1,
			.flags.vsync_idle_low = 1,
			.flags.pclk_active_neg = dosboard_cfg.pclk_neg,
		},
		.data_width = 16,
		.in_color_format = LCD_COLOR_FMT_RGB565,
		.out_color_format = LCD_COLOR_FMT_RGB565,
		.num_fbs = 1,
		.bounce_buffer_size_px = dosboard_cfg.bounce_px,
		.dma_burst_size = 64,
		.hsync_gpio_num = LCD_HSYNC_GPIO,
		.vsync_gpio_num = LCD_VSYNC_GPIO,
		.de_gpio_num = LCD_DE_GPIO,
		.pclk_gpio_num = LCD_PCLK_GPIO,
		.disp_gpio_num = -1,          /* GPIO38 is USB_VBUS_EN on this board */
		.flags = {
			.fb_in_psram = true,
		},
	};
	esp_lcd_panel_handle_t panel = NULL;

	for (int i = 0; i < 16; i++)
		cfg.data_gpio_nums[i] = data_pins[i];
	esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
	if (err == ESP_OK) {
		err = esp_lcd_panel_init(panel);
		if (err != ESP_OK) {
			esp_lcd_panel_del(panel);
			panel = NULL;
		}
	}
	if (err != ESP_OK) {
		/* only the UART can say so: there is no picture */
		ESP_LOGE(TAG, "the RGB output did not start (%s)", esp_err_to_name(err));
		return NULL;
	}
	s_bounce = dosboard_cfg.bounce_px > 0;
	dosboard_note("dosboard: 800x600 at %d Hz pixel clock, data on the %s edge, %s.",
		      LCD_PCLK_HZ, dosboard_cfg.pclk_neg ? "falling" : "rising",
		      s_bounce ? "bounce buffers" : "no bounce buffers");
	return panel;
}

void *lcd_init(void)
{
	/* [dosboard] from the card: the PCLK edge and the bounce buffers are
	   needed before the panel starts */
	dosboard_load_config();

	esp_lcd_panel_handle_t panel = panel_start();
	if (!panel && dosboard_cfg.bounce_px > 0) {
		/* the bounce buffers did not fit, most likely: once more without,
		   so that a setting on the card cannot leave the board blind */
		dosboard_note("dosboard: the RGB output did not start with lcd_bounce_px %d; "
			      "trying again without bounce buffers.", dosboard_cfg.bounce_px);
		dosboard_cfg.bounce_px = 0;
		panel = panel_start();
	}
	void *fb_ptr = NULL;
	if (!panel ||
	    esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb_ptr) != ESP_OK ||
	    !fb_ptr) {
		ESP_LOGE(TAG, "no frame buffer: no picture, and no HDMI set-up");
		return NULL;
	}
	globals.panel = panel;
	globals.panel_fb = fb_ptr;
	s_fb = fb_ptr;

	/* colour bars first, so that they are there the moment the MS7210
	   starts sending */
	if (dosboard_cfg.test_card_ms > 0) {
		dosboard_test_card(s_fb, LCD_WIDTH, LCD_HEIGHT);
		lcd_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);
	}
	/* the MS7210 meters PCLK and locks its PLL to it, so PCLK runs first */
	dosboard_video_start();
	if (dosboard_cfg.test_card_ms > 0) {
		vTaskDelay(pdMS_TO_TICKS(dosboard_cfg.test_card_ms) + 1);
		/* the boot console draws all but the last rows; black under it */
		memset(s_fb, 0, LCD_WIDTH * LCD_HEIGHT * 2);
		lcd_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);
	}
	return fb_ptr;
}

void lcd_set_invalidate(bool on)
{
	s_invalidate = on;
}

void lcd_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	if (!s_fb || s_bounce)
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

#endif /* USE_LCD_RGB_DOSBOARD */
