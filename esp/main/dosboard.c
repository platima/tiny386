/*
 * The DOS board's own start-up: the board check, the pins that are new on
 * this board, the MS7210 on its I2C bus, the [dosboard] settings, and the
 * hot plug task.  Pins: board_dosboard.h (rev A2c, CHANGES-A2.md section 19).
 *
 * Start-up order:
 *   app_main     board_early_init(): probe the MS7210 (chip ID A2 20 0A at
 *                I2C 0x59, GPIO0 and GPIO1, the Korvo-1's I2C bus too); on
 *                any other board stop here, before a single pin that is new
 *                on the DOS board has been driven.  If nothing answers at
 *                all, GPIO46 is read (its weak pull-up on for a moment; R302
 *                holds it low on a DOS board) and RSTN is pulsed only if it
 *                reads low.  Then RSTN (GPIO49), the USB-A power and the
 *                sound's mute (GPIO48: the line out and the speakers off
 *                until the I2S clocks run, i2s.c).
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
#include <strings.h>
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
#include "status_led.h"
#include "storage.h"

/* ==== lines from before the boot console ==== */

#define NOTES_MAX 24
/* as long as ms7210.c's log lines (logf_()), so that the first-light
   summary keeps its PCLK meter figure (DOS board review, round 2, S4) */
#define NOTE_LEN 160
/* in PSRAM, made in board_early_init() and freed once shown; the hot plug
   task may note a line while app_main() does, hence the lock */
static char (*s_notes)[NOTE_LEN];
static int s_nnotes;
static bool s_console_up;
static portMUX_TYPE s_notes_lock = portMUX_INITIALIZER_UNLOCKED;

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
	portENTER_CRITICAL(&s_notes_lock);
	bool up = s_console_up;
	if (!up && s_notes && s_nnotes < NOTES_MAX)
		memcpy(s_notes[s_nnotes++], line, NOTE_LEN);
	portEXIT_CRITICAL(&s_notes_lock);
	if (up)
		console_puts(sounds_bad(line) ? CON_WARN : CON_INFO, line);
	else
		fprintf(stderr, "%s\n", line);
}

void board_console_ready(void)
{
	portENTER_CRITICAL(&s_notes_lock);
	char (*notes)[NOTE_LEN] = s_notes;
	int n = s_nnotes;
	s_notes = NULL;
	s_nnotes = 0;
	s_console_up = true;
	portEXIT_CRITICAL(&s_notes_lock);
	/* console_puts() also writes to the UART, which had these already;
	   a second copy there marks where the console began */
	for (int i = 0; i < n; i++)
		console_puts(sounds_bad(notes[i]) ? CON_WARN : CON_INFO, notes[i]);
	free(notes);
}

dosboard_cfg_t dosboard_cfg = {
	/* PCLK inverted inside the MS7210 (0x1281 = 0x04, as both reference
	   tables have it) against the S31's pclk_active_neg = 1: by ESP-IDF's
	   register description the S31 then changes the data on the rising
	   edge, and the MS7210 samples on the other one (DOS board review,
	   round 2, S5; dosboard.ini lists the four pairs) */
	.pclk_phase = 4,
	.pclk_neg = LCD_PCLK_ACTIVE_NEG,
	.hdmi_mode = MS7210_MODE_AUTO,
	.audio = 1,
	.bounce_px = LCD_BOUNCE_BUFFER_PX,
	.test_card_ms = 2000,
	.ch9350_state = 2,
	.kbd_zero_guard_ms = 0,
	.rb_swap = 0,
	.i2s_slot_bits = 16,
	.audio_shift = AUDIO_SHIFT_DEFAULT,
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

/* RSTN is GPIO49 from rev A2c, a camera input on a Korvo-1, so it is driven
   only once board_early_init() has made it an output: until then the
   probe's reset pulse (ms7210.c row A1) is left out, and R603 holds RSTN
   high. */
static bool s_rstn_ours;
/* what board_early_init() saw, for the messages: the Korvo-1 device found
   on the bus, whether the MS7210 answered before its reset, and GPIO46
   (-1 not read, 0 high, 1 low) */
static int s_korvo1_addr = -1;
static bool s_found_unreset;
static int s_strap = -1;

static void io_rstn(void *ctx, int level)
{
	if (s_rstn_ours)
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

/* the MS7210's two handles (fast and slow) at another 7-bit address */
static int i2c_readdress(uint8_t addr7)
{
	i2c_device_config_t dc = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = addr7,
		.scl_speed_hz = HDMI_I2C_HZ,
	};

	if (dev_fast)
		i2c_master_bus_rm_device(dev_fast);
	if (dev_slow)
		i2c_master_bus_rm_device(dev_slow);
	dev_fast = dev_slow = NULL;
	if (i2c_master_bus_add_device(i2c_bus, &dc, &dev_fast) != ESP_OK)
		return -1;
	dc.scl_speed_hz = HDMI_DDC_HZ;
	if (i2c_master_bus_add_device(i2c_bus, &dc, &dev_slow) != ESP_OK)
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
	/* leave GPIO0 and GPIO1 as they were found: on a Korvo-1 they are the
	   codec's, the touch panel's and the camera's I2C.  No other pin has
	   been driven: RSTN (GPIO49 since rev A2c, a camera input on the
	   Korvo-1) only after the MS7210 answered or GPIO46 read low, and
	   GPIO46 itself only read, then left an input with no pull. */
	char seen[96];

	i2c_close();
	if (s_rstn_ours)
		gpio_reset_pin(HDMI_RSTN_GPIO);
	/* yellow 2 on the status LED: on a DOS board with no picture (a dead
	   MS7210) the LED is all there is.  GPIO37 drives the same LED circuit
	   on a Korvo-1, so this is safe there too. */
	status_led_fatal(STATUS_ERR_HDMI_CHIP);
	if (hdmi.errors < 0)
		snprintf(seen, sizeof(seen), "the I2C bus could not be set up");
	else if (s_found_unreset)
		snprintf(seen, sizeof(seen), "found before its reset, not after it: "
			 "look at RSTN, GPIO49");
	else if (s_strap == 0 && s_korvo1_addr >= 0)
		snprintf(seen, sizeof(seen), "nothing at 0x%02X or 0x%02X; GPIO46 high (no R302) "
			 "and a device at 0x%02X, as on a Korvo-1", MS7210_I2C_ADDR,
			 MS7210_I2C_ADDR_ALT, s_korvo1_addr);
	else if (s_strap == 0)
		snprintf(seen, sizeof(seen), "nothing at 0x%02X or 0x%02X, and GPIO46 high: no "
			 "R302, so not a DOS board", MS7210_I2C_ADDR, MS7210_I2C_ADDR_ALT);
	else if (hdmi.errors && s_korvo1_addr >= 0)
		snprintf(seen, sizeof(seen), "nothing at 0x%02X or 0x%02X, before or after its "
			 "reset; a device at 0x%02X", MS7210_I2C_ADDR, MS7210_I2C_ADDR_ALT,
			 s_korvo1_addr);
	else if (hdmi.errors)
		snprintf(seen, sizeof(seen), "nothing answered at 0x%02X or 0x%02X",
			 MS7210_I2C_ADDR, MS7210_I2C_ADDR_ALT);
	else
		snprintf(seen, sizeof(seen), "chip ID at 0x%02X read %02X %02X %02X, "
			 "expected A2 20 0A", hdmi.cfg.i2c_addr, hdmi.chip_id[0],
			 hdmi.chip_id[1], hdmi.chip_id[2]);
	for (;;) {
		fprintf(stderr,
			"\n*** tiny386 for the DOS board: no MS7210 HDMI transmitter on I2C"
			" (%s).\n"
			"*** This image is only for the DOS board. On an ESP32-S31-Korvo-1 flash the"
			" Korvo-1 image.\n"
			"*** On a DOS board: check the MS7210's 1.2 V and 3.3 V, RSTN (GPIO49,"
			" R603 holds it high) and the I2C pull-ups.\n"
			"*** Halted; nothing else was started.\n",
			seen);
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}

/* The MS7210's chip ID at 0x59, else at 0x2B: 0 when found, else -1, with
   hdmi.errors saying what was seen (below 0: no bus; 0: something else
   answered at 0x59; above 0: nothing answered at either). */
static int probe_ms7210(void)
{
	ms7210_cfg_t alt;

	if (i2c_readdress(MS7210_I2C_ADDR) != 0) {
		hdmi.errors = -1;
		return -1;
	}
	if (ms7210_probe(&hdmi, &hdmi_io, NULL) == 0)
		return 0;
	/* Nothing answered at 0x59 (SA high, as the board has it, after the
	   data sheet's text): its figure 3 has SA to ground, 0x2B, so that is
	   tried before giving up.  Only the MS7210's own chip ID counts at
	   either (DOS board review, round 2, note 8). */
	if (hdmi.errors <= 0)
		return -1;              /* something else answered at 0x59 */
	fprintf(stderr, "dosboard: nothing at I2C 0x%02X; trying 0x%02X\n",
		MS7210_I2C_ADDR, MS7210_I2C_ADDR_ALT);
	if (i2c_readdress(MS7210_I2C_ADDR_ALT) != 0) {
		hdmi.errors = -1;
		return -1;
	}
	ms7210_default_cfg(&alt);
	alt.i2c_addr = MS7210_I2C_ADDR_ALT;
	return ms7210_probe(&hdmi, &hdmi_io, &alt);
}

/* A Korvo-1's own devices on GPIO0 and GPIO1: its ES8389 codec at 0x10 (or
   0x11 with its address pin high; the schematic's "0x20" is the 8-bit form,
   as its "OV3660 0x78" is) and the touch controller of the panel kit at
   0x14 (or 0x5D) [Korvo-1 schematic v1.1, sheet 3; research s31-module.md
   7.6].  The DOS board's bus has the MS7210 alone, and whatever is put on
   J202.  An address probe only: nothing is written. */
static int korvo1_device(void)
{
	static const uint8_t addrs[] = { 0x10, 0x11, 0x14, 0x5D };

	for (unsigned i = 0; i < sizeof(addrs); i++)
		if (i2c_master_probe(i2c_bus, addrs[i], 50) == ESP_OK)
			return addrs[i];
	return -1;
}

/* GPIO46: the CH9350L's S0 on the DOS board, held at ground by R302 (1k);
   camera D0 on a Korvo-1, an input that only the camera drives [research
   s31-module.md 11].  Read with the S31's weak pull-up on (45k typical,
   ESP32-S31 datasheet v0.5 table 5-4), it reads low on a DOS board (about
   0.07 V) and high on a Korvo-1 whose camera has not been set up [inferred:
   a sensor keeps its data pads off until it is told].  Low on every read
   counts as low.  The pin is then left as the chip has it after reset, an
   input with no pull (table 2-1).  The CH9350L reads S0 only at its own
   reset, which R303 holds off until ch9350.c gives one, so nothing changes
   there (v3.5 review, F3).  1 = low, 0 = high. */
static int s0_strap_low(void)
{
	const gpio_config_t up = {
		.pin_bit_mask = 1ULL << KM_S0_GPIO,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};
	const gpio_config_t plain = {
		.pin_bit_mask = 1ULL << KM_S0_GPIO,
		.mode = GPIO_MODE_INPUT,
	};
	int low = 1;

	gpio_config(&up);
	esp_rom_delay_us(100);          /* 45k into some tens of pF: under 2 us */
	for (int i = 0; i < 4; i++) {
		if (gpio_get_level(KM_S0_GPIO))
			low = 0;
		esp_rom_delay_us(10);
	}
	gpio_config(&plain);
	return low;
}

void board_early_init(void)
{
	const gpio_config_t rstn = {
		.pin_bit_mask = 1ULL << HDMI_RSTN_GPIO,
		.mode = GPIO_MODE_OUTPUT,
	};
	/* GPIO37, the status LED, is status_led.c's (RMT), from app_main() on.
	   GPIO48 (AUD_XSMT) is low before it is an output: the line out and the
	   speakers stay off from here until i2s.c has the I2S clocks running. */
	const gpio_config_t outs = {
		.pin_bit_mask = (1ULL << USB_VBUS_EN_GPIO) | (1ULL << AUDIO_MUTE_GPIO),
		.mode = GPIO_MODE_OUTPUT,
	};
	const gpio_config_t ins = {
		.pin_bit_mask = (1ULL << USER_KEY_GPIO) | (1ULL << SD_CD_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};

	/* room for the lines noted until the boot console is up */
	s_notes = heap_caps_calloc(NOTES_MAX, NOTE_LEN, MALLOC_CAP_SPIRAM);
	fprintf(stderr, "dosboard: looking for the MS7210 at I2C 0x%02X\n", HDMI_I2C_ADDR);
	if (i2c_open() != 0) {
		hdmi.errors = -1;
		wrong_board();
	}
	/* First without the reset pulse (io_rstn() leaves RSTN alone): R603 has
	   held RSTN high since power-on, so a DOS board's MS7210 should answer,
	   and on a Korvo-1 GPIO49 is a camera input, not to be driven. */
	s_found_unreset = probe_ms7210() == 0;
	if (!s_found_unreset) {
		if (hdmi.errors <= 0)
			wrong_board();          /* no bus, or something else at 0x59 */
		/* Nothing answered.  Perhaps a DOS board whose MS7210 wants the
		   reset the data sheet asks for (RSTN high 10 ms or more after its
		   supplies settle, which R603 alone does not give at power-on).
		   Since v3.6 the board's own strap decides (v3.5 review, F3):
		   GPIO46 high, no R302, so no DOS board: stop with RSTN never
		   driven, whatever is on the bus (a Korvo-1 with nothing on its
		   bus included).  GPIO46 low: a DOS board, so on to the probe with
		   the reset as before v3.5, even if a device answers at one of a
		   Korvo-1's addresses (on a DOS board, something on J202's I2C
		   pins).  Those addresses are still looked for, for the
		   messages. */
		s_korvo1_addr = korvo1_device();
		s_strap = s0_strap_low();
		if (!s_strap)
			wrong_board();
		if (s_korvo1_addr >= 0)
			fprintf(stderr, "dosboard: no MS7210 without a reset; GPIO46 low (R302), and a "
				"device at I2C 0x%02X, taken to be on J202: trying again with the reset\n",
				s_korvo1_addr);
		else
			fprintf(stderr, "dosboard: no MS7210 without a reset; GPIO46 low (R302): "
				"trying again with the reset\n");
	}
	/* RSTN is ours from here: high first, so that the first edge is the
	   reset; then the probe again, now with its reset pulse, so that the
	   MS7210 is set up from a clean reset as the data sheet wants */
	gpio_set_level(HDMI_RSTN_GPIO, 1);
	gpio_config(&rstn);
	s_rstn_ours = true;
	if (probe_ms7210() != 0)
		wrong_board();

	/* the right board: the pins that are new on it */
	gpio_set_level(USB_VBUS_EN_GPIO, 1);     /* USB-A ports on (R309 does so too) */
	gpio_set_level(AUDIO_MUTE_GPIO, 0);      /* line out and speakers off */
	gpio_config(&outs);
	gpio_set_level(AUDIO_MUTE_GPIO, 0);
	gpio_config(&ins);
	dosboard_note("dosboard: MS7210 found at I2C 0x%02X%s, %s its reset (RSTN, GPIO49); "
		      "card detect says the card is %s.",
		      hdmi.cfg.i2c_addr, hdmi.cfg.i2c_addr == MS7210_I2C_ADDR ? "" :
		      " (SA low, not as the board is drawn)",
		      s_found_unreset ? "before and after" : "only after",
		      gpio_get_level(SD_CD_GPIO) ? "out" : "in");
	if (s_korvo1_addr >= 0)
		dosboard_note("dosboard: a device at I2C 0x%02X too, an address a Korvo-1 uses; "
			      "GPIO46 low (R302) said this is a DOS board, so it is taken to be on "
			      "J202.", s_korvo1_addr);
}

/* ==== [dosboard] settings ==== */

/* 1, yes, on, true or 0, no, off, false; anything else is -1 */
static int parse_bool(const char *v)
{
	static const char *const yes[] = { "1", "yes", "on", "true", NULL };
	static const char *const no[] = { "0", "no", "off", "false", NULL };
	for (int i = 0; yes[i]; i++)
		if (strcasecmp(v, yes[i]) == 0)
			return 1;
	for (int i = 0; no[i]; i++)
		if (strcasecmp(v, no[i]) == 0)
			return 0;
	return -1;
}

static int parse_cfg(void *user, const char *section, const char *name, const char *value)
{
	dosboard_cfg_t *c = user;
	const char *bad = NULL;         /* what the value should have been */
	int b = parse_bool(value);

	if (strcmp(section, "dosboard") != 0)
		return 1;
	if (strcmp(name, "pclk_phase") == 0) {
		/* one digit, 0 to 7: "8" or "yes" is no phase (DOS board review,
		   round 2, note 7) */
		if (value[0] >= '0' && value[0] <= '7' && value[1] == 0)
			c->pclk_phase = value[0] - '0';
		else
			bad = "0 to 7";
	} else if (strcmp(name, "pclk_neg") == 0) {
		if (b < 0)
			bad = "0 or 1";
		else
			c->pclk_neg = b;
	} else if (strcmp(name, "hdmi_mode") == 0) {
		if (strcasecmp(value, "hdmi") == 0)
			c->hdmi_mode = MS7210_MODE_HDMI;
		else if (strcasecmp(value, "dvi") == 0)
			c->hdmi_mode = MS7210_MODE_DVI;
		else if (strcasecmp(value, "auto") == 0)
			c->hdmi_mode = MS7210_MODE_AUTO;
		else
			bad = "auto, hdmi or dvi";
	} else if (strcmp(name, "hdmi_audio") == 0) {
		if (b < 0)
			bad = "0 or 1";
		else
			c->audio = b;
	} else if (strcmp(name, "rb_swap") == 0) {
		if (b < 0)
			bad = "0 or 1";
		else
			c->rb_swap = b;
	} else if (strcmp(name, "lcd_bounce_px") == 0) {
		c->bounce_px = atoi(value);
	} else if (strcmp(name, "test_card_ms") == 0) {
		c->test_card_ms = atoi(value);
	} else if (strcmp(name, "ch9350_state") == 0) {
		c->ch9350_state = atoi(value) == 0 ? 0 : 2;
	} else if (strcmp(name, "kbd_zero_guard_ms") == 0) {
		/* 0 to 50: a negative guard would become some 71 minutes, and
		   a key let go while the mouse moved would stay held */
		char *end;
		long g = strtol(value, &end, 10);
		if (end != value && *end == 0 && g >= 0 && g <= 50)
			c->kbd_zero_guard_ms = (int) g;
		else
			bad = "0 to 50";
	} else if (strcmp(name, "i2s_slot_bits") == 0) {
		if (atoi(value) == 16 || atoi(value) == 32)
			c->i2s_slot_bits = atoi(value);
		else
			bad = "16 or 32";
	} else if (strcmp(name, "audio_shift") == 0) {
		if (atoi(value) >= 0 && atoi(value) <= 8 && value[0] >= '0' && value[0] <= '8')
			c->audio_shift = atoi(value);
		else
			bad = "0 to 8";
	} else {
		dosboard_note("dosboard.ini: [dosboard] %s is not a setting tiny386 knows; "
			      "ignored.", name);
		return 1;
	}
	if (bad) {
		dosboard_note("dosboard.ini: [dosboard] %s = %s could not be read: it is "
			      "%s; the default is kept.", name, value, bad);
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
	/* and takes two of them from internal RAM: more than 10 rows' worth
	   (2 x 16,000 bytes) would leave too little for the rest (v5.1,
	   dosboard.h) */
	if (dosboard_cfg.bounce_px > DOSBOARD_BOUNCE_MAX_PX) {
		dosboard_note("dosboard: lcd_bounce_px %d is more than internal RAM can spare; "
			      "%d (10 rows) used.", dosboard_cfg.bounce_px, DOSBOARD_BOUNCE_MAX_PX);
		dosboard_cfg.bounce_px = DOSBOARD_BOUNCE_MAX_PX;
	}
	/* what the sound path takes from this file, before i2s_main() runs */
	i2s_slot_bits = dosboard_cfg.i2s_slot_bits;
	i2s_audio_shift = dosboard_cfg.audio_shift;
}

/* ==== HDMI ==== */

static void hdmi_task(void *arg)
{
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(500));
		/* on the status LED: yellow 2 while the MS7210 does not answer,
		   yellow 3 while no monitor's hot plug line is high; both clear
		   themselves */
		int r = ms7210_poll(&hdmi);
		status_led_error(STATUS_ERR_HDMI_CHIP, r < 0);
		if (r >= 0)
			status_led_error(STATUS_ERR_NO_MONITOR, !hdmi.hpd);
	}
}

int dosboard_video_start(void)
{
	int r;

	hdmi.cfg.pclk_phase = dosboard_cfg.pclk_phase;
	hdmi.cfg.mode = dosboard_cfg.hdmi_mode;
	hdmi.cfg.audio = dosboard_cfg.audio;
	hdmi.cfg.rb_swap = dosboard_cfg.rb_swap;
	dosboard_note("dosboard: MS7210 set-up, PCLK phase %d, S31 PCLK %s edge.",
		      hdmi.cfg.pclk_phase, dosboard_cfg.pclk_neg ? "falling" : "rising");
	r = ms7210_init(&hdmi);
	if (r)
		dosboard_note("dosboard: MS7210 set-up had %d I2C errors.", hdmi.errors);
	/* before the boot console: a note, and no hot plug, rather than a stop
	   with nothing on the panel to say why */
	if (xTaskCreatePinnedToCore(hdmi_task, "hdmi", 4096, NULL, 1, NULL, 0) != pdPASS)
		dosboard_note("dosboard: the hot plug task could not be started (no "
			      "memory): a monitor plugged in later is not set up.");
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
