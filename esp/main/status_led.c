/*
 * The status LED: see status_led.h, status_code.h, and STATUS-LED.md beside
 * the images.
 *
 * Normal running: ESP-IDF's RMT driver sends each frame (one reset symbol
 * and 24 bit symbols, status_rmt_frame()), from a task on core 0 that looks
 * at the state every STATUS_TICK_MS and sends a frame when the colour
 * changes, and once a second anyway.
 *
 * The panic handler: no RMT (its interrupt cannot run, and its driver takes
 * locks).  GPIO37 is taken back from the RMT through the GPIO matrix and
 * the frame is bit-banged against the CPU's cycle counter (status_bb.h),
 * from internal RAM, with interrupts off as the panic handler has them.
 * crash.c calls status_led_panic() on the way in and status_led_halt() in
 * place of ESP-IDF's halt loop, so the crash pattern keeps going for as
 * long as the board is halted.
 *
 * No LP-core monitor: GPIO37 is not one of the LP core's pins (GPIO0 to 7),
 * and what it would add is covered by the watchdogs and the beat watch
 * here.  tiny386-idf/research/status-led-lp-core.md has the reasons.
 *
 * Both boards drive the LED through a BSS138 to 5 V levels: the line idles
 * low (about 5 mA from +5V through the 1k into GPIO37 while it does,
 * CHANGES-A2.md section 3); a high idle would be read as the start of a bit.
 */
#ifdef STATUS_LED_GPIO

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "soc/soc_caps.h"

#include "console.h"
#include "status_led.h"

#if STATUS_LED_GPIO < 32 || STATUS_LED_GPIO >= SOC_GPIO_PIN_COUNT
#error "status_led.c bit-bangs through the second GPIO bank (GPIO32 up)"
#endif

volatile uint32_t status_led_beats;

/* The last run's record, in the LP SRAM, which a reset does not clear (as
   console.c's log ring); written on every change, and by the panic path. */
static RTC_NOINIT_ATTR struct status_record s_rec;

static struct status_record s_prev;     /* as this run found it */
static bool s_prev_valid;
static int s_cause;                     /* enum status_cause */
static esp_reset_reason_t s_reset;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct status_state s_state;
static int64_t s_t0_us;
static DRAM_ATTR bool s_started;        /* app_main() reached */
static DRAM_ATTR volatile bool s_panicking;

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_copy;
static uint32_t s_syms[STATUS_RMT_SYMBOLS];
static esp_err_t s_rmt_err = ESP_ERR_INVALID_STATE;
static uint32_t s_sent_rgb = 0xffffffffu;

/* ==== the state ==== */

static uint32_t now_ms(void)
{
	return (uint32_t) ((esp_timer_get_time() - s_t0_us) / 1000);
}

/* with s_lock held: the record follows every change */
static void note_locked(int shown)
{
	status_record_put(&s_rec, s_state.stage, shown);
}

void status_led_stage(int stage)
{
	portENTER_CRITICAL(&s_lock);
	note_locked(status_state_stage(&s_state, stage));
	portEXIT_CRITICAL(&s_lock);
}

void status_led_error(int err, bool on)
{
	portENTER_CRITICAL(&s_lock);
	note_locked(status_state_error(&s_state, err, on));
	portEXIT_CRITICAL(&s_lock);
}

void status_led_fatal(int err)
{
	portENTER_CRITICAL(&s_lock);
	note_locked(status_state_fatal(&s_state, err));
	portEXIT_CRITICAL(&s_lock);
}

/* ==== RMT ==== */

static esp_err_t rmt_start(void)
{
	/* status_rmt_frame() packs the symbols as rmt_symbol_word_t lays them
	   out; a toolchain that put the bit fields elsewhere would light the
	   LED with nonsense, so check once */
	rmt_symbol_word_t w = { .duration0 = 7, .level0 = 1, .duration1 = 19, .level1 = 0 };
	if (sizeof(w) != 4 || w.val != (7u | 1u << 15 | 19u << 16))
		return ESP_ERR_INVALID_STATE;

	const rmt_tx_channel_config_t cfg = {
		.gpio_num = STATUS_LED_GPIO,
		.clk_src = RMT_CLK_SRC_DEFAULT,         /* REF_F80M: 20 MHz is a quarter */
		.resolution_hz = STATUS_RMT_HZ,
		.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL, /* 48: a frame is 25 */
		.trans_queue_depth = 2,
		.flags.init_level = 0,                  /* idle low, as between frames */
	};
	esp_err_t err = rmt_new_tx_channel(&cfg, &s_chan);
	if (err != ESP_OK)
		return err;
	const rmt_copy_encoder_config_t ec = {};       /* no fields */
	err = rmt_new_copy_encoder(&ec, &s_copy);
	if (err == ESP_OK)
		err = rmt_enable(s_chan);
	if (err != ESP_OK) {
		if (s_copy)
			rmt_del_encoder(s_copy);
		rmt_del_channel(s_chan);
		s_copy = NULL;
		s_chan = NULL;
	}
	return err;
}

/* only the LED task (and status_led_init() before it starts) sends */
static void send_rgb(uint32_t rgb)
{
	if (!s_chan)
		return;
	/* the copy encoder reads s_syms while the frame goes out */
	if (rmt_tx_wait_all_done(s_chan, 20) != ESP_OK)
		return;
	status_rmt_frame(status_grb(rgb), s_syms);
	const rmt_transmit_config_t tc = { .loop_count = 0, .flags.eot_level = 0 };
	if (rmt_transmit(s_chan, s_copy, s_syms, sizeof(s_syms), &tc) == ESP_OK)
		s_sent_rgb = rgb;
}

static void led_task(void *arg)
{
	(void) arg;
	struct status_watch watch = { 0 };
	TickType_t wake = xTaskGetTickCount();
	uint32_t sent_at = 0;

	for (;;) {
		vTaskDelayUntil(&wake, pdMS_TO_TICKS(STATUS_TICK_MS));
		uint32_t now = now_ms();
		struct status_state st;

		portENTER_CRITICAL(&s_lock);
		st = s_state;
		portEXIT_CRITICAL(&s_lock);
		bool was = st.active & (1u << STATUS_ERR_STALLED);
		bool stalled = status_watch_step(&watch, status_led_beats, now,
						 st.stage == STATUS_STAGE_RUNNING);
		if (stalled != was) {
			status_led_error(STATUS_ERR_STALLED, stalled);
			if (stalled)
				console_uart_printf("status LED: the emulator has made no progress "
						    "for %d s (yellow 5)", STATUS_STALL_MS / 1000);
			else
				console_uart_printf("status LED: the emulator goes on again");
			portENTER_CRITICAL(&s_lock);
			st = s_state;
			portEXIT_CRITICAL(&s_lock);
		}
		uint32_t rgb = status_pattern(&st, now);
		/* a changed colour at once; the same one again every second, in
		   case a frame was lost */
		if (rgb != s_sent_rgb || now - sent_at >= 1000) {
			send_rgb(rgb);
			sent_at = now;
		}
	}
}

/* ==== start-up ==== */

static int cause_of(esp_reset_reason_t r)
{
	switch (r) {
	case ESP_RST_PANIC:
		return STATUS_CAUSE_CRASH;
	case ESP_RST_INT_WDT:
	case ESP_RST_TASK_WDT:
	case ESP_RST_WDT:
	case ESP_RST_CPU_LOCKUP:
		return STATUS_CAUSE_WATCHDOG;
	case ESP_RST_BROWNOUT:
	case ESP_RST_PWR_GLITCH:
		return STATUS_CAUSE_POWER;
	default:
		return STATUS_CAUSE_NONE;
	}
}

void status_led_init(void)
{
	s_t0_us = esp_timer_get_time();
	s_started = true;

	/* the last run: replayed first after a crash, a watchdog or a power
	   dip; also after RESET pressed on a crash that halted (as the boot
	   console shows that run's lines).  ESP-IDF calls that reset a
	   power-on, so only the record says it followed a crash; whether the
	   LP SRAM keeps the record through the RESET button (CHIP_PU) is not
	   known until it is tried on a board. */
	s_prev = s_rec;
	s_prev_valid = status_record_valid(&s_prev);
	s_reset = esp_reset_reason();
	s_cause = cause_of(s_reset);
	if (s_prev_valid && s_cause == STATUS_CAUSE_NONE &&
	    (s_prev.error == STATUS_ERR_PANIC || s_prev.error == STATUS_ERR_PSRAM))
		s_cause = STATUS_CAUSE_CRASH;
	portENTER_CRITICAL(&s_lock);
	memset(&s_state, 0, sizeof(s_state));
	if (s_prev_valid)
		status_replay_setup(&s_state, s_cause, s_prev.stage, s_prev.error);
	note_locked(status_state_stage(&s_state, STATUS_STAGE_STARTED));
	portEXIT_CRITICAL(&s_lock);

	s_rmt_err = rmt_start();
	if (s_rmt_err != ESP_OK)
		return;         /* said on the console by status_led_report() */
	send_rgb(status_pattern(&s_state, 0));
	/* 4096 bytes, as the project's other tasks: this one calls
	   console_uart_printf() (newlib's vsnprintf) and the RMT driver */
	if (xTaskCreatePinnedToCore(led_task, "status_led", 4096, NULL, 1, NULL, 0) != pdPASS) {
		/* no task to change it: dark, as status_led_report() says, not
		   the first colour for good */
		send_rgb(0);
		s_rmt_err = ESP_ERR_NO_MEM;
	}
}

void status_led_report(void)
{
	if (s_rmt_err != ESP_OK) {
		console_printf(CON_WARN, "Status LED (GPIO%d): could not be started (%s); it "
			       "stays dark but for a crash.", STATUS_LED_GPIO,
			       esp_err_to_name(s_rmt_err));
		return;
	}
	console_printf(CON_INFO, "Status LED on GPIO%d: colours and blinks as in "
		       "STATUS-LED.md.", STATUS_LED_GPIO);
	if (s_prev_valid && s_state.replay_end_ms)
		console_printf(CON_INFO, "Status LED: first %d white flash%s (the last run "
			       "ended with %s), then that run's code: it had reached \"%s\"%s%s.",
			       s_cause, s_cause == 1 ? "" : "es",
			       s_cause == STATUS_CAUSE_CRASH ? "a crash" :
			       s_cause == STATUS_CAUSE_WATCHDOG ? "a watchdog reset" : "a power dip",
			       status_stage_name(s_prev.stage),
			       s_prev.error ? " and showed " : "",
			       s_prev.error ? status_error_name(s_prev.error) : "");
}

/* ==== the panic handler ==== */

#define STATUS_BB_ATTR IRAM_ATTR
#define STATUS_BB_NOW() ((uint32_t) esp_cpu_get_cycle_count())
#define STATUS_BB_HIGH() (GPIO.out1_w1ts.val = 1u << (STATUS_LED_GPIO - 32))
#define STATUS_BB_LOW() (GPIO.out1_w1tc.val = 1u << (STATUS_LED_GPIO - 32))
#include "status_bb.h"

static DRAM_ATTR struct status_bb s_bb;
static DRAM_ATTR uint32_t s_cpu_us;
static DRAM_ATTR uint32_t s_panic_cycles;

/* An empty one for boards with no line out to quieten (i2s.c has the DOS
   board's). */
void __attribute__((weak)) IRAM_ATTR audio_panic_quiet(void)
{
}

/* from ESP-IDF's halt stub, where only internal RAM may be reached */
bool IRAM_ATTR status_led_panicking(void)
{
	return s_panicking;
}

/* GPIO37 from the RMT's signal to the plain GPIO output, through the IO MUX's
   GPIO function and the matrix, output enabled */
static IRAM_ATTR void bb_take_pin(void)
{
	gpio_ll_set_level(&GPIO, STATUS_LED_GPIO, 0);
	gpio_ll_func_sel(&GPIO, STATUS_LED_GPIO, PIN_FUNC_GPIO);
	gpio_ll_set_output_signal_matrix_source(&GPIO, STATUS_LED_GPIO, SIG_GPIO_OUT_IDX, false);
	gpio_ll_set_output_enable_ctrl(&GPIO, STATUS_LED_GPIO, false, false);
	gpio_ll_output_enable(&GPIO, STATUS_LED_GPIO);
}

#ifdef TINY386_CRASH_TEST
/* crash_test = 5 (esp_main.c): the panic path taken as for a panic before
   app_main(), yellow 1, at the clock the caller sets */
void status_led_test_early(void)
{
	s_started = false;
}
#endif

void IRAM_ATTR status_led_panic(void)
{
	if (s_panicking)
		return;
	s_panicking = true;
	bool early = !s_started;

	status_record_put(&s_rec, s_state.stage, early ? STATUS_ERR_PSRAM : STATUS_ERR_PANIC);
	/* the CPU clock as ESP-IDF last set it, which before app_main() may be
	   the second-stage bootloader's */
	s_cpu_us = esp_rom_get_cpu_ticks_per_us();
	if (s_cpu_us < 10 || s_cpu_us > 1000)
		s_cpu_us = 40;
	status_bb_timing(&s_bb, s_cpu_us);
	bb_take_pin();
	status_bb_frame(status_grb(status_panic_pattern(early, 0)), &s_bb);
	s_panic_cycles = (uint32_t) esp_cpu_get_cycle_count();
	audio_panic_quiet();
}

void IRAM_ATTR status_led_halt(void)
{
	bool early = !s_started;
	const uint32_t per_ms = s_cpu_us * 1000;
	uint32_t last = s_panic_cycles, part = 0, ms = 0, shown_ms = 0;
	uint32_t shown = status_panic_pattern(early, 0);

	for (;;) {
		uint32_t now = (uint32_t) esp_cpu_get_cycle_count();
		part += now - last;
		last = now;
		while (part >= per_ms) {
			part -= per_ms;
			ms++;
		}
		uint32_t rgb = status_panic_pattern(early, ms);
		if (rgb != shown || ms - shown_ms >= 1000) {
			status_bb_frame(status_grb(rgb), &s_bb);
			shown = rgb;
			shown_ms = ms;
		}
	}
}

#endif /* STATUS_LED_GPIO */
