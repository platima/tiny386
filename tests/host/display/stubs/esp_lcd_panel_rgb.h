/* Host stand-in for ESP-IDF's esp_lcd_panel_rgb.h (tests/host/display):
   the fields and calls lcd_korvo1.c uses, as ESP-IDF names them. */
#ifndef HOST_DISPLAY_ESP_LCD_PANEL_RGB_H
#define HOST_DISPLAY_ESP_LCD_PANEL_RGB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#define ESP_RGB_LCD_PANEL_MAX_FB_NUM 3

typedef enum { LCD_CLK_SRC_PLL160M = 1 } lcd_clock_source_t;
typedef enum { LCD_COLOR_FMT_RGB565 = 1 } lcd_color_format_t;

typedef struct {
	uint32_t pclk_hz;
	uint32_t h_res, v_res;
	uint32_t hsync_pulse_width, hsync_back_porch, hsync_front_porch;
	uint32_t vsync_pulse_width, vsync_back_porch, vsync_front_porch;
	struct {
		uint32_t pclk_active_neg: 1;
	} flags;
} esp_lcd_rgb_timing_t;

typedef struct {
	lcd_clock_source_t clk_src;
	esp_lcd_rgb_timing_t timings;
	size_t data_width;
	lcd_color_format_t in_color_format, out_color_format;
	size_t num_fbs;
	size_t dma_burst_size;
	int hsync_gpio_num, vsync_gpio_num, de_gpio_num, pclk_gpio_num, disp_gpio_num;
	int data_gpio_nums[16];
	void *user_fbs[ESP_RGB_LCD_PANEL_MAX_FB_NUM];
	struct {
		uint32_t fb_in_psram: 1;
	} flags;
} esp_lcd_rgb_panel_config_t;

typedef struct {
	int unused;
} esp_lcd_rgb_panel_event_data_t;

typedef bool (*esp_lcd_rgb_panel_frame_buf_complete_cb_t)(esp_lcd_panel_handle_t panel,
		const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx);

typedef struct {
	esp_lcd_rgb_panel_frame_buf_complete_cb_t on_frame_buf_complete;
} esp_lcd_rgb_panel_event_callbacks_t;

esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *config,
				esp_lcd_panel_handle_t *ret_panel);
esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t panel, uint32_t fb_num,
					     void **fb0, ...);
esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
		const esp_lcd_rgb_panel_event_callbacks_t *callbacks, void *user_ctx);
#endif
