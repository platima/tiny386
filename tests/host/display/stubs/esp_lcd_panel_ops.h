/* Host stand-in for ESP-IDF's esp_lcd_panel_ops.h (tests/host/display):
   the panel is the test's pretend RGB driver. */
#ifndef HOST_DISPLAY_ESP_LCD_PANEL_OPS_H
#define HOST_DISPLAY_ESP_LCD_PANEL_OPS_H
#include "esp_err.h"
typedef struct esp_lcd_panel_t esp_lcd_panel_t;
typedef esp_lcd_panel_t *esp_lcd_panel_handle_t;
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start,
				    int x_end, int y_end, const void *color_data);
#endif
