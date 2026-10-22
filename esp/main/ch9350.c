/*
 * WCH CH9350L in lower mode on the DOS board (see ch9350.h).
 *
 * Frames (DS26 7.2.4): 57 AB, an opcode, then that opcode's parameters.
 * In state 2 there is no length byte and no checksum, so the parser knows
 * each opcode's length (ch9350.md 6.7) and looks for 57 AB again only after
 * an opcode it does not know, after a frame that fails a check, or when a
 * frame stops halfway: a mouse frame may carry 57 AB as data (dx 0x57,
 * dy 0xAB = -85), so searching inside frames would cut it in two.
 */
#include <string.h>
#include "ch9350.h"

enum {
	ST_H1,          /* want 0x57 */
	ST_H2,          /* want 0xAB */
	ST_OP,          /* opcode */
	ST_LEN,         /* 0x83 and 0x88: bytes after this one */
	ST_BODY,        /* fixed or counted body into p->body */
	ST_81HDR,       /* 0x81: port, payload length (two bytes) */
	ST_81SKIP,      /* 0x81: payload, two id bytes, sum (not kept) */
};

/* Parameter bytes after the opcode, for the frames of fixed length
   (DS26 p. 10; ch9350.md 6.1 and 6.7).  -1: not fixed or unknown. */
static int fixed_len(uint8_t op)
{
	switch (op) {
	case 0x01: return 8;     /* keyboard, states 2 to 4 */
	case 0x02: return 4;     /* relative mouse, state 2 */
	case 0x04: return 7;     /* absolute mouse, states 3 and 4 */
	case 0x10: return 4;     /* VID and PID change */
	case 0x40: return 1;     /* working state switch */
	case 0x80: return 1;     /* status change, states 2 to 4 */
	case 0x82: return 1;     /* status request, states 0 and 1 */
	case 0x85: return 1;     /* working state change */
	case 0x84:               /* reset delay */
	case 0x86:               /* device disconnected (also seen at attach) */
	case 0x87:               /* get version */
	case 0x89:               /* timeout configuration */
		return 0;
	default:
		return -1;
	}
}

void ch9350_parser_init(ch9350_parser_t *p, int mode, const ch9350_sink_t *sink,
			uint32_t timeout_us, uint32_t zero_guard_us)
{
	memset(p, 0, sizeof(*p));
	p->sink = *sink;
	p->mode = mode;
	p->st = ST_H1;
	p->timeout_us = timeout_us;
	p->zero_guard_us = zero_guard_us;
}

static uint8_t sum8(const uint8_t *d, int n)
{
	uint8_t s = 0;

	while (n-- > 0)
		s += *d++;
	return s;
}

static int all_zero(const uint8_t *d, int n)
{
	while (n-- > 0)
		if (*d++)
			return 0;
	return 1;
}

static int boot_report_ok(const uint8_t *r)
{
	if (r[1] != 0)
		return 0;
	for (int i = 2; i < 8; i++)
		if (r[i] > 0xA4 && (r[i] < 0xE0 || r[i] > 0xE7))
			return 0;
	return 1;
}

static void kbd(ch9350_parser_t *p, const uint8_t *r, uint32_t now)
{
	if (p->zero_guard_us && all_zero(r, 8) &&
	    p->mouse_frames && (uint32_t)(now - p->last_mouse_us) < p->zero_guard_us) {
		p->zero_ignored++;
		return;
	}
	p->kbd_frames++;
	if (p->sink.kbd)
		p->sink.kbd(p->sink.ctx, r);
}

static void mouse(ch9350_parser_t *p, const uint8_t *d, int n, uint32_t now)
{
	p->mouse_frames++;
	p->last_mouse_us = now;
	if (p->sink.mouse)
		p->sink.mouse(p->sink.ctx, d[0] & 0x1F, (int8_t)d[1], (int8_t)d[2],
			      n >= 4 ? (int8_t)d[3] : 0);
}

/* State 0 (0x88) and state 1 (0x83) data frames, DS26 7.2.4.11:
   len, label, the device's own report, sequence number, sum of report and
   sequence.  Label bits 5:4 = 01 keyboard, 10 mouse; bits 2:1 = 01 HID
   protocol, which is when a report ID comes first (reported by KVMS, see
   ch9350.md 6.3). */
static void data_frame(ch9350_parser_t *p, uint32_t now)
{
	const uint8_t *b = p->body;
	int n = b[0] - 3;               /* report bytes */
	uint8_t label = b[1];
	const uint8_t *d = b + 2;
	uint8_t seq = d[n], sum = d[n + 1];
	int type = (label >> 4) & 3, proto = (label >> 1) & 3;

	if ((uint8_t)(sum8(d, n) + seq) != sum) {
		p->bad_sums++;
		return;
	}
	if (type == 1) {
		if (proto == 1 && n >= 9) {
			d++;
			n--;
		}
		/* the device's own report, as it sent it: only one laid out as a
		   boot report (8 bytes, reserved byte 0, key codes in range) is
		   read as one; an NKRO bitmap or any other layout is counted, not
		   turned into wrong keys, as state 2 frames are */
		if (n != 8 || !boot_report_ok(d)) {
			p->bad_frames++;
			return;
		}
		kbd(p, d, now);
	} else if (type == 2) {
		if (proto == 1 && n >= 5) {
			d++;
			n--;
		}
		if (n < 3) {
			p->bad_frames++;
			return;
		}
		mouse(p, d, n, now);
	}
	/* multimedia and other devices: nothing for a PC of this age */
}

static void dispatch(ch9350_parser_t *p, uint32_t now)
{
	const uint8_t *d = p->body;

	p->frames++;
	switch (p->op) {
	case 0x01:
		/* the reserved byte of a boot report is zero, and its key codes
		   are at most 0xA4 (HID usage tables); anything else means the
		   frame was put together from the wrong bytes [inferred check] */
		if (!boot_report_ok(d)) {
			p->bad_frames++;
			break;
		}
		kbd(p, d, now);
		break;
	case 0x02:
		mouse(p, d, 4, now);
		break;
	case 0x04:
		break;              /* absolute mouse: states 3 and 4 only */
	case 0x86:
		if (p->sink.release_all)
			p->sink.release_all(p->sink.ctx);
		break;
	case 0x83:
	case 0x88:
		data_frame(p, now);
		break;
	default:
		if (p->sink.status)
			p->sink.status(p->sink.ctx, p->op, p->have ? d[0] : 0);
		break;
	}
}

/* Back to hunting for a header after a bad byte b; b may itself start one. */
static void resync(ch9350_parser_t *p, uint8_t b)
{
	p->resyncs++;
	p->hunting = 1;
	p->st = (b == 0x57) ? ST_H2 : ST_H1;
}

static void step(ch9350_parser_t *p, uint8_t b, uint32_t now)
{
	int n;

	switch (p->st) {
	case ST_H1:
		if (b == 0x57) {
			p->st = ST_H2;
		} else {
			p->junk_bytes++;
			if (!p->hunting) {
				p->hunting = 1;
				p->resyncs++;
			}
		}
		break;
	case ST_H2:
		if (b == 0xAB) {
			p->st = ST_OP;
		} else {
			p->junk_bytes++;          /* the 0x57 before */
			if (!p->hunting) {
				p->hunting = 1;
				p->resyncs++;
			}
			if (b != 0x57) {
				p->junk_bytes++;
				p->st = ST_H1;
			}
		}
		break;
	case ST_OP:
		p->op = b;
		p->have = 0;
		p->hunting = 0;
		if (b == 0x83 || b == 0x88) {
			p->st = ST_LEN;
		} else if (b == 0x81) {
			p->need = 3;
			p->st = ST_81HDR;
		} else if ((n = fixed_len(b)) < 0) {
			p->unknown_ops++;
			resync(p, b);
		} else if (n == 0) {
			dispatch(p, now);
			p->st = ST_H1;
		} else {
			p->need = n;
			p->st = ST_BODY;
		}
		break;
	case ST_LEN:
		/* label, at least one report byte, sequence and sum; under 72 in all */
		if (b < 4 || b >= CH9350_MAX_BODY - 1) {
			p->bad_frames++;
			resync(p, b);
			break;
		}
		p->body[0] = b;
		p->have = 1;
		p->need = b;
		p->st = ST_BODY;
		break;
	case ST_BODY:
		p->body[p->have++] = b;
		if (--p->need == 0) {
			dispatch(p, now);
			p->st = ST_H1;
		}
		break;
	case ST_81HDR:
		p->body[p->have++] = b;
		if (--p->need == 0) {
			int len = p->body[1] | (p->body[2] << 8);   /* low byte first [inferred] */
			if (len > 1024) {
				p->bad_frames++;
				resync(p, b);
				break;
			}
			p->need = len + 3;      /* payload, two id bytes, sum */
			p->sum = 0;
			p->st = ST_81SKIP;
		}
		break;
	case ST_81SKIP:
		if (p->need > 1) {
			p->sum += b;            /* sum over payload and the two id bytes */
		} else {
			if (b != p->sum) {
				p->bad_sums++;
			} else {
				p->frames++;
				if (p->sink.status)
					p->sink.status(p->sink.ctx, 0x81, p->body[0]);
			}
			p->st = ST_H1;
		}
		p->need--;
		break;
	}
}

void ch9350_parser_feed(ch9350_parser_t *p, const uint8_t *data, int n, uint32_t now_us)
{
	if (n <= 0)
		return;
	/* the chip sends a frame in one go (11 bytes take about 1 ms at
	   115200); a frame still open after a long gap lost a byte */
	if (p->st != ST_H1 && p->timeout_us &&
	    (uint32_t)(now_us - p->last_us) > p->timeout_us) {
		p->timeouts++;
		p->st = ST_H1;
		p->hunting = 0;
	}
	p->last_us = now_us;
	for (int i = 0; i < n; i++)
		step(p, data[i], now_us);
}

void ch9350_led_frame(uint8_t out[CH9350_LED_FRAME_LEN], int usb_leds, int mode)
{
	static const uint8_t f[CH9350_LED_FRAME_LEN] = {
		0x57, 0xAB, 0x12,
		0x00, 0x00, 0x00, 0x00,     /* port 1 and port 2 PID: not needed */
		0x00,                       /* keyboard LEDs */
		0x07,                       /* state byte (reported: KVMS, ch9350.md 6.5) */
		0xAC, 0x20,                 /* status value, fixed value */
	};

	memcpy(out, f, sizeof(f));
	out[7] = usb_leds & 0x07;
	if (mode == 0)
		out[8] = 0x00;              /* state 0 and 1 (reported: DVPL, ch9350.md 6.5) */
}

#ifndef ESP_PLATFORM
/* for the host test only: the firmware has kbd_input.c do this
   (ps2_kbd_leds_changed()) */
int ch9350_ps2_leds_to_usb(int ps2)
{
	return ((ps2 >> 1) & 1)            /* Num */
	     | (((ps2 >> 2) & 1) << 1)     /* Caps */
	     | ((ps2 & 1) << 2);           /* Scroll */
}
#endif

/* ==== into kbd_input.c, and from there tiny386's PS/2 keyboard and mouse ==== */

#include "kbd_input.h"

static void in_kbd(void *ctx, const uint8_t r[8])
{
	ch9350_input_t *t = ctx;

	/* a report is a snapshot of the keys held, so the chip's 3 or 4
	   copies of each frame (ch9350.md 6.6) make no events after the
	   first; an ErrorRollOver report is ignored there */
	kbd_input_report(t->dev, r);
}

static void in_mouse(void *ctx, int buttons, int dx, int dy, int wheel)
{
	ch9350_input_t *t = ctx;

	t->buttons = buttons & 0x1F;
	/* as HID has them: dy positive down, wheel positive up; kbd_input.c
	   turns them into what ps2_mouse_event() takes */
	kbd_input_mouse(dx, dy, wheel, buttons & 0x1F);
}

static void in_release(void *ctx)
{
	ch9350_input_t *t = ctx;

	kbd_input_release_all();
	if (t->buttons) {
		t->buttons = 0;
		kbd_input_mouse(0, 0, 0, 0);
	}
}

void ch9350_input_sink(ch9350_sink_t *sink, ch9350_input_t *t)
{
	sink->ctx = t;
	sink->kbd = in_kbd;
	sink->mouse = in_mouse;
	sink->release_all = in_release;
}

#if defined(ESP_PLATFORM) && defined(KM_UART_NUM)
/*
 * The DOS board: UART1 at 115200 8N1 on GPIO47 (RX) and GPIO55 (TX), RST on
 * GPIO7 (high = reset), S0 on GPIO46 (low = state 2).  One task on core 0.
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "common.h"
#include "console.h"
#include "status_led.h"

/* Nothing at all from the chip this long after its reset: yellow 4 on the
   status LED until a byte comes.  In state 0 the chip asks for status every
   second (DS26 7.2.4.2); in state 2 it is said to echo the LED frame this
   task sends each second (ch9350.md 6.5, seen by others, not checked here).
   If a working chip with nothing plugged in says nothing at all, the LED
   shows yellow 4 until a keyboard or mouse goes in. */
#define CH9350_SILENT_US 3000000

static ch9350_parser_t parser;
static ch9350_input_t input = { .dev = -1 };
static int chip_mode = 2;
static int zero_guard_ms;
static int usb_leds;
static atomic_int leds_wanted = -1;

static void send_leds(void)
{
	uint8_t f[CH9350_LED_FRAME_LEN];

	ch9350_led_frame(f, usb_leds, chip_mode);
	uart_write_bytes(KM_UART_NUM, f, sizeof(f));
}

static uint32_t status_frames;          /* 0x80 and 0x82 frames, for the log */
static int64_t last_answer;

static void sink_status(void *ctx, uint8_t op, uint8_t arg)
{
	/* 0x80 (states 2 to 4) and 0x82 (states 0 and 1) want an answer
	   status frame; it carries the LEDs.  No more than one answer in 100
	   ms: a chip that answered every LED frame with another status frame
	   would otherwise keep both sides sending at wire speed. */
	if (op == 0x80 || op == 0x82) {
		int64_t now = esp_timer_get_time();
		status_frames++;
		if (now - last_answer >= 100000) {
			send_leds();
			last_answer = now;
		}
	}
}

/* kbd_input.c, from the emulator's core when the guest sets the lights
   (PS/2 0xED), already in the CH9350L's order: only noted here, the task
   sends the frame */
static void leds_changed(uint8_t leds)
{
	atomic_store(&leds_wanted, leds & 0x07);
}

static void chip_reset(void)
{
	/* S0 is read at reset: low = state 2, high (with S1 floating high) =
	   state 0.  RST is active high, at least 100 ns (DS26 7.1.2). */
	gpio_set_level(KM_S0_GPIO, chip_mode == 2 ? 0 : 1);
	gpio_set_level(KM_RST_GPIO, 1);
	esp_rom_delay_us(2000);
	gpio_set_level(KM_RST_GPIO, 0);
	vTaskDelay(pdMS_TO_TICKS(100) + 1);
}

static void ch9350_task(void *arg)
{
	static uint8_t buf[128];
	ch9350_sink_t sink = { 0 };
	const uart_config_t cfg = {
		.baud_rate = KM_BAUD,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	int64_t last_led = 0, last_log = 0;
	uint32_t last_errors = 0, last_status = 0;

	ch9350_input_sink(&sink, &input);
	sink.status = sink_status;

	esp_err_t err = uart_driver_install(KM_UART_NUM, 1024, 0, 0, NULL, 0);
	if (err == ESP_OK)
		err = uart_param_config(KM_UART_NUM, &cfg);
	if (err == ESP_OK)
		err = uart_set_pin(KM_UART_NUM, KM_TX_GPIO, KM_RX_GPIO,
				   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		console_printf(CON_ERROR, "CH9350L: UART%d could not be set up (%s): no "
			       "keyboard or mouse.", KM_UART_NUM, esp_err_to_name(err));
		vTaskDelete(NULL);
		return;
	}
	/* a reset now, so that every device attaches again and its keys
	   start from nothing held */
	chip_reset();
	uart_flush_input(KM_UART_NUM);
	ch9350_parser_init(&parser, chip_mode, &sink, 100000, zero_guard_ms * 1000u);
	int64_t reset_at = esp_timer_get_time();
	bool heard = false, said = false;

	for (;;) {
		int n = uart_read_bytes(KM_UART_NUM, buf, sizeof(buf), 1);
		int64_t now = esp_timer_get_time();

		if (n > 0)
			ch9350_parser_feed(&parser, buf, n, (uint32_t)now);
		if (!heard && n > 0) {
			heard = true;
			if (said) {
				status_led_error(STATUS_ERR_KEYBOARD, false);
				console_uart_printf("ch9350: the chip is sending now");
			}
		} else if (!heard && !said && now - reset_at > CH9350_SILENT_US) {
			said = true;
			status_led_error(STATUS_ERR_KEYBOARD, true);
			console_uart_printf("ch9350: nothing from the chip %d s after its reset "
					    "(status LED yellow 4). With a keyboard or mouse plugged "
					    "in, check U301's 3.3 V, its RST (GPIO7, high = reset) "
					    "and the UART lines (GPIO47 from its TXD, GPIO55 to its "
					    "RXD).",
					    CH9350_SILENT_US / 1000000);
		}

		int want = atomic_exchange(&leds_wanted, -1);
		if (want >= 0 && want != usb_leds) {
			usb_leds = want;
			send_leds();
			last_led = now;
		} else if (chip_mode == 2 && now - last_led > 1000000) {
			/* about once a second, as a real upper chip does (ch9350.md 6.5) */
			send_leds();
			last_led = now;
		}

		uint32_t errors = parser.resyncs + parser.unknown_ops + parser.timeouts +
				  parser.bad_frames + parser.bad_sums;
		/* every 10 s at most, when there were errors, or more status
		   frames than a chip at rest sends (a few a second at most) */
		if ((errors != last_errors || status_frames - last_status > 100) &&
		    now - last_log > 10000000) {
			console_uart_printf("ch9350: %lu frames (%lu keyboard, %lu mouse, %lu "
				"status); resyncs %lu, junk %lu, unknown %lu, timeouts %lu, bad %lu, "
				"bad sums %lu, zero frames dropped %lu",
				(unsigned long)parser.frames, (unsigned long)parser.kbd_frames,
				(unsigned long)parser.mouse_frames, (unsigned long)status_frames,
				(unsigned long)parser.resyncs,
				(unsigned long)parser.junk_bytes, (unsigned long)parser.unknown_ops,
				(unsigned long)parser.timeouts, (unsigned long)parser.bad_frames,
				(unsigned long)parser.bad_sums, (unsigned long)parser.zero_ignored);
			last_errors = errors;
			last_status = status_frames;
			last_log = now;
		}
	}
}

void ch9350_setup(int mode, int guard_ms)
{
	static bool started;
	const gpio_config_t out = {
		.pin_bit_mask = (1ULL << KM_RST_GPIO) | (1ULL << KM_S0_GPIO),
		.mode = GPIO_MODE_OUTPUT,
	};

	if (started)
		return;
	started = true;
	chip_mode = (mode == 0) ? 0 : 2;
	zero_guard_ms = guard_ms;
	/* hold the chip in reset with S0 set until the task is ready */
	gpio_set_level(KM_RST_GPIO, 1);
	gpio_set_level(KM_S0_GPIO, chip_mode == 2 ? 0 : 1);
	gpio_config(&out);

	/* one kbd_input.c keyboard for whatever is on the chip's two ports:
	   it sends one report for both (DS26 p. 13) */
	input.dev = kbd_input_open();
	kbd_input_set_leds_fn(leds_changed);
	kbd_input_start();              /* typematic repeat */
	if (xTaskCreatePinnedToCore(ch9350_task, "ch9350", 4096, NULL, 3, NULL, 0) != pdPASS)
		console_fatal("The task ch9350 could not be started (no memory).");
	console_printf(CON_INFO, "CH9350L started in state %d on UART%d, %d baud: the "
		       "keyboard and mouse go in its two USB-A ports.", chip_mode,
		       KM_UART_NUM, KM_BAUD);
}
#endif /* ESP_PLATFORM && KM_UART_NUM */
