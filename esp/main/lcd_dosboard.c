#ifdef USE_LCD_RGB_DOSBOARD
/*
 * The DOS board: 800x600 RGB565 at 60 Hz on the LCD_CAM RGB interface, into
 * the MS7210 HDMI transmitter.  VESA DMT timing: 40.000 MHz (PLL_F160M / 4),
 * H 800 active, 40 front porch, 128 sync, 88 back porch; V 600, 1, 4, 23;
 * both syncs positive (FIRMWARE-PLAN 5.2, values in board_dosboard.h).
 *
 * As on the Korvo-1 (lcd_korvo1.c) the frame buffer is in PSRAM, which
 * ESP-IDF runs write-back, the VGA code draws straight into it, and
 * lcd_flush() only writes the drawn lines back from the cache.
 *
 * With bounce buffers (LCD_BOUNCE_BUFFER_PX or [dosboard] lcd_bounce_px) the
 * driver copies the frame buffer into two internal RAM buffers from an
 * interrupt on core 0, reading through the cache, so nothing needs writing
 * back and lcd_flush() does nothing.
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "common.h"
#include "dosboard.h"
#include "ch9350.h"

static const char *TAG = "lcd";
static int bounce;

void pc_vga_step(void *o);

void lcd_draw(int x_start, int y_start, int x_end, int y_end, void *src)
{
	(void)x_start; (void)y_start; (void)x_end; (void)y_end; (void)src;
}

void lcd_flush(int x, int y, int w, int h)
{
	uint16_t *fb = globals.panel_fb;

	(void)x; (void)w;
	if (!globals.panel || h <= 0 || bounce)
		return;
	ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(globals.panel,
						  0, y, LCD_WIDTH, y + h,
						  fb + y * LCD_WIDTH));
}

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
	ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
	bounce = dosboard_cfg.bounce_px > 0;
	ESP_LOGI(TAG, "800x600 at %d Hz pixel clock, data on the %s edge, %s",
		 LCD_PCLK_HZ, dosboard_cfg.pclk_neg ? "falling" : "rising",
		 bounce ? "bounce buffers" : "no bounce buffers");
	return panel;
}

void vga_task(void *arg)
{
	int core_id = esp_cpu_get_core_id();
	fprintf(stderr, "vga runs on core %d\n", core_id);

	dosboard_load_config();
	esp_lcd_panel_handle_t panel = panel_start();

	void *fb_ptr = NULL;
	ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb_ptr));
	ESP_LOGI(TAG, "Panel FB @ %p (%d bytes)", fb_ptr, LCD_WIDTH * LCD_HEIGHT * 2);

	globals.panel = panel;
	globals.panel_fb = fb_ptr;

	/* colour bars first, so they are there the moment the MS7210 starts */
	if (dosboard_cfg.test_card_ms > 0) {
		dosboard_test_card(fb_ptr, LCD_WIDTH, LCD_HEIGHT);
		lcd_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);
	}
	/* the MS7210 meters PCLK and locks its PLL to it, so PCLK runs first */
	dosboard_video_start();
	if (dosboard_cfg.test_card_ms > 0) {
		vTaskDelay(pdMS_TO_TICKS(dosboard_cfg.test_card_ms));
		/* the guest picture is centred and does not cover the edges */
		memset(fb_ptr, 0, LCD_WIDTH * LCD_HEIGHT * 2);
		lcd_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);
	}

	/* keyboard and mouse; the task waits for the emulator */
	ch9350_start(dosboard_cfg.ch9350_state, dosboard_cfg.kbd_zero_guard_ms);

	/* Tell i386_task that the panel and its frame buffer are ready */
	xEventGroupSetBits(global_event_group, BIT1);
	xEventGroupWaitBits(global_event_group,
			    BIT0,
			    pdFALSE,
			    pdFALSE,
			    portMAX_DELAY);

	while (1) {
		pc_vga_step(globals.pc);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

#endif /* USE_LCD_RGB_DOSBOARD */
