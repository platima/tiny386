#ifdef USE_LCD_QSPI_MOSAICO
/*
 * ESP-Mosaico: the 2.16 inch 480x480 AMOLED, a CO5300 on QSPI (SPI2), as
 * lcd.h has it for a board whose VGA code and console draw into a frame
 * buffer of their own (LCD_ZERO_COPY).
 *
 * The frame is LCD_WIDTH x LCD_HEIGHT (640x480) RGB565 in PSRAM, the size
 * DOS and Windows draw at; the panel keeps its own picture, so only rows
 * that changed are sent (lcd_flush()).  Each piece of CHUNK panel rows is
 * made in a buffer of internal RAM, with the bytes of each pixel the other
 * way round (the CO5300 takes them high byte first), while the piece before
 * it goes out by DMA.  The panel shows either
 *   - 480 of the frame's 640 columns 1:1 (a window at s_px: always for a VBE
 *     picture 480 wide or less, which vga.c centres, so Windows at 480x480
 *     fills the panel; and for a wider one in FOLLOW, the window following
 *     the mouse pointer), or
 *   - the whole frame at three quarters (FIT), 480x360 in rows 60 to 419,
 *     each output pixel the area average of the source pixels under it:
 *     the boot console, text modes and 640x480 in FIT.
 * Commands as Espressif's ESP-Mosaico BSP sends them (onboard/display.c,
 * the vendor sequence; 40 MHz; whole rows, so the windows are 0 to 479 by
 * any number of even rows, which keeps to the CO5300's rounding).
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

#include "../../pc.h"
#include "../../vga.h"
#include "common.h"
#include "console.h"
#include "lcd.h"
#include "lcd_mosaico.h"

static const char *TAG = "lcd";

#define PW MOSAICO_PANEL_W
#define PH MOSAICO_PANEL_H
#define FW LCD_WIDTH
#define FH LCD_HEIGHT
/* panel rows a transfer: even, and 2 x 5,760 bytes of internal RAM */
#define CHUNK 6
#define CHUNK_BYTES (PW * 2 * CHUNK)
/* FIT: the frame at 3/4, rows FIT_TOP to FIT_TOP + FIT_H - 1 */
#define FIT_H (FH * 3 / 4)
#define FIT_TOP ((PH - FIT_H) / 2)
/* the window's place for a picture 480 wide or less (vga.c centres it) */
#define PX_CENTRE ((FW - PW) / 2)

/* QSPI: a register is 02h with the command in the address phase, one wire;
   pixels are 32h with the command, then the data on four wires */
#define CMD(c) ((int) ((0x02u << 24) | ((uint32_t) (c) << 8)))
#define PIX(c) ((int) ((0x32u << 24) | ((uint32_t) (c) << 8)))

static uint16_t *s_fb;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_buf[2];
static int s_next;
static SemaphoreHandle_t s_free;        /* buffers not with the DMA */
static uint16_t s_tmp[FW];              /* FIT: a row blended from two */
static struct lcd_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

/* what the panel shows: -1 not drawn yet, 0 FIT, 1 a window at s_px */
static int s_shown = -1;
static int s_shown_px;
static volatile uint8_t s_view = LCD_VIEW_FIT;  /* for wide pictures */
static volatile int s_follow_px = PX_CENTRE;
static int s_ptr_x = FW / 2;            /* the pointer, as far as we know */
static TaskHandle_t s_repaint_task;

struct init_cmd {
	uint8_t cmd, len;
	uint8_t data[4];
	uint16_t delay_ms;
};
/* the BSP's vendor sequence, with TE on (35h 00) */
static const struct init_cmd s_init[] = {
	{ 0x11, 0, { 0 }, 600 },                /* sleep out */
	{ 0xFE, 1, { 0x20 }, 0 },
	{ 0x19, 1, { 0x10 }, 0 },
	{ 0x1C, 1, { 0xA0 }, 0 },
	{ 0xFE, 1, { 0x00 }, 0 },
	{ 0xC4, 1, { 0x80 }, 0 },
	{ 0x3A, 1, { 0x55 }, 0 },               /* RGB565 */
	{ 0x35, 1, { 0x00 }, 0 },               /* TE on */
	{ 0x53, 1, { 0x20 }, 0 },
	{ 0x51, 1, { 0xFF }, 0 },               /* brightness */
	{ 0x63, 1, { 0xFF }, 0 },
	{ 0x2A, 4, { 0x00, 0x00, 0x01, 0xDF }, 0 },
	{ 0x2B, 4, { 0x00, 0x00, 0x01, 0xDF }, 0 },
	{ 0x29, 0, { 0 }, 600 },                /* display on */
};

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e,
		    void *ctx)
{
	(void) io; (void) e; (void) ctx;
	BaseType_t woken = pdFALSE;
	xSemaphoreGiveFromISR(s_free, &woken);
	return woken == pdTRUE;
}

static void cmd(uint8_t c, const uint8_t *d, size_t n)
{
	esp_lcd_panel_io_tx_param(s_io, CMD(c), d, n);
}

/* both buffers back from the DMA */
static void wait_idle(void)
{
	xSemaphoreTake(s_free, portMAX_DELAY);
	xSemaphoreTake(s_free, portMAX_DELAY);
	xSemaphoreGive(s_free);
	xSemaphoreGive(s_free);
}

/* ==== making the rows ==== */

static inline uint16_t swap16(uint32_t v)
{
	return (uint16_t) ((v >> 8) | (v << 8));
}

/* panel rows y to y + n - 1 of the window at px */
static void fill_window(uint16_t *dst, int y, int n, int px)
{
	for (int r = 0; r < n; r++, dst += PW) {
		const uint32_t *s = (const uint32_t *) (s_fb + (size_t) (y + r) * FW + px);
		uint32_t *d = (uint32_t *) dst;
		for (int i = 0; i < PW / 2; i++) {
			uint32_t w = s[i];
			d[i] = ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
		}
	}
}

/* p and q mixed wp to 4 - wp, RGB565 (each field times at most 4 stays
   clear of the next: R and B masked together, G alone) */
static inline uint32_t mix(uint32_t p, uint32_t q, uint32_t wp)
{
	uint32_t wq = 4 - wp;
	uint32_t rb = ((p & 0xF81Fu) * wp + (q & 0xF81Fu) * wq) >> 2;
	uint32_t g = ((p & 0x07E0u) * wp + (q & 0x07E0u) * wq) >> 2;
	return (rb & 0xF81Fu) | (g & 0x07E0u);
}

/* Output row oy (0 to FIT_H - 1) of the frame at 3/4: four source rows
   (or columns) a b c d make three, (3a + b)/4, (b + c)/2, (c + 3d)/4, the
   area of each output pixel over the source. */
static void fit_row(uint16_t *dst, int oy)
{
	static const uint8_t first[3] = { 0, 1, 2 }, weight[3] = { 3, 2, 1 };
	int k = oy % 3;
	const uint16_t *a = s_fb + (size_t) (oy / 3 * 4 + first[k]) * FW;
	const uint16_t *b = a + FW;
	for (int x = 0; x < FW; x++)
		s_tmp[x] = (uint16_t) mix(a[x], b[x], weight[k]);
	for (int x = 0, o = 0; x < FW; x += 4, o += 3) {
		uint32_t p0 = s_tmp[x], p1 = s_tmp[x + 1], p2 = s_tmp[x + 2], p3 = s_tmp[x + 3];
		dst[o] = swap16(mix(p0, p1, 3));
		dst[o + 1] = swap16(mix(p1, p2, 2));
		dst[o + 2] = swap16(mix(p2, p3, 1));
	}
}

static void fill_fit(uint16_t *dst, int y, int n)
{
	for (int r = 0; r < n; r++, dst += PW) {
		int oy = y + r - FIT_TOP;
		if (oy < 0 || oy >= FIT_H)
			memset(dst, 0, PW * 2);
		else
			fit_row(dst, oy);
	}
}

/* ==== sending ==== */

/* panel rows y0 to y1 - 1 (even, in 0 to PH) as view and px say */
static void send_rows(int y0, int y1, int view, int px)
{
	for (int y = y0; y < y1; y += CHUNK) {
		int n = y1 - y < CHUNK ? y1 - y : CHUNK;
		xSemaphoreTake(s_free, portMAX_DELAY);
		uint16_t *b = s_buf[s_next];
		s_next ^= 1;
		if (view)
			fill_window(b, y, n, px);
		else
			fill_fit(b, y, n);
		/* the rows' window (this waits for the piece before to go) */
		uint8_t ra[4] = { y >> 8, y & 0xFF, (y + n - 1) >> 8, (y + n - 1) & 0xFF };
		cmd(0x2B, ra, 4);
		if (esp_lcd_panel_io_tx_color(s_io, PIX(0x2C), b, (size_t) n * PW * 2) != ESP_OK)
			xSemaphoreGive(s_free);
	}
}

/* what the panel should show now: view 0 FIT or 1 a window at *px */
static int wanted(int *px)
{
	*px = 0;
	if (console_owns_display() || !globals.pc)
		return 0;
	struct vga_info vi;
	vga_get_info(((PC *) globals.pc)->vga, &vi);
	if (vi.kind == 3 && vi.width <= PW) {
		*px = PX_CENTRE;
		return 1;
	}
	if (s_view == LCD_VIEW_FOLLOW) {
		*px = s_follow_px;
		return 1;
	}
	return 0;
}

/* the whole panel again, as it should be now */
static void repaint(void)
{
	int px, view = wanted(&px);
	uint32_t t0 = esp_cpu_get_cycle_count();
	send_rows(0, PH, view, px);
	wait_idle();
	s_shown = view;
	s_shown_px = px;
	uint32_t dt = esp_cpu_get_cycle_count() - t0;
	portENTER_CRITICAL(&s_stats_lock);
	s_stats.flushes++;
	s_stats.rows += PH;
	s_stats.cycles += dt;
	if (dt > s_stats.cycles_max)
		s_stats.cycles_max = dt;
	portEXIT_CRITICAL(&s_stats_lock);
}

/* ==== lcd.h ==== */

void lcd_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	if (!s_io || h <= 0)
		return;
	int px, view = wanted(&px);
	if (view != s_shown || (view && px != s_shown_px)) {
		repaint();
		return;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > FH)
		h = FH - y;
	if (h <= 0)
		return;
	int y0, y1;
	if (view) {
		y0 = y & ~1;
		y1 = (y + h + 1) & ~1;
	} else {
		/* four source rows make three panel rows */
		y0 = (FIT_TOP + y / 4 * 3) & ~1;
		y1 = (FIT_TOP + (y + h + 3) / 4 * 3 + 1) & ~1;
		if (y1 > FIT_TOP + FIT_H)
			y1 = FIT_TOP + FIT_H;
	}
	uint32_t t0 = esp_cpu_get_cycle_count();
	send_rows(y0, y1, view, px);
	wait_idle();
	uint32_t dt = esp_cpu_get_cycle_count() - t0;
	portENTER_CRITICAL(&s_stats_lock);
	s_stats.flushes++;
	s_stats.rows += (uint32_t) (y1 - y0);
	s_stats.cycles += dt;
	if (dt > s_stats.cycles_max)
		s_stats.cycles_max = dt;
	portEXIT_CRITICAL(&s_stats_lock);
}

void lcd_set_invalidate(bool on)
{
	(void) on;      /* the frame is only read by the CPU here */
}

void lcd_get_stats(struct lcd_stats *st, bool reset)
{
	portENTER_CRITICAL(&s_stats_lock);
	*st = s_stats;
	if (reset)
		memset(&s_stats, 0, sizeof(s_stats));
	portEXIT_CRITICAL(&s_stats_lock);
}

/* ==== the view ==== */

/* repaints asked for from elsewhere (a key, the button, the window
   following the pointer), at most every 40 ms, under the display lock */
static void repaint_task(void *arg)
{
	(void) arg;
	for (;;) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		display_lock();
		int px, view = wanted(&px);
		if (view != s_shown || (view && px != s_shown_px))
			repaint();
		display_unlock();
		vTaskDelay(pdMS_TO_TICKS(40));
	}
}

static void ask_repaint(void)
{
	if (s_repaint_task)
		xTaskNotifyGive(s_repaint_task);
}

void lcd_mosaico_set_view(enum lcd_view v)
{
	s_view = v == LCD_VIEW_FOLLOW ? LCD_VIEW_FOLLOW : LCD_VIEW_FIT;
	ask_repaint();
}

enum lcd_view lcd_mosaico_view(void)
{
	return (enum lcd_view) s_view;
}

void lcd_mosaico_next_view(void)
{
	lcd_mosaico_set_view(s_view == LCD_VIEW_FIT ? LCD_VIEW_FOLLOW : LCD_VIEW_FIT);
}

/* The window keeps 48 pixels of room round the pointer.  Windows moves its
   pointer by the mouse's counts with some acceleration, so this is an
   estimate; pushing the pointer against a side of the screen puts the two
   in step again, since both stop there. */
#define FOLLOW_MARGIN 48
void lcd_mosaico_mouse(int dx, int dy)
{
	(void) dy;
	int x = s_ptr_x + dx;
	x = x < 0 ? 0 : x >= FW ? FW - 1 : x;
	s_ptr_x = x;
	int px = s_follow_px;
	if (x < px + FOLLOW_MARGIN)
		px = x - FOLLOW_MARGIN;
	else if (x > px + PW - 1 - FOLLOW_MARGIN)
		px = x - (PW - 1 - FOLLOW_MARGIN);
	px = px < 0 ? 0 : px > FW - PW ? FW - PW : px;
	px &= ~3;
	if (px != s_follow_px) {
		s_follow_px = px;
		if (s_view == LCD_VIEW_FOLLOW)
			ask_repaint();
	}
}

void lcd_mosaico_brightness(int level)
{
	if (!s_io)
		return;
	uint8_t v = level < 0 ? 0 : level > 255 ? 255 : (uint8_t) level;
	display_lock();
	cmd(0x51, &v, 1);
	display_unlock();
}

/* ==== start ==== */

void *lcd_init(void)
{
	/* the panel's supply (VCC_3V3_CTRL, active low) is on since
	   board_early_init() */
	gpio_config_t rst = {
		.pin_bit_mask = 1ULL << MOSAICO_LCD_RST,
		.mode = GPIO_MODE_OUTPUT,
	};
	gpio_config(&rst);
	gpio_set_level(MOSAICO_LCD_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(MOSAICO_LCD_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(MOSAICO_LCD_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(120));

	s_fb = heap_caps_aligned_calloc(64, 1, (size_t) FW * FH * 2, MALLOC_CAP_SPIRAM);
	s_buf[0] = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	s_buf[1] = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	s_free = xSemaphoreCreateCounting(2, 2);
	if (!s_fb || !s_buf[0] || !s_buf[1] || !s_free) {
		ESP_LOGE(TAG, "no memory for the frame or the panel's buffers");
		return NULL;
	}

	const spi_bus_config_t bus = {
		.sclk_io_num = MOSAICO_LCD_SCL,
		.data0_io_num = MOSAICO_LCD_D0,
		.data1_io_num = MOSAICO_LCD_D1,
		.data2_io_num = MOSAICO_LCD_D2,
		.data3_io_num = MOSAICO_LCD_D3,
		.max_transfer_sz = CHUNK_BYTES,
	};
	esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "SPI2: %s", esp_err_to_name(err));
		return NULL;
	}
	/* the BSP's drive strength for the QSPI lines (its default, 2) */
	const int pins[] = { MOSAICO_LCD_SCL, MOSAICO_LCD_D0, MOSAICO_LCD_D1,
			     MOSAICO_LCD_D2, MOSAICO_LCD_D3 };
	for (int i = 0; i < 5; i++)
		gpio_set_drive_capability(pins[i], GPIO_DRIVE_CAP_2);

	const esp_lcd_panel_io_spi_config_t io_cfg = {
		.cs_gpio_num = MOSAICO_LCD_CS,
		.dc_gpio_num = -1,
		.spi_mode = 0,
		.pclk_hz = MOSAICO_LCD_HZ,
		.trans_queue_depth = 4,
		.on_color_trans_done = on_done,
		.lcd_cmd_bits = 32,
		.lcd_param_bits = 8,
		.flags.quad_mode = true,
	};
	err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io_cfg, &s_io);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "panel IO: %s", esp_err_to_name(err));
		s_io = NULL;
		return NULL;
	}
	for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
		cmd(s_init[i].cmd, s_init[i].len ? s_init[i].data : NULL, s_init[i].len);
		if (s_init[i].delay_ms)
			vTaskDelay(pdMS_TO_TICKS(s_init[i].delay_ms));
	}
	/* columns 0 to 479 from now on; each piece sets its rows */
	const uint8_t ca[4] = { 0, 0, (PW - 1) >> 8, (PW - 1) & 0xFF };
	cmd(0x2A, ca, 4);
	/* black, the frame being zeros */
	send_rows(0, PH, 1, 0);
	wait_idle();
	s_shown = 1;
	s_shown_px = 0;

	globals.panel_fb = s_fb;
	if (xTaskCreatePinnedToCore(repaint_task, "lcd_view", 2560, NULL, 3,
				    &s_repaint_task, 0) != pdPASS)
		s_repaint_task = NULL;
	ESP_LOGI(TAG, "CO5300 480x480 at %d MHz, frame %dx%d", MOSAICO_LCD_HZ / 1000000, FW, FH);
	return s_fb;
}

#endif /* USE_LCD_QSPI_MOSAICO */
