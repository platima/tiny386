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
 * The frame buffer is in PSRAM, which ESP-IDF runs write-back, and the LCD
 * DMA reads PSRAM behind the cache.  The VGA code draws straight into the
 * frame buffer, so after each refresh lcd_flush() hands the drawn lines back
 * to the RGB driver.  Because the pointer is inside the frame buffer, the
 * driver copies nothing and only writes the cache lines back.
 */

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "common.h"

static const char *TAG = "lcd";

#define LCD_PCLK_HZ 18000000

void pc_vga_step(void *o);

void lcd_draw(int x_start, int y_start, int x_end, int y_end, void *src)
{
	(void)x_start; (void)y_start; (void)x_end; (void)y_end; (void)src;
}

void lcd_flush(int x, int y, int w, int h)
{
	uint16_t *fb = globals.panel_fb;

	(void)x; (void)w;
	if (!globals.panel || h <= 0)
		return;
	ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(globals.panel,
						  0, y, LCD_WIDTH, y + h,
						  fb + y * LCD_WIDTH));
}

void vga_task(void *arg)
{
	int core_id = esp_cpu_get_core_id();
	fprintf(stderr, "vga runs on core %d\n", core_id);

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
	ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &panel));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

	void *fb_ptr = NULL;
	ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb_ptr));
	ESP_LOGI(TAG, "Panel FB @ %p (%d bytes)", fb_ptr, LCD_WIDTH * LCD_HEIGHT * 2);

	globals.panel = panel;
	globals.panel_fb = fb_ptr;

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

#endif /* USE_LCD_RGB_KORVO1 */
