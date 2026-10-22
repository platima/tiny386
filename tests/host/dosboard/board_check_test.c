/*
 * Host test 18 (v3.6): the DOS board image's board check, board_early_init()
 * in esp/main/dosboard.c, built as the DOS board build has it (with
 * board_dosboard.h included first) and with the real ms7210.c, against a
 * pretend I2C bus and pretend pins (the stand-in headers in shim/).
 *
 * Each case is one board: a Korvo-1 with or without its codec and touch
 * panel, with its camera idle or driving D0; a DOS board whose MS7210
 * answers before its reset or only after it, at 0x59 or at 0x2B, with or
 * without R302, with something on J202's I2C pins; a bus that cannot be set
 * up, something else at 0x59, an MS7210 lost after its reset.  Each case
 * runs in a child process of its own, since dosboard.c keeps what it found
 * in statics.
 *
 * In every case:
 *   - no pin is touched but the I2C pair (only through the bus set-up),
 *     GPIO46, GPIO49 (RSTN) and, once the MS7210 has answered, GPIO38,
 *     GPIO48, GPIO42 and GPIO50;
 *   - before the MS7210 has answered there is no GPIO call but GPIO46's,
 *     and GPIO49 is touched only after GPIO46 has read low;
 *   - GPIO46 is read only with its pull-up on, is never an output, and is
 *     left an input with no pull;
 *   - GPIO49 is set high before it is made an output, so that its first
 *     edge is the reset;
 *   - GPIO48 is set low before it is made an output, and never set high;
 *   - the outcome is the one the case expects: the board taken (at which
 *     address, before and after its reset or only after) or the image
 *     stopped with yellow 2 and a message that names what was seen.
 *
 * Built by run.sh; BOARD_CHECK_V35 marks a build against v3.5's dosboard.c,
 * which has to fail the cases v3.6 changed (K3, D4, D5).
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "common.h"
#include "console.h"
#include "dosboard.h"
#include "status_led.h"
#include "storage.h"

/* ==== the boards ==== */

enum strap {
	R302,           /* 1k to ground: the DOS board */
	FLOATS,         /* nothing outside: a Korvo-1 with its camera idle, or R302 left off */
	CAM_LOW,        /* driven low: a Korvo-1 camera that holds D0 low */
	CAM_HIGH,       /* driven high */
};

struct board {
	const char *id, *what;
	int bus_fails;          /* i2c_new_master_bus() refuses */
	int ms_addr;            /* the MS7210's address, 0 = none */
	int ms_before;          /* it answers before any reset pulse */
	int ms_after;           /* it answers after a reset pulse */
	int other_at_59;        /* something that is not an MS7210 answers at 0x59 */
	int others[3];          /* other addresses that answer an address probe */
	enum strap strap;
	/* what should happen */
	int taken;              /* board_early_init() returns */
	int taken_addr;
	const char *note;       /* in the UART text when taken */
	const char *seen;       /* in the UART text when stopped */
	int reads46;            /* GPIO46 read */
	int drives49;           /* GPIO49 made an output */
};

static const struct board boards[] = {
	{ "K1", "Korvo-1: its codec at 0x10, the camera idle",
	  0, 0, 0, 0, 0, { 0x10 }, FLOATS,
	  0, 0, NULL, "GPIO46 high (no R302) and a device at 0x10", 1, 0 },
	{ "K2", "Korvo-1: codec at 0x10 and the panel kit's touch controller at 0x5D",
	  0, 0, 0, 0, 0, { 0x10, 0x5D }, FLOATS,
	  0, 0, NULL, "GPIO46 high (no R302) and a device at 0x10", 1, 0 },
	{ "K3", "Korvo-1: nothing at all on its bus (v3.5 pulsed GPIO49 here)",
	  0, 0, 0, 0, 0, { 0 }, FLOATS,
	  0, 0, NULL, "GPIO46 high: no R302, so not a DOS board", 1, 0 },
	{ "K4", "Korvo-1: codec at 0x10, a camera holding D0 low (the case the strap cannot tell)",
	  0, 0, 0, 0, 0, { 0x10 }, CAM_LOW,
	  0, 0, NULL, "before or after its reset; a device at 0x10", 1, 1 },
	{ "K5", "Korvo-1: nothing on its bus, a camera holding D0 high",
	  0, 0, 0, 0, 0, { 0 }, CAM_HIGH,
	  0, 0, NULL, "GPIO46 high: no R302, so not a DOS board", 1, 0 },
	{ "D1", "DOS board: the MS7210 at 0x59 before and after its reset",
	  0, 0x59, 1, 1, 0, { 0 }, R302,
	  1, 0x59, "MS7210 found at I2C 0x59, before and after its reset", NULL, 0, 1 },
	{ "D2", "DOS board: the MS7210 at 0x59 only after its reset",
	  0, 0x59, 0, 1, 0, { 0 }, R302,
	  1, 0x59, "MS7210 found at I2C 0x59, only after its reset", NULL, 1, 1 },
	{ "D3", "DOS board: the MS7210 at 0x2B (SA low) only after its reset",
	  0, 0x2B, 0, 1, 0, { 0 }, R302,
	  1, 0x2B, "MS7210 found at I2C 0x2B (SA low, not as the board is drawn), only after its reset",
	  NULL, 1, 1 },
	{ "D4", "DOS board: MS7210 only after its reset, a pressure sensor at 0x5D on J202 (v3.5 stopped)",
	  0, 0x59, 0, 1, 0, { 0x5D }, R302,
	  1, 0x59, "a device at I2C 0x5D too", NULL, 1, 1 },
	{ "D5", "DOS board: MS7210 only after its reset, something at 0x10 on J202 (v3.5 stopped)",
	  0, 0x59, 0, 1, 0, { 0x10 }, R302,
	  1, 0x59, "a device at I2C 0x10 too", NULL, 1, 1 },
	{ "D6", "DOS board without R302, MS7210 only after its reset",
	  0, 0x59, 0, 1, 0, { 0 }, FLOATS,
	  0, 0, NULL, "GPIO46 high: no R302, so not a DOS board", 1, 0 },
	{ "D7", "DOS board without R302, MS7210 before and after its reset",
	  0, 0x59, 1, 1, 0, { 0 }, FLOATS,
	  1, 0x59, "MS7210 found at I2C 0x59, before and after its reset", NULL, 0, 1 },
	{ "D8", "something that is not an MS7210 at 0x59",
	  0, 0, 0, 0, 1, { 0 }, R302,
	  0, 0, NULL, "chip ID at 0x59 read 12 00 00", 0, 0 },
	{ "D9", "an I2C bus that cannot be set up",
	  1, 0x59, 1, 1, 0, { 0 }, R302,
	  0, 0, NULL, "the I2C bus could not be set up", 0, 0 },
	{ "D10", "DOS board: an MS7210 that answers before its reset, not after",
	  0, 0x59, 1, 0, 0, { 0 }, R302,
	  0, 0, NULL, "found before its reset, not after it", 0, 1 },
	{ "D11", "DOS board with R302 and no MS7210 answering at all",
	  0, 0, 0, 0, 0, { 0 }, R302,
	  0, 0, NULL, "nothing answered at 0x59 or 0x2B", 1, 1 },
};

/* ==== the pretend chip: pins, bus, clock ==== */

static const struct board *B;

enum { EV_BUS, EV_CONFIG, EV_SET, EV_GET, EV_RESET, EV_ANSWER };
/* EV_CONFIG: a mode, b pull-up, c pull-down, d the level already set
   (-1 none); EV_SET: a level; EV_GET: a what was read, b pull-up, c
   pull-down; EV_BUS: a SDA, b SCL; EV_ANSWER: a the address */
struct ev {
	int op, pin, a, b, c, d;
};
static struct ev evs[512];
static int nev;

struct pin {
	int mode, pu, pd, level, level_set, touched;
};
static struct pin pins[64];

static int rst_low, reset_seen, answered;
static int64_t now_us;
static int halted, led_err;
static jmp_buf halt_jmp;
static char console_text[4096];

static void ev(int op, int pin, int a, int b, int c, int d)
{
	if (nev < (int) (sizeof(evs) / sizeof(evs[0])))
		evs[nev++] = (struct ev) { op, pin, a, b, c, d };
}

static int ms_alive(void)
{
	if (rst_low)
		return 0;
	return reset_seen ? B->ms_after : B->ms_before;
}

static int present(int addr)
{
	if (B->ms_addr && addr == B->ms_addr && ms_alive())
		return 1;
	if (B->other_at_59 && addr == 0x59)
		return 1;
	for (int i = 0; i < 3; i++)
		if (B->others[i] && B->others[i] == addr)
			return 1;
	return 0;
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
	for (int p = 0; p < 64; p++) {
		if (!(cfg->pin_bit_mask >> p & 1))
			continue;
		ev(EV_CONFIG, p, cfg->mode, cfg->pull_up_en, cfg->pull_down_en,
		   pins[p].level_set ? pins[p].level : -1);
		pins[p].mode = cfg->mode;
		pins[p].pu = cfg->pull_up_en;
		pins[p].pd = cfg->pull_down_en;
		pins[p].touched = 1;
	}
	return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t p, uint32_t level)
{
	ev(EV_SET, p, (int) level, 0, 0, 0);
	pins[p].level = (int) level;
	pins[p].level_set = 1;
	pins[p].touched = 1;
	if (p == HDMI_RSTN_GPIO && (pins[p].mode & GPIO_MODE_DEF_OUTPUT)) {
		if (!level)
			rst_low = 1;
		else if (rst_low) {
			rst_low = 0;
			reset_seen = 1;
		}
	}
	return ESP_OK;
}

int gpio_get_level(gpio_num_t p)
{
	int v = 1;

	if (p == KM_S0_GPIO) {
		const struct pin *q = &pins[p];
		if (!(q->mode & GPIO_MODE_DEF_INPUT))
			v = 0;                  /* an input not enabled reads 0 */
		else if (B->strap == R302 || B->strap == CAM_LOW)
			v = 0;
		else if (B->strap == CAM_HIGH)
			v = 1;
		else
			v = q->pu && !q->pd;    /* floating: only the pull-up makes it high */
		ev(EV_GET, p, v, pins[p].pu, pins[p].pd, 0);
	} else {
		ev(EV_GET, p, v, pins[p].pu, pins[p].pd, 0);    /* USER up, no card */
	}
	pins[p].touched = 1;
	return v;
}

esp_err_t gpio_reset_pin(gpio_num_t p)
{
	ev(EV_RESET, p, 0, 0, 0, 0);
	/* as ESP-IDF's: pull-up on, input and output off */
	pins[p].mode = GPIO_MODE_DISABLE;
	pins[p].pu = 1;
	pins[p].pd = 0;
	pins[p].touched = 1;
	return ESP_OK;
}

struct bc_i2c_bus { int used; };
struct bc_i2c_dev { int used; int addr; };
static struct bc_i2c_bus the_bus;
static struct bc_i2c_dev devs[8];

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *bus)
{
	ev(EV_BUS, -1, cfg->sda_io_num, cfg->scl_io_num, cfg->i2c_port, 0);
	if (B->bus_fails)
		return ESP_FAIL;
	the_bus.used = 1;
	*bus = &the_bus;
	return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
	bus->used = 0;
	return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
				    i2c_master_dev_handle_t *dev)
{
	for (int i = 0; i < 8; i++)
		if (!devs[i].used) {
			devs[i].used = 1;
			devs[i].addr = cfg->device_address;
			*dev = &devs[i];
			return ESP_OK;
		}
	return ESP_FAIL;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
	dev->used = 0;
	return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wlen,
			      int timeout_ms)
{
	return present(dev->addr) ? ESP_OK : ESP_FAIL;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wlen,
				      uint8_t *r, size_t rlen, int timeout_ms)
{
	static const uint8_t ms_id[3] = { 0xA2, 0x20, 0x0A }, other_id[3] = { 0x12, 0x34, 0x56 };
	unsigned reg = wlen >= 2 ? (unsigned) (w[0] | w[1] << 8) : 0xFFFF;

	if (B->ms_addr && dev->addr == B->ms_addr && ms_alive()) {
		if (!answered++)
			ev(EV_ANSWER, -1, dev->addr, 0, 0, 0);
		for (size_t i = 0; i < rlen; i++)
			r[i] = reg + i < 3 ? ms_id[reg + i] : 0;
		return ESP_OK;
	}
	if (B->other_at_59 && dev->addr == 0x59) {
		for (size_t i = 0; i < rlen; i++)
			r[i] = reg + i < 3 ? other_id[reg + i] : 0;
		return ESP_OK;
	}
	return ESP_FAIL;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int timeout_ms)
{
	return present(addr) ? ESP_OK : ESP_FAIL;
}

void esp_rom_delay_us(uint32_t us)
{
	now_us += us;
}

int64_t esp_timer_get_time(void)
{
	return now_us;
}

void vTaskDelay(TickType_t ticks)
{
	now_us += (int64_t) ticks * 1000;
	if (halted)
		longjmp(halt_jmp, 1);   /* wrong_board()'s loop, after its message */
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack,
				   void *arg, int prio, TaskHandle_t *handle, int core)
{
	return pdPASS;
}

/* ==== what the rest of the firmware gives dosboard.c ==== */

EventGroupHandle_t global_event_group;
struct Globals globals;
int i2s_slot_bits = 16;
int i2s_audio_shift = AUDIO_SHIFT_DEFAULT;

void status_led_fatal(int err)
{
	halted = 1;
	led_err = err;
}

void status_led_error(int err, bool on)
{
}

bool storage_sd_mount(void)
{
	return false;
}

void console_puts(int level, const char *s)
{
	size_t n = strlen(console_text);
	snprintf(console_text + n, sizeof(console_text) - n, "%s\n", s);
}

/* ==== one case, in a child process ==== */

static int fails;

static void expect(int ok, const char *what)
{
	if (!ok) {
		fails++;
		printf("    FAIL %s\n", what);
	}
}

static int allowed_pin(int p)
{
	return p == KM_S0_GPIO || p == HDMI_RSTN_GPIO || p == USB_VBUS_EN_GPIO ||
	       p == AUDIO_MUTE_GPIO || p == USER_KEY_GPIO || p == SD_CD_GPIO;
}

static int run_case(void)
{
	char uart[8192] = "";
	FILE *cap = tmpfile();
	int saved = dup(2), returned = 0;

	fflush(stderr);
	dup2(fileno(cap), 2);
	if (!setjmp(halt_jmp)) {
		board_early_init();
		returned = 1;
	}
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	rewind(cap);
	size_t got = fread(uart, 1, sizeof(uart) - 1, cap);
	uart[got] = 0;
	fclose(cap);
	board_console_ready();          /* the notes onto the pretend console, and freed */

	char all[8192];
	memcpy(all, uart, got + 1);
	for (char *l = strtok(uart, "\n"); l; l = strtok(NULL, "\n"))
		if (*l)
			printf("    | %s\n", l);

	/* the pins */
	int answer_at = nev, low_reads = 0, high_reads = 0, s0_ok_at_end = 1;
	int drove49 = 0, read46 = 0;
	for (int i = 0; i < nev; i++)
		if (evs[i].op == EV_ANSWER) {
			answer_at = i;
			break;
		}
	for (int i = 0; i < nev; i++) {
		const struct ev *e = &evs[i];
		char what[160];
		if (e->op == EV_BUS) {
			snprintf(what, sizeof(what), "the I2C bus on GPIO%d and GPIO%d, not 0 and 1", e->a, e->b);
			expect(e->a == HDMI_I2C_SDA && e->b == HDMI_I2C_SCL && e->a == 0 && e->b == 1, what);
			continue;
		}
		if (e->op == EV_ANSWER)
			continue;
		snprintf(what, sizeof(what), "GPIO%d touched, which the board check has no business with", e->pin);
		expect(allowed_pin(e->pin), what);
		if (i < answer_at && e->pin != KM_S0_GPIO && e->pin != HDMI_RSTN_GPIO) {
			snprintf(what, sizeof(what), "GPIO%d touched before the MS7210 answered", e->pin);
			expect(0, what);
		}
		if (e->pin == KM_S0_GPIO) {
			if (e->op == EV_CONFIG)
				expect(!(e->a & GPIO_MODE_DEF_OUTPUT), "GPIO46 made an output");
			if (e->op == EV_SET || e->op == EV_RESET)
				expect(0, "GPIO46 set or reset");
			if (e->op == EV_GET) {
				read46 = 1;
				expect(e->b && !e->c, "GPIO46 read without its pull-up on (or with a pull-down)");
				if (e->a)
					high_reads++;
				else
					low_reads++;
			}
		}
		if (e->pin == HDMI_RSTN_GPIO && (e->op == EV_CONFIG || e->op == EV_SET)) {
			if (i < answer_at) {
				snprintf(what, sizeof(what), "GPIO49 driven before the MS7210 answered and with "
					 "GPIO46 not read low (%d low and %d high reads)", low_reads, high_reads);
				expect(low_reads > 0 && high_reads == 0, what);
			}
			if (e->op == EV_CONFIG && (e->a & GPIO_MODE_DEF_OUTPUT)) {
				drove49 = 1;
				expect(e->d == 1, "GPIO49 made an output before it was set high");
			}
		}
		if (e->pin == AUDIO_MUTE_GPIO) {
			if (e->op == EV_CONFIG && (e->a & GPIO_MODE_DEF_OUTPUT))
				expect(e->d == 0, "GPIO48 made an output before it was set low");
			if (e->op == EV_SET && e->a == 1)
				expect(0, "GPIO48 set high by the board check");
		}
		if (e->pin == USB_VBUS_EN_GPIO && e->op == EV_CONFIG && (e->a & GPIO_MODE_DEF_OUTPUT))
			expect(e->d == 1, "GPIO38 made an output before it was set high");
	}
	if (pins[KM_S0_GPIO].touched)
		s0_ok_at_end = pins[KM_S0_GPIO].mode == GPIO_MODE_INPUT && !pins[KM_S0_GPIO].pu &&
			       !pins[KM_S0_GPIO].pd;
	expect(s0_ok_at_end, "GPIO46 not left an input with no pull");
	expect(read46 == B->reads46, B->reads46 ? "GPIO46 not read" : "GPIO46 read when it had no need to be");
	expect(drove49 == B->drives49, B->drives49 ? "GPIO49 never made an output" :
	       "GPIO49 made an output");

	/* the outcome */
	if (B->taken) {
		expect(returned, "the image stopped on a board it should have taken");
		expect(B->note && strstr(all, B->note), "the console line expected when taken is missing");
		expect(pins[USB_VBUS_EN_GPIO].touched && pins[AUDIO_MUTE_GPIO].touched,
		       "taken, but the USB-A power and the mute were not set up");
		if (strstr(B->note, "too"))
			expect(strstr(console_text, B->note) != NULL, "the J202 device not on the console");
	} else {
		expect(!returned, "board_early_init() returned on a board it should have stopped on");
		expect(halted && led_err == STATUS_ERR_HDMI_CHIP, "stopped without yellow 2");
		expect(B->seen && strstr(all, B->seen), "the message does not name what was seen");
		expect(strstr(all, "Halted; nothing else was started") != NULL, "no halt message");
		if (pins[HDMI_RSTN_GPIO].touched && drove49)
			expect(pins[HDMI_RSTN_GPIO].mode == GPIO_MODE_DISABLE,
			       "GPIO49 left an output after the stop");
	}
	printf("    %s: %s; GPIO46 %s; GPIO49 %s; %d GPIO calls; %s\n", B->id,
	       returned ? "taken" : "stopped", read46 ? (low_reads && !high_reads ? "read low" :
	       "read high") : "not read", drove49 ? "driven" : "never driven", nev,
	       fails ? "FAIL" : "as expected");
	return fails ? 1 : 0;
}

int main(void)
{
	int bad = 0, n = sizeof(boards) / sizeof(boards[0]);

	printf("board_check_test: board_early_init() in %d cases%s\n", n,
#ifdef BOARD_CHECK_V35
	       " (dosboard.c at v3.5)"
#else
	       ""
#endif
	       );
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			return 2;
		}
		if (pid == 0) {
			B = &boards[i];
			printf("  %s %s\n", B->id, B->what);
			int r = run_case();
			fflush(stdout);
			exit(r);
		}
		int st;
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			bad++;
			if (!WIFEXITED(st))
				printf("    %s: the child process died (signal %d)\n", boards[i].id,
				       WIFSIGNALED(st) ? WTERMSIG(st) : 0);
		}
	}
	printf("board_check_test: %d of %d cases as expected: %s\n", n - bad, n, bad ? "FAIL" : "PASS");
	return bad ? 1 : 0;
}
