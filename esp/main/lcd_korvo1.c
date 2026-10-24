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
 *
 * v4.4: double buffering (lcd.h).  The panel is started again with two
 * frame buffers, the first one from the emulator's pool, the second where
 * the driver's own one was; a picture goes to the panel by the driver's
 * draw_bitmap() with a pointer into the back buffer, which makes the DMA
 * take that buffer at the end of the frame it is sending, and the driver's
 * frame-buffer-complete event (the DMA's link switch) says when it has.
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cache.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#include "common.h"
#include "console.h"
#include "lcd.h"

static const char *TAG = "lcd";

#define LCD_PCLK_HZ 18000000
#define FB_BYTES ((size_t) LCD_WIDTH * LCD_HEIGHT * 2)

static uint16_t *s_fb;          /* the front buffer: the console's, the panel's */
static bool s_invalidate;
static struct lcd_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

/* v4.4: double buffering */
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_back;        /* the second buffer; NULL: none */
static bool s_double;           /* pictures drawn into s_back and swapped */
static SemaphoreHandle_t s_switched;
static uint8_t s_rows[LCD_HEIGHT];      /* rows the last picture changed */
static bool s_rows_any, s_copy_all;
static uint32_t s_swaps, s_unreported;  /* v4.5: since the start, never reset */

/* the panel's settings, with one frame buffer the driver allocates, or
   with fb0 and fb1 the two given */
static void panel_config(esp_lcd_rgb_panel_config_t *cfg, void *fb0, void *fb1)
{
	const esp_lcd_rgb_panel_config_t base = {
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
	*cfg = base;
	if (fb0 && fb1) {
		cfg->num_fbs = 2;
		cfg->user_fbs[0] = fb0;
		cfg->user_fbs[1] = fb1;
	}
}

/* the panel started; its first frame buffer in *fb */
static esp_lcd_panel_handle_t panel_start(const esp_lcd_rgb_panel_config_t *cfg, void **fb)
{
	esp_lcd_panel_handle_t panel = NULL;
	esp_err_t err = esp_lcd_new_rgb_panel(cfg, &panel);
	if (err == ESP_OK)
		err = esp_lcd_panel_init(panel);
	*fb = NULL;
	if (err == ESP_OK)
		err = esp_lcd_rgb_panel_get_frame_buffer(panel, 1, fb);
	if (err != ESP_OK || !*fb) {
		ESP_LOGE(TAG, "the panel did not start (%s)", esp_err_to_name(err));
		if (panel)
			esp_lcd_panel_del(panel);
		return NULL;
	}
	return panel;
}

void *lcd_init(void)
{
	esp_lcd_rgb_panel_config_t cfg;
	panel_config(&cfg, NULL, NULL);
	void *fb_ptr = NULL;
	esp_lcd_panel_handle_t panel = panel_start(&cfg, &fb_ptr);
	if (!panel) {
		/* only the UART can say so: there is no panel */
		return NULL;
	}

	globals.panel = panel;
	globals.panel_fb = fb_ptr;
	s_panel = panel;
	s_fb = fb_ptr;
	return fb_ptr;
}

void lcd_set_invalidate(bool on)
{
	s_invalidate = on;
}

/* rows y to y + h - 1 of buf from the cache to PSRAM */
static void flush_rows(uint16_t *buf, int y, int h)
{
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
	esp_cache_msync(buf + y * LCD_WIDTH, (size_t) h * LCD_WIDTH * 2, flags);
	uint32_t dt = esp_cpu_get_cycle_count() - t0;

	portENTER_CRITICAL_SAFE(&s_stats_lock);
	s_stats.flushes++;
	s_stats.rows += h;
	s_stats.cycles += dt;
	if (dt > s_stats.cycles_max)
		s_stats.cycles_max = dt;
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
}

void lcd_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	if (!s_fb)
		return;
	flush_rows(s_fb, y, h);
}

void lcd_get_stats(struct lcd_stats *st, bool reset)
{
	portENTER_CRITICAL_SAFE(&s_stats_lock);
	*st = s_stats;
	if (reset)
		s_stats = (struct lcd_stats) { 0 };
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
}

/* ==== v4.4: double buffering ==== */

/* the DMA has taken the buffer it was told to (the driver's link switch
   event, from its interrupt) */
static IRAM_ATTR bool on_switched(esp_lcd_panel_handle_t panel,
				  const esp_lcd_rgb_panel_event_data_t *edata, void *ctx)
{
	(void) panel; (void) edata; (void) ctx;
	BaseType_t woken = pdFALSE;
	if (s_switched)
		xSemaphoreGiveFromISR(s_switched, &woken);
	return woken == pdTRUE;
}

/* the crash screen draws into the front buffer (console_fb()) and, with
   double buffering, into this one too: a crash may come between the swap
   asked for and the swap made (crash.c) */
void *lcd_crash_second_fb(void)
{
	return s_back;
}

static bool double_start(void *fb)
{
	if (!s_panel || !s_fb || !fb || s_back)
		return false;
	if (!s_switched)
		s_switched = xSemaphoreCreateBinary();
	if (!s_switched)
		return false;
	/* the console's picture into the new front buffer, all of it in PSRAM */
	memcpy(fb, s_fb, FB_BYTES);
	esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

	/* the old panel and its frame buffer go; the second buffer takes the
	   room that one leaves */
	esp_lcd_panel_del(s_panel);
	s_panel = NULL;
	globals.panel = NULL;
	uint16_t *fb1 = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
	esp_lcd_rgb_panel_config_t cfg;
	void *fb_ptr = NULL;
	esp_lcd_panel_handle_t panel = NULL;
	if (fb1) {
		memcpy(fb1, fb, FB_BYTES);
		esp_cache_msync(fb1, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
		panel_config(&cfg, fb, fb1);
		panel = panel_start(&cfg, &fb_ptr);
		if (panel) {
			const esp_lcd_rgb_panel_event_callbacks_t cbs = {
				.on_frame_buf_complete = on_switched,
			};
			if (esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL) != ESP_OK) {
				esp_lcd_panel_del(panel);
				panel = NULL;
			}
		}
	}
	if (!panel) {
		/* back as it was: one buffer the driver allocates, the console's
		   picture copied into it */
		ESP_LOGE(TAG, "double buffering did not start (%s)", fb1 ? "the panel" : "no memory");
		free(fb1);
		panel_config(&cfg, NULL, NULL);
		panel = panel_start(&cfg, &fb_ptr);
		if (!panel) {
			/* no panel at all now: the console stops drawing (its
			   buffer was the driver's, freed with the panel) and
			   says what follows on the UART only (v4.5, the review
			   of v4.4, F6) */
			s_fb = NULL;
			globals.panel_fb = NULL;
			console_set_fb(NULL);
			return false;
		}
		memcpy(fb_ptr, fb, FB_BYTES);
		esp_cache_msync(fb_ptr, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
		s_panel = panel;
		s_fb = fb_ptr;
		globals.panel = panel;
		globals.panel_fb = fb_ptr;
		console_set_fb(fb_ptr);
		return false;
	}
	s_panel = panel;
	s_fb = fb;
	s_back = fb1;
	s_double = true;
	s_copy_all = false;
	s_rows_any = false;
	memset(s_rows, 0, sizeof(s_rows));
	globals.panel = panel;
	globals.panel_fb = fb;
	console_set_fb(fb);
	return true;
}

/* with the display lock held: no console line drawn into a frame buffer
   while it goes away */
bool lcd_double_start(void *fb)
{
	display_lock();
	bool ok = double_start(fb);
	display_unlock();
	return ok;
}

bool lcd_double_made(void)
{
	return s_back != NULL;
}

bool lcd_double_on(void)
{
	return s_double;
}

void *lcd_vga_fb(void)
{
	return s_double ? s_back : s_fb;
}

void *lcd_double_set(bool on)
{
	if (!s_back || on == s_double)
		return lcd_vga_fb();
	if (on) {
		/* the VGA code has drawn into the front buffer: the back one
		   is made the same before it draws there */
		uint32_t t0 = esp_cpu_get_cycle_count();
		memcpy(s_back, s_fb, FB_BYTES);
		esp_cache_msync(s_back, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
		portENTER_CRITICAL_SAFE(&s_stats_lock);
		s_stats.copied_rows += LCD_HEIGHT;
		s_stats.copy_cycles += esp_cpu_get_cycle_count() - t0;
		portEXIT_CRITICAL_SAFE(&s_stats_lock);
		memset(s_rows, 0, sizeof(s_rows));
		s_rows_any = false;
		s_copy_all = false;
	}
	/* off: both hold the last picture (the copy after each swap), so the
	   VGA code goes on in the front buffer */
	s_double = on;
	return lcd_vga_fb();
}

void lcd_vga_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	if (!s_double) {
		lcd_flush(x, y, w, h);
		return;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > LCD_HEIGHT)
		h = LCD_HEIGHT - y;
	if (h <= 0)
		return;
	flush_rows(s_back, y, h);
	memset(s_rows + y, 1, h);
	s_rows_any = true;
}

void lcd_vga_copy_all(void)
{
	s_copy_all = true;
}

void *lcd_vga_present(void)
{
	if (!s_double || (!s_rows_any && !s_copy_all))
		return lcd_vga_fb();
	/* the panel takes the back buffer from its next frame on: the driver
	   points every buffer's DMA list at it and asks for the switch event */
	xSemaphoreTake(s_switched, 0);
	uint32_t t0 = esp_cpu_get_cycle_count();
	esp_lcd_panel_draw_bitmap(s_panel, 0, 0, 1, 1, s_back);
	/* a frame is 28 ms at 35 a second; the DMA may send the old buffer
	   once more before it switches */
	bool ok = xSemaphoreTake(s_switched, pdMS_TO_TICKS(100)) == pdTRUE;
	uint32_t t1 = esp_cpu_get_cycle_count();
	uint16_t *front = s_back, *back = s_fb;
	s_fb = front;
	s_back = back;
	globals.panel_fb = front;
	console_set_fb(front);
	/* the rows of this picture into the new back buffer, so that both hold
	   it (the next picture is drawn over it), and into PSRAM, where the
	   panel reads them when this buffer is shown */
	int copied = 0;
	for (int y = 0; y < LCD_HEIGHT; y++) {
		if (!s_copy_all && !s_rows[y])
			continue;
		int y1 = y + 1;
		while (y1 < LCD_HEIGHT && (s_copy_all || s_rows[y1]))
			y1++;
		memcpy(back + y * LCD_WIDTH, front + y * LCD_WIDTH, (size_t) (y1 - y) * LCD_WIDTH * 2);
		esp_cache_msync(back + y * LCD_WIDTH, (size_t) (y1 - y) * LCD_WIDTH * 2,
				ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
		copied += y1 - y;
		y = y1;
	}
	memset(s_rows, 0, sizeof(s_rows));
	s_rows_any = false;
	s_copy_all = false;
	uint32_t t2 = esp_cpu_get_cycle_count();
	portENTER_CRITICAL_SAFE(&s_stats_lock);
	s_stats.flips++;
	s_stats.copied_rows += copied;
	s_stats.wait_cycles += t1 - t0;
	s_stats.copy_cycles += t2 - t1;
	s_swaps++;
	if (!ok) {
		s_stats.flip_timeouts++;
		s_unreported++;
	}
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
	return back;
}

void lcd_double_totals(uint32_t *swaps, uint32_t *unreported)
{
	portENTER_CRITICAL_SAFE(&s_stats_lock);
	*swaps = s_swaps;
	*unreported = s_unreported;
	portEXIT_CRITICAL_SAFE(&s_stats_lock);
}

#endif /* USE_LCD_RGB_KORVO1 */
