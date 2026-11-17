#ifdef USE_LCD_QSPI_MOSAICO
/*
 * ESP-Mosaico: the 2.16 inch 480x480 AMOLED, a CO5300 on QSPI (SPI2), as
 * lcd.h has it for a board whose VGA code and console draw into a frame
 * buffer of their own (LCD_ZERO_COPY).
 *
 * The frame is LCD_WIDTH x LCD_HEIGHT (640x480) RGB565 in PSRAM, the size
 * DOS and Windows draw at; the panel keeps its own picture, so only rows
 * that changed are sent (lcd_flush()), and of each piece of CHUNK panel
 * rows only the rectangle of 32-pixel blocks that differ from what the
 * panel shows (panel_diff.c).  That rectangle goes from a buffer of
 * internal RAM, with the bytes of each pixel the other way round (the
 * CO5300 takes them high byte first), by DMA while the next piece is
 * made.  The panel shows either
 *   - 480 of the frame's 640 columns 1:1 (a window at s_px: always for a VBE
 *     picture 480 wide or less, which vga.c centres, so Windows at 480x480
 *     fills the panel; and for a wider one in FOLLOW, the window following
 *     the mouse pointer), or
 *   - the whole frame at three quarters (FIT), 480x360 in rows 60 to 419,
 *     each output pixel the area average of the source pixels under it:
 *     the boot console, text modes and 640x480 in FIT.
 * The window's blocks are compared where they lie in the frame, so only
 * the rectangle that differs is copied; FIT's pieces are made in full,
 * then compared.
 * Commands as Espressif's ESP-Mosaico BSP sends them (onboard/display.c,
 * the vendor sequence), at its 40 MHz and drive strength 2 unless [esp]
 * lcd_mhz and lcd_drive (or Ctrl+Alt+F5 and F4) say otherwise; the
 * windows are even numbers of rows and of columns, which keeps to the
 * CO5300's rounding.
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
#include "panel_diff.h"

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
static struct panel_diff s_pd;         /* what the panel shows, by blocks */
static uint32_t s_sent_px;             /* pixels sent, for the counts */
static int s_hz = MOSAICO_LCD_HZ;      /* asked for */
static int s_khz;                       /* what the bus makes of it */
static int s_drive = MOSAICO_LCD_DRIVE;
static const int s_pins[] = { MOSAICO_LCD_SCL, MOSAICO_LCD_D0, MOSAICO_LCD_D1,
			      MOSAICO_LCD_D2, MOSAICO_LCD_D3 };
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

/* rows ya to yb - 1 of a piece of n rows made even numbers, as the
   CO5300's windows want (x0 and x1 are multiples of 32 already) */
static inline void even_rows(int *ya, int *yb, int n)
{
	*ya &= ~1;
	*yb = (*yb + 1) & ~1;
	if (*yb > n)
		*yb = n;
}

/* Panel rows y0 to y1 - 1 (even, in 0 to PH) as view and px say: of each
   piece only the rectangle of its blocks that differ from what the panel
   shows is sent; all of it with force (another view).  The window's
   blocks are compared where they lie in the frame, and only that
   rectangle is copied to a buffer; FIT's pieces are made in a buffer in
   full, then compared and packed. */
static void send_rows(int y0, int y1, int view, int px, bool force)
{
	for (int y = y0; y < y1; y += CHUNK) {
		int n = y1 - y < CHUNK ? y1 - y : CHUNK;
		int ya, yb, x0, x1;
		uint16_t *b;
		size_t npx;
		if (view) {
			const uint16_t *src = s_fb + (size_t) y * FW + px;
			if (!panel_diff_rows(&s_pd, src, FW, y, n, force, &ya, &yb, &x0, &x1))
				continue;       /* the panel has these rows already */
			even_rows(&ya, &yb, n);
			xSemaphoreTake(s_free, portMAX_DELAY);
			b = s_buf[s_next];
			npx = panel_diff_copy_swapped(b, src, FW, ya, yb, x0, x1);
		} else {
			xSemaphoreTake(s_free, portMAX_DELAY);
			b = s_buf[s_next];
			fill_fit(b, y, n);
			if (!panel_diff_rows(&s_pd, b, PW, y, n, force, &ya, &yb, &x0, &x1)) {
				/* the panel has these rows already: the buffer stays free */
				xSemaphoreGive(s_free);
				continue;
			}
			even_rows(&ya, &yb, n);
			npx = panel_diff_pack(b, PW, ya, yb, x0, x1);
		}
		s_next ^= 1;
		/* the window (the first command waits for the piece before to go) */
		uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, (x1 - 1) >> 8, (x1 - 1) & 0xFF };
		cmd(0x2A, ca, 4);
		int r0 = y + ya, r1 = y + yb - 1;
		uint8_t ra[4] = { r0 >> 8, r0 & 0xFF, r1 >> 8, r1 & 0xFF };
		cmd(0x2B, ra, 4);
		s_sent_px += (uint32_t) npx;
		if (esp_lcd_panel_io_tx_color(s_io, PIX(0x2C), b, npx * 2) != ESP_OK) {
			/* not sent: the buffer is free again, and these rows go in
			   full the next time they come */
			xSemaphoreGive(s_free);
			panel_diff_forget_rows(&s_pd, y, n);
		}
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
	uint32_t t0 = esp_cpu_get_cycle_count(), px0 = s_sent_px;
	send_rows(0, PH, view, px, true);
	wait_idle();
	s_shown = view;
	s_shown_px = px;
	uint32_t dt = esp_cpu_get_cycle_count() - t0;
	portENTER_CRITICAL(&s_stats_lock);
	s_stats.flushes++;
	/* rows: the pixels sent, in rows of 480 */
	s_stats.rows += (s_sent_px - px0 + PW - 1) / PW;
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
	uint32_t t0 = esp_cpu_get_cycle_count(), px0 = s_sent_px;
	send_rows(y0, y1, view, px, false);
	wait_idle();
	uint32_t dt = esp_cpu_get_cycle_count() - t0;
	portENTER_CRITICAL(&s_stats_lock);
	s_stats.flushes++;
	s_stats.rows += (s_sent_px - px0 + PW - 1) / PW;
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
   following the pointer, a new clock), at most every 15 ms (a whole panel
   is 23 ms at 40 MHz, 12 at 80), under the display lock */
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
		vTaskDelay(pdMS_TO_TICKS(15));
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

static esp_err_t make_io(int hz)
{
	const esp_lcd_panel_io_spi_config_t io_cfg = {
		.cs_gpio_num = MOSAICO_LCD_CS,
		.dc_gpio_num = -1,
		.spi_mode = 0,
		.pclk_hz = hz,
		.trans_queue_depth = 4,
		.on_color_trans_done = on_done,
		.lcd_cmd_bits = 32,
		.lcd_param_bits = 8,
		.flags.quad_mode = true,
	};
	return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io_cfg, &s_io);
}

/* The clock the SPI master makes of hz, in kHz: asked of a device added for
   the purpose, as esp_lcd adds its own but without a CS line, and taken
   away again.  The bus is idle meanwhile. */
static int actual_khz(int hz)
{
	const spi_device_interface_config_t dev = {
		.clock_speed_hz = hz,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 1,
		.flags = SPI_DEVICE_HALFDUPLEX,
	};
	spi_device_handle_t h;
	int khz = hz / 1000;
	if (spi_bus_add_device(SPI2_HOST, &dev, &h) == ESP_OK) {
		if (spi_device_get_actual_freq(h, &khz) != ESP_OK)
			khz = hz / 1000;
		spi_bus_remove_device(h);
	}
	return khz;
}

/* the whole picture again at the next chance (the repaint task), so that
   what the panel shows was all sent with the new setting; under the
   display lock */
static void resend_all(void)
{
	s_shown = -1;
	ask_repaint();
}

int lcd_mosaico_set_mhz(int mhz)
{
	if (mhz < 10 || mhz > 80)
		return -1;
	int hz = mhz * 1000000;
	if (!s_io || hz == s_hz)
		return s_io ? 0 : -1;
	/* the panel keeps its picture: only the bus's device changes */
	display_lock();
	wait_idle();
	esp_lcd_panel_io_del(s_io);
	s_io = NULL;
	int ret = 0;
	if (make_io(hz) == ESP_OK) {
		s_hz = hz;
	} else {
		ret = -1;
		if (make_io(s_hz) != ESP_OK)
			s_io = NULL;
	}
	if (s_io) {
		s_khz = actual_khz(s_hz);
		resend_all();
	}
	display_unlock();
	return ret;
}

int lcd_mosaico_mhz(void)
{
	return s_hz / 1000000;
}

int lcd_mosaico_khz(void)
{
	return s_khz;
}

static void say_setting(void)
{
	int tenths = (s_khz + 50) / 100;
	console_printf(CON_INFO, "Panel: clock %d.%d MHz, drive strength %d. Ctrl+Alt+F5 the next "
		       "clock, Ctrl+Alt+F4 the next strength; [esp] lcd_mhz = %d and lcd_drive = %d "
		       "keep them.", tenths / 10, tenths % 10, s_drive, lcd_mosaico_mhz(), s_drive);
}

/* the clocks the bus makes exactly, 480 MHz divided */
static const int s_clocks[] = { 40, 48, 53, 60, 68, 80 };

void lcd_mosaico_next_mhz(void)
{
	int now = lcd_mosaico_mhz(), next = s_clocks[0];
	for (size_t i = 0; i < sizeof(s_clocks) / sizeof(s_clocks[0]); i++) {
		if (s_clocks[i] > now) {
			next = s_clocks[i];
			break;
		}
	}
	if (lcd_mosaico_set_mhz(next) != 0)
		console_printf(CON_WARN, "The panel's clock would not go to %d MHz.", next);
	say_setting();
}

int lcd_mosaico_set_drive(int level)
{
	if (level < 0 || level > 3)
		return -1;
	s_drive = level;
	if (!s_io)
		return 0;       /* lcd_init() sets it */
	display_lock();
	wait_idle();
	for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); i++)
		gpio_set_drive_capability(s_pins[i], (gpio_drive_cap_t) level);
	resend_all();
	display_unlock();
	return 0;
}

int lcd_mosaico_drive(void)
{
	return s_drive;
}

void lcd_mosaico_next_drive(void)
{
	lcd_mosaico_set_drive((s_drive + 1) % 4);
	say_setting();
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
	/* what the panel shows, a hash a block: 28,800 bytes, in PSRAM */
	uint32_t *hashes = heap_caps_malloc((size_t) PH * (PW / PD_BLOCK) * 4, MALLOC_CAP_SPIRAM);
	uint8_t *known = heap_caps_malloc(PH, MALLOC_CAP_SPIRAM);
	if (!s_fb || !s_buf[0] || !s_buf[1] || !s_free || !hashes || !known) {
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
	/* the QSPI lines' drive strength (the BSP's, 2, unless lcd_drive came
	   first) */
	for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); i++)
		gpio_set_drive_capability(s_pins[i], (gpio_drive_cap_t) s_drive);

	panel_diff_init(&s_pd, PW, PH, hashes, known);
	err = make_io(s_hz);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "panel IO: %s", esp_err_to_name(err));
		s_io = NULL;
		return NULL;
	}
	s_khz = actual_khz(s_hz);
	for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
		cmd(s_init[i].cmd, s_init[i].len ? s_init[i].data : NULL, s_init[i].len);
		if (s_init[i].delay_ms)
			vTaskDelay(pdMS_TO_TICKS(s_init[i].delay_ms));
	}
	/* black, the frame being zeros */
	send_rows(0, PH, 1, 0, true);
	wait_idle();
	s_shown = 1;
	s_shown_px = 0;

	globals.panel_fb = s_fb;
	if (xTaskCreatePinnedToCore(repaint_task, "lcd_view", 2560, NULL, 3,
				    &s_repaint_task, 0) != pdPASS)
		s_repaint_task = NULL;
	ESP_LOGI(TAG, "CO5300 480x480 at %d kHz, frame %dx%d", s_khz, FW, FH);
	return s_fb;
}

#endif /* USE_LCD_QSPI_MOSAICO */
