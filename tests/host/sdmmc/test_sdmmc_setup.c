/*
 * v5.3: esp/main/sdmmc_setup_wrap.c, the SD slot's width, sampling mode
 * and delays left alone between commands when nothing has changed.  The
 * wrap is built as the S31 boards build it, around stand-ins for ESP-IDF's
 * five functions that write a pretend controller (the width register, D3's
 * pin, the DDR bit, the delay phase and line), and a model of the start of
 * sd_host_slot_sdmmc_do_transaction() (188e3e55bb): the clock set when the
 * slot's frequency is not ready or the hardware's is not the request, then
 * the four in order, each stopping the command if it fails.
 *
 * Before every command the pretend controller must hold what ESP-IDF's own
 * four would leave in it for the slot's fields as they are then, through
 * the card's start (400 kHz, 1 bit), the switch to 40 MHz and 4 bits, each
 * field changed alone, a clock set (which here also clears the delay phase,
 * as a clock change might), a failed command after a disturbed controller,
 * one of the four failing, the host set up again (the controller reset, the
 * slot's fields back to their start), a second slot, and sd_clock_fix = 0.
 * And the four must be skipped when nothing changed: one call each in a run
 * of good commands, not one a command.
 *
 *   test_sdmmc_setup
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_private/sd_host_private.h"

esp_err_t __wrap_sd_host_slot_set_card_clk(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_slot_set_bus_width(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_slot_set_bus_sampling_mode(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_set_delay_phase(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_set_delay_line(sd_host_sdmmc_slot_t *slot);
void sd_setup_forget(void);
extern uint32_t sd_setup_kept, sd_setup_set;

int sd_clock_fix = 1;

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		if (fails++ < 30) { \
			printf("  FAIL line %d: ", __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

#define SDR104_KHZ 200000
#define DDR 2

/* ==== the pretend controller, and ESP-IDF's five as they write it ==== */

static struct hw {
	int clk_khz;                    /* what the dividers give; 0 after a reset */
	int width;                      /* the width register, 1 after a reset */
	int d3_sdmmc;                   /* D3 given to the SD host (a GPIO before) */
	int ddr;
	int phase, phase_hs;            /* the delay phase, and its speed mode */
	int line;                       /* the delay line (written at SDR104 only) */
} s_hw;

static unsigned s_calls[5];             /* clock, width, sampling, phase, line */
static int s_fail_at = -1;              /* which of the five fails next, once */

static esp_err_t fail_now(int which)
{
	s_calls[which]++;
	if (s_fail_at == which) {
		s_fail_at = -1;
		return ESP_FAIL;
	}
	return ESP_OK;
}

static int eff_width(const sd_host_sdmmc_slot_t *s)
{
	return s->width.width_state == SD_HOST_SLOT_STATE_READY ? s->width.width : 1;
}

static int eff_ddr(const sd_host_sdmmc_slot_t *s)
{
	return s->sampling_mode.sampling_mode_state == SD_HOST_SLOT_STATE_READY &&
	       s->sampling_mode.mode == DDR;
}

static int eff_phase(const sd_host_sdmmc_slot_t *s)
{
	return s->delay_phase.delay_phase_state == SD_HOST_SLOT_STATE_READY ?
	       s->delay_phase.delayphase : 0;
}

static int req_khz(const sd_host_sdmmc_slot_t *s)
{
	return s->freq.freq_state == SD_HOST_SLOT_STATE_READY ? s->freq.freq_hz / 1000 : 400;
}

esp_err_t __real_sd_host_slot_set_card_clk(sd_host_sdmmc_slot_t *slot)
{
	if (fail_now(0))
		return ESP_FAIL;
	s_hw.clk_khz = req_khz(slot);
	/* a clock change may touch what the delay phase set */
	s_hw.phase = -1;
	return ESP_OK;
}

esp_err_t __real_sd_host_slot_set_bus_width(sd_host_sdmmc_slot_t *slot)
{
	if (fail_now(1))
		return ESP_FAIL;
	int w = eff_width(slot);
	s_hw.width = w;
	if (w == 4 || w == 8)
		s_hw.d3_sdmmc = 1;
	return ESP_OK;
}

esp_err_t __real_sd_host_slot_set_bus_sampling_mode(sd_host_sdmmc_slot_t *slot)
{
	if (fail_now(2))
		return ESP_FAIL;
	s_hw.ddr = eff_ddr(slot);
	return ESP_OK;
}

esp_err_t __real_sd_host_set_delay_phase(sd_host_sdmmc_slot_t *slot)
{
	if (fail_now(3))
		return ESP_FAIL;
	s_hw.phase = eff_phase(slot);
	s_hw.phase_hs = slot->freq.freq_hz / 1000 == SDR104_KHZ;
	return ESP_OK;
}

esp_err_t __real_sd_host_set_delay_line(sd_host_sdmmc_slot_t *slot)
{
	if (fail_now(4))
		return ESP_FAIL;
	if (slot->freq.freq_hz / 1000 == SDR104_KHZ)
		s_hw.line = slot->delay_line.delay_line_state == SD_HOST_SLOT_STATE_READY ?
			    slot->delay_line.delayline : 0;
	return ESP_OK;
}

/* ==== a command ==== */

/* what ESP-IDF's own four leave in the controller for the slot as it is */
static bool hw_right(const sd_host_sdmmc_slot_t *s, char *why, size_t n)
{
	int w = eff_width(s);
	bool ok = s_hw.width == w && (!(w == 4 || w == 8) || s_hw.d3_sdmmc) &&
		  s_hw.ddr == eff_ddr(s) && s_hw.phase == eff_phase(s) &&
		  s_hw.phase_hs == (s->freq.freq_hz / 1000 == SDR104_KHZ);
	if (s->freq.freq_hz / 1000 == SDR104_KHZ)
		ok = ok && s_hw.line == (s->delay_line.delay_line_state == SD_HOST_SLOT_STATE_READY ?
					 s->delay_line.delayline : 0);
	snprintf(why, n, "width %d (want %d), d3 %d, ddr %d (%d), phase %d (%d), hs %d, line %d",
		 s_hw.width, w, s_hw.d3_sdmmc, s_hw.ddr, eff_ddr(s), s_hw.phase, eff_phase(s),
		 s_hw.phase_hs, s_hw.line);
	return ok;
}

/* the start of sd_host_slot_sdmmc_do_transaction(), then the command's own
   result (fail: the card's error, which sdmmc_timeout_wrap.c sees); the
   controller held against the slot unless the test has disturbed it */
static esp_err_t command(sd_host_sdmmc_slot_t *s, bool fail, const char *what)
{
	bool check = !fail;
	esp_err_t r = ESP_OK;
	if (s->freq.freq_state != SD_HOST_SLOT_STATE_READY || s_hw.clk_khz != req_khz(s))
		r = __wrap_sd_host_slot_set_card_clk(s);
	if (r == ESP_OK)
		r = __wrap_sd_host_slot_set_bus_width(s);
	if (r == ESP_OK)
		r = __wrap_sd_host_slot_set_bus_sampling_mode(s);
	if (r == ESP_OK)
		r = __wrap_sd_host_set_delay_phase(s);
	if (r == ESP_OK)
		r = __wrap_sd_host_set_delay_line(s);
	if (r == ESP_OK) {
		char why[160];
		CHECK(!check || hw_right(s, why, sizeof(why)), "%s: the controller before the "
		      "command: %s", what, why);
		if (fail)
			r = ESP_ERR_TIMEOUT;
	}
	if (r != ESP_OK)
		sd_setup_forget();      /* as sdmmc_timeout_wrap.c does */
	return r;
}

/* n good commands: how many times the four ran */
static unsigned run(sd_host_sdmmc_slot_t *s, int n, const char *what)
{
	unsigned w0 = s_calls[1];
	for (int i = 0; i < n; i++)
		CHECK(command(s, false, what) == ESP_OK, "%s: command %d failed", what, i);
	return s_calls[1] - w0;
}

static void slot_start(sd_host_sdmmc_slot_t *s, int id, sd_host_sdmmc_ctlr_t *ctlr)
{
	memset(s, 0, sizeof(*s));
	s->slot_id = id;
	s->freq.freq_hz = 400 * 1000;
	s->freq.freq_state = SD_HOST_SLOT_STATE_INIT;
	s->width.width = 4;             /* from the slot's set-up; not ready yet */
	s->clk_src = 1;
	s->ctlr = ctlr;
}

static void hw_reset(void)
{
	memset(&s_hw, 0, sizeof(s_hw));
	s_hw.width = 1;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	static int ctlr_a, ctlr_b;
	sd_host_sdmmc_slot_t s, t;
	unsigned n;

	printf("== the card's start, then 40 MHz and 4 bits\n");
	hw_reset();
	slot_start(&s, 1, (sd_host_sdmmc_ctlr_t *) &ctlr_a);
	n = run(&s, 20, "start");
	CHECK(n == 20 && s_calls[0] == 20, "at 400 kHz every command sets the clock and the four "
	      "(%u, %u)", n, s_calls[0]);
	s.freq.freq_hz = 40000 * 1000;
	s.freq.freq_state = SD_HOST_SLOT_STATE_READY;
	s.width.width_state = SD_HOST_SLOT_STATE_READY;
	n = run(&s, 50, "40 MHz, 4 bits");
	CHECK(n == 1, "40 MHz and 4 bits: the four once in 50 commands (%u)", n);
	CHECK(s_hw.width == 4 && s_hw.d3_sdmmc, "4 bits, D3 the SD host's");

	printf("== each field changed alone\n");
	s.sampling_mode.mode = DDR;
	s.sampling_mode.sampling_mode_state = SD_HOST_SLOT_STATE_READY;
	CHECK(run(&s, 10, "DDR") == 1, "DDR: the four once");
	s.sampling_mode.mode = 1;
	CHECK(run(&s, 10, "SDR") == 1, "back to SDR: once");
	s.delay_phase.delayphase = 2;
	s.delay_phase.delay_phase_state = SD_HOST_SLOT_STATE_READY;
	CHECK(run(&s, 10, "phase 2") == 1, "delay phase 2: once");
	s.delay_line.delayline = 3;
	s.delay_line.delay_line_state = SD_HOST_SLOT_STATE_READY;
	CHECK(run(&s, 10, "line 3") == 1, "delay line 3 (no effect below SDR104): once");
	s.freq.freq_hz = SDR104_KHZ * 1000;
	CHECK(run(&s, 10, "SDR104") == 1, "SDR104: once (and the clock set)");
	CHECK(s_hw.line == 3 && s_hw.phase_hs, "at SDR104 the delay line and the phase's speed mode");
	s.freq.freq_hz = 40000 * 1000;
	CHECK(run(&s, 10, "40 MHz again") == 1, "40 MHz again: once");
	s.width.width = 1;
	CHECK(run(&s, 10, "1 bit") == 1, "1 bit: once");
	s.width.width = 4;
	CHECK(run(&s, 10, "4 bits") == 1, "4 bits: once");
	s.clk_src = 2;
	CHECK(run(&s, 10, "clock source") == 1, "another clock source: once");

	printf("== a clock set\n");
	s_hw.clk_khz = 12345;           /* the hardware's clock not the request's */
	CHECK(run(&s, 10, "clock set") == 1 && s_hw.phase == 2, "after a clock set, the four "
	      "again (the phase restored), once");

	printf("== a failed command after the controller was disturbed\n");
	s_hw.width = 1;
	s_hw.ddr = 1;
	CHECK(command(&s, true, "failing") == ESP_ERR_TIMEOUT, "the failed command");
	CHECK(run(&s, 10, "after a failure") == 1, "after a failed command, the four again, once");

	printf("== one of the four failing\n");
	for (int which = 1; which <= 4; which++) {
		unsigned w0 = s_calls[1];
		s.delay_phase.delayphase = 4 + which;     /* something to set */
		s_fail_at = which;
		CHECK(command(&s, false, "one fails") == ESP_FAIL, "function %d failing fails the "
		      "command", which);
		n = run(&s, 10, "after one failed");
		CHECK(n == 1 && s_calls[1] - w0 == 2, "function %d failed: the four again at the next "
		      "command, then kept (%u)", which, n);
	}

	printf("== the host set up again\n");
	hw_reset();
	slot_start(&s, 1, (sd_host_sdmmc_ctlr_t *) &ctlr_a);
	n = run(&s, 5, "set up again");
	CHECK(n == 5, "the slot back at its start: every command sets the four (%u)", n);
	s.freq.freq_hz = 40000 * 1000;
	s.freq.freq_state = SD_HOST_SLOT_STATE_READY;
	s.width.width_state = SD_HOST_SLOT_STATE_READY;
	CHECK(run(&s, 20, "again 40 MHz") == 1, "then kept");
	/* a controller reset alone, the slot's fields as before: the clock is
	   not the request's, so it is set, and the four with it */
	hw_reset();
	CHECK(run(&s, 20, "controller reset") == 1, "a controller reset: the four again, once");

	printf("== a second slot\n");
	slot_start(&t, 0, (sd_host_sdmmc_ctlr_t *) &ctlr_b);
	t.freq.freq_hz = 40000 * 1000;
	t.freq.freq_state = SD_HOST_SLOT_STATE_READY;
	t.width.width_state = SD_HOST_SLOT_STATE_READY;
	t.width.width = 1;
	s_hw.clk_khz = 40000;
	CHECK(run(&t, 5, "slot 0") == 1, "slot 0: once");
	CHECK(run(&s, 5, "slot 1") == 1, "back to slot 1: once");

	printf("== sd_clock_fix = 0\n");
	sd_clock_fix = 0;
	CHECK(run(&s, 10, "sd_clock_fix 0") == 10, "every command sets the four");
	sd_clock_fix = 1;
	CHECK(run(&s, 10, "sd_clock_fix 1") <= 1, "kept again");

	printf("== a long run\n");
	unsigned k0 = sd_setup_kept, s0 = sd_setup_set;
	CHECK(run(&s, 10000, "long") == 0, "10,000 good commands: none sets the four");
	CHECK(sd_setup_kept - k0 == 10000 && sd_setup_set == s0, "counted as kept (%u), none set",
	      (unsigned) (sd_setup_kept - k0));

	printf("the four set %u times, kept %u; real calls: clock %u, width %u, sampling %u, "
	       "phase %u, line %u\n", (unsigned) sd_setup_set, (unsigned) sd_setup_kept, s_calls[0],
	       s_calls[1], s_calls[2], s_calls[3], s_calls[4]);
	printf("test_sdmmc_setup: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
