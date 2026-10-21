/*
 * The DOS board's own start-up: the board check, the pins that are new on
 * this board, the MS7210 on its I2C bus, the [dosboard] settings, and the
 * hot plug task.  Pins: board_dosboard.h (DESIGN-BRIEF.md section 4).
 *
 * Start-up order:
 *   app_main     board_early_init(): probe the MS7210 (chip ID A2 20 0A at
 *                I2C 0x59); on any other board stop here, before a single
 *                pin that is new on the DOS board has been driven.
 *   lcd_init     dosboard_load_config() (the card, dosboard.ini), the RGB
 *                output (PCLK starts), the test card, dosboard_video_start()
 *                (MS7210 sections B to E, which need PCLK).
 *   app_main     the boot console, then board_console_ready(): the lines
 *                noted so far go onto it.
 *   app_main     the CH9350L (ch9350_setup()) where the Korvo-1 starts its
 *                USB host: for the boot menu, or once the ini is read.
 */
#ifdef USE_LCD_RGB_DOSBOARD

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "../../ini.h"
#include "common.h"
#include "console.h"
#include "dosboard.h"
#include "ms7210.h"
#include "storage.h"

/* ==== lines from before the boot console ==== */

#define NOTES_MAX 24
#define NOTE_LEN 112
static char (*s_notes)[NOTE_LEN];       /* in PSRAM, freed once shown */
static int s_nnotes;
static bool s_console_up;

/* a line that says something went wrong, to be shown in yellow */
static bool sounds_bad(const char *s)
{
	static const char *const words[] = {
		"NOT ", "not stable", "not attached", "no answer", "no EDID",
		"wrong", " reads 0x", "I2C errors", "does not divide", "not a ",
		"could not", NULL
	};
	for (int i = 0; words[i]; i++)
		if (strstr(s, words[i]))
			return true;
	return false;
}

void dosboard_note(const char *fmt, ...)
{
	char line[NOTE_LEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (s_console_up) {
		console_puts(sounds_bad(line) ? CON_WARN : CON_INFO, line);
		return;
	}
	fprintf(stderr, "%s\n", line);
	if (!s_notes)
		s_notes = heap_caps_calloc(NOTES_MAX, NOTE_LEN, MALLOC_CAP_SPIRAM);
	if (s_notes && s_nnotes < NOTES_MAX)
		snprintf(s_notes[s_nnotes++], NOTE_LEN, "%s", line);
}

void board_console_ready(void)
{
	/* console_puts() also writes to the UART, which had these already;
	   a second copy there marks where the console began */
	for (int i = 0; i < s_nnotes; i++)
		console_puts(sounds_bad(s_notes[i]) ? CON_WARN : CON_INFO, s_notes[i]);
	free(s_notes);
	s_notes = NULL;
	s_nnotes = 0;
	s_console_up = true;
}

dosboard_cfg_t dosboard_cfg = {
	.pclk_phase = 0,
	.pclk_neg = LCD_PCLK_ACTIVE_NEG,
	.hdmi_mode = MS7210_MODE_AUTO,
	.audio = 1,
	.bounce_px = LCD_BOUNCE_BUFFER_PX,
	.test_card_ms = 2000,
	.ch9350_state = 2,
	.kbd_zero_guard_ms = 0,
};

/* ==== MS7210 on I2C0 (GPIO0 SDA, GPIO1 SCL) ==== */

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t dev_fast, dev_slow, dev_edid;
static uint32_t bus_hz = HDMI_I2C_HZ;
static ms7210_t hdmi;

static i2c_master_dev_handle_t dev_for(uint8_t addr7)
{
	if (addr7 == MS7210_EDID_ADDR)
		return dev_edid;
	return bus_hz < HDMI_I2C_HZ ? dev_slow : dev_fast;
}

static int io_write(void *ctx, uint8_t addr7, const uint8_t *buf, int len)
{
	return i2c_master_transmit(dev_for(addr7), buf, len, 100) == ESP_OK ? 0 : -1;
}

static int io_write_read(void *ctx, uint8_t addr7, const uint8_t *w, int wlen,
			 uint8_t *r, int rlen)
{
	/* 128 bytes at 20 kHz take about 60 ms */
	int timeout = addr7 == MS7210_EDID_ADDR ? 500 : 100;

	return i2c_master_transmit_receive(dev_for(addr7), w, wlen, r, rlen, timeout) ==
	       ESP_OK ? 0 : -1;
}

static void io_speed(void *ctx, uint32_t hz)
{
	bus_hz = hz;
}

static void io_rstn(void *ctx, int level)
{
	gpio_set_level(HDMI_RSTN_GPIO, level);
}

static void io_delay(void *ctx, uint32_t us)
{
	/* never shorter than asked, whatever the tick rate */
	if (us >= 10000)
		vTaskDelay(pdMS_TO_TICKS(us / 1000) + 1);
	else
		esp_rom_delay_us(us);
}

static void io_log(void *ctx, const char *line)
{
	dosboard_note("%s", line);
}

static const ms7210_io_t hdmi_io = {
	.ctx = NULL,
	.i2c_write = io_write,
	.i2c_write_read = io_write_read,
	.i2c_speed = io_speed,
	.rstn = io_rstn,
	.delay_us = io_delay,
	.log = io_log,
	.row = NULL,
};

static int i2c_open(void)
{
	const i2c_master_bus_config_t bc = {
		.i2c_port = HDMI_I2C_PORT,
		.sda_io_num = HDMI_I2C_SDA,
		.scl_io_num = HDMI_I2C_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,   /* R207, R208 4.7 k are the real ones */
	};
	i2c_device_config_t dc = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = HDMI_I2C_ADDR,
		.scl_speed_hz = HDMI_I2C_HZ,
	};

	if (i2c_new_master_bus(&bc, &i2c_bus) != ESP_OK)
		return -1;
	if (i2c_master_bus_add_device(i2c_bus, &dc, &dev_fast) != ESP_OK)
		return -1;
	dc.scl_speed_hz = HDMI_DDC_HZ;
	if (i2c_master_bus_add_device(i2c_bus, &dc, &dev_slow) != ESP_OK)
		return -1;
	dc.device_address = MS7210_EDID_ADDR;
	if (i2c_master_bus_add_device(i2c_bus, &dc, &dev_edid) != ESP_OK)
		return -1;
	return 0;
}

static void i2c_close(void)
{
	if (dev_fast)
		i2c_master_bus_rm_device(dev_fast);
	if (dev_slow)
		i2c_master_bus_rm_device(dev_slow);
	if (dev_edid)
		i2c_master_bus_rm_device(dev_edid);
	if (i2c_bus)
		i2c_del_master_bus(i2c_bus);
	dev_fast = dev_slow = dev_edid = NULL;
	i2c_bus = NULL;
}

/* ==== board check ==== */

static void __attribute__((noreturn)) wrong_board(void)
{
	/* leave GPIO0 to 2 as they were found: on a Korvo-1 they are the
	   codec's I2C and MCLK */
	char seen[48];

	i2c_close();
	gpio_reset_pin(HDMI_RSTN_GPIO);
	if (hdmi.errors < 0)
		snprintf(seen, sizeof(seen), "the I2C bus could not be set up");
	else if (hdmi.errors)
		snprintf(seen, sizeof(seen), "nothing answered");
	else
		snprintf(seen, sizeof(seen), "chip ID read %02X %02X %02X, expected A2 20 0A",
			 hdmi.chip_id[0], hdmi.chip_id[1], hdmi.chip_id[2]);
	for (;;) {
		fprintf(stderr,
			"\n*** tiny386 for the DOS board: no MS7210 HDMI transmitter at I2C 0x%02X"
			" (%s).\n"
			"*** This image is only for the DOS board. On an ESP32-S31-Korvo-1 flash the"
			" Korvo-1 image.\n"
			"*** On a DOS board: check the MS7210's 1.2 V and 3.3 V, RSTN (GPIO2) and"
			" the I2C pull-ups.\n"
			"*** Halted; nothing else was started.\n",
			HDMI_I2C_ADDR, seen);
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}

void board_early_init(void)
{
	const gpio_config_t rstn = {
		.pin_bit_mask = 1ULL << HDMI_RSTN_GPIO,
		.mode = GPIO_MODE_OUTPUT,
	};
	const gpio_config_t outs = {
		.pin_bit_mask = (1ULL << LED_STATUS_GPIO) | (1ULL << USB_VBUS_EN_GPIO),
		.mode = GPIO_MODE_OUTPUT,
	};
	const gpio_config_t ins = {
		.pin_bit_mask = (1ULL << USER_KEY_GPIO) | (1ULL << SD_CD_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};

	fprintf(stderr, "dosboard: looking for the MS7210 at I2C 0x%02X\n", HDMI_I2C_ADDR);
	/* RSTN has R603 to +3V3; start high so that the first edge is the reset */
	gpio_set_level(HDMI_RSTN_GPIO, 1);
	gpio_config(&rstn);
	if (i2c_open() != 0) {
		hdmi.errors = -1;
		wrong_board();
	}
	if (ms7210_probe(&hdmi, &hdmi_io, NULL) != 0)
		wrong_board();

	/* the right board: the pins that are new on it */
	gpio_set_level(LED_STATUS_GPIO, 0);      /* lit while starting */
	gpio_set_level(USB_VBUS_EN_GPIO, 1);     /* USB-A ports on (R309 does so too) */
	gpio_config(&outs);
	gpio_config(&ins);
	dosboard_note("dosboard: MS7210 found at I2C 0x%02X; card detect says the card is %s.",
		      HDMI_I2C_ADDR, gpio_get_level(SD_CD_GPIO) ? "out" : "in");
}

/* ==== [dosboard] settings ==== */

static int parse_cfg(void *user, const char *section, const char *name, const char *value)
{
	dosboard_cfg_t *c = user;

	if (strcmp(section, "dosboard") != 0)
		return 1;
	if (strcmp(name, "pclk_phase") == 0)
		c->pclk_phase = atoi(value) & 7;
	else if (strcmp(name, "pclk_neg") == 0)
		c->pclk_neg = atoi(value) != 0;
	else if (strcmp(name, "hdmi_mode") == 0)
		c->hdmi_mode = strcmp(value, "hdmi") == 0 ? MS7210_MODE_HDMI :
			       strcmp(value, "dvi") == 0 ? MS7210_MODE_DVI : MS7210_MODE_AUTO;
	else if (strcmp(name, "hdmi_audio") == 0)
		c->audio = atoi(value) != 0;
	else if (strcmp(name, "lcd_bounce_px") == 0)
		c->bounce_px = atoi(value);
	else if (strcmp(name, "test_card_ms") == 0)
		c->test_card_ms = atoi(value);
	else if (strcmp(name, "ch9350_state") == 0)
		c->ch9350_state = atoi(value) == 0 ? 0 : 2;
	else if (strcmp(name, "kbd_zero_guard_ms") == 0)
		c->kbd_zero_guard_ms = atoi(value);
	else {
		dosboard_note("dosboard.ini: [dosboard] %s is not a setting tiny386 knows; "
			      "ignored.", name);
		return 1;
	}
	dosboard_note("  [dosboard] %s = %s", name, value);
	return 1;
}

void dosboard_load_config(void)
{
	/* beside the inis, but not one the boot menu offers (setup.c leaves
	   dosboard.ini out of its list) */
	static const char *files[] = {
		"/sdcard/tiny386/dosboard.ini",
		"/sdcard/dosboard.ini",
		NULL,
	};

	/* the card, now rather than in app_main's mount_storage(), which then
	   finds it mounted; without one, the defaults */
	if (!storage_sd_mount()) {
		dosboard_note("dosboard: no card could be read yet: the [dosboard] "
			      "settings are the defaults.");
		return;
	}
	int i;
	for (i = 0; files[i]; i++) {
		int err = ini_parse(files[i], parse_cfg, &dosboard_cfg);
		if (err == -1)
			continue;               /* not there */
		dosboard_note("dosboard: settings from %s.", files[i]);
		if (err > 0)
			dosboard_note("dosboard: %s line %d could not be read (and any "
				      "others like it).", files[i], err);
		break;
	}
	if (!files[i])
		dosboard_note("dosboard: no dosboard.ini on the card: the [dosboard] "
			      "settings are the defaults.");
	/* the RGB driver wants the frame to be a whole number of bounce buffers */
	if (dosboard_cfg.bounce_px < 0 ||
	    (dosboard_cfg.bounce_px && (LCD_WIDTH * LCD_HEIGHT) % dosboard_cfg.bounce_px)) {
		dosboard_note("dosboard: lcd_bounce_px %d does not divide %d, so 0 (no "
			      "bounce buffers).", dosboard_cfg.bounce_px, LCD_WIDTH * LCD_HEIGHT);
		dosboard_cfg.bounce_px = 0;
	}
}

/* ==== HDMI ==== */

static void hdmi_task(void *arg)
{
	int led = 0;

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(500));
		ms7210_poll(&hdmi);
		/* the status LED blinks once a second while all is well */
		gpio_set_level(LED_STATUS_GPIO, led ^= 1);
	}
}

int dosboard_video_start(void)
{
	int r;

	hdmi.cfg.pclk_phase = dosboard_cfg.pclk_phase;
	hdmi.cfg.mode = dosboard_cfg.hdmi_mode;
	hdmi.cfg.audio = dosboard_cfg.audio;
	dosboard_note("dosboard: MS7210 set-up, PCLK phase %d, S31 PCLK %s edge.",
		      hdmi.cfg.pclk_phase, dosboard_cfg.pclk_neg ? "falling" : "rising");
	r = ms7210_init(&hdmi);
	if (r)
		dosboard_note("dosboard: MS7210 set-up had %d I2C errors.", hdmi.errors);
	xTaskCreatePinnedToCore(hdmi_task, "hdmi", 4096, NULL, 1, NULL, 0);
	return r;
}

/* Eight colour bars over a grey ramp, with a white frame round the edge:
   colour order, the six bits of green, and the edges of the picture. */
void dosboard_test_card(uint16_t *fb, int w, int h)
{
	static const uint16_t bars[8] = {
		0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000,
	};
	int ramp_y = h * 2 / 3;

	for (int y = 0; y < h; y++) {
		uint16_t *row = fb + y * w;
		for (int x = 0; x < w; x++) {
			uint16_t c;
			if (y < ramp_y) {
				c = bars[x * 8 / w];
			} else {
				int k = x * 32 / w;                 /* 32 grey steps */
				c = (k << 11) | ((k * 2) << 5) | k;
			}
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
				c = 0xFFFF;
			row[x] = c;
		}
	}
}

#endif /* USE_LCD_RGB_DOSBOARD */
