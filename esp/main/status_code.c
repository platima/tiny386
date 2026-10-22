/*
 * The status LED's codes and patterns: see status_code.h, and STATUS-LED.md
 * beside the images for the table as Plat reads it.
 *
 * Built into the firmware (the panic handler's loop calls the functions
 * marked STATUS_IRAM, so they and their tables are in internal RAM) and on
 * a PC for tests/host/test_status.c (STATUS_HOST).
 */
#include <stddef.h>
#include <stdint.h>

#include "status_code.h"

/* ==== colours: 0x00RRGGBB, at full scale ====
 * v3.3 to v3.6 had them at about a quarter of full brightness (white
 * 0x282828, blue 0x00003c, cyan 0x002d2d, magenta 0x320032, green
 * 0x003200, red 0x460000, yellow 0x3c2300), far too bright on the
 * Korvo-1.  Here the same colours are all multiplied by 255/70, so that
 * red, the brightest, is at full scale and each colour keeps its hue and
 * its share of light against the others; the brightness level brings them
 * down where they become a frame (status_frame_grb()).  Level 70 gives
 * v3.6's values back exactly (tests/host/test_status.c). */
#define BLACK   0x000000u
#define WHITE   0x929292u
#define BLUE    0x0000dbu
#define CYAN    0x00a4a4u
#define MAGENTA 0xb600b6u
#define GREEN   0x00b600u
#define RED     0xff0000u
#define YELLOW  0xdb8000u

static const STATUS_DRAM uint32_t stage_rgb[STATUS_STAGE_COUNT] = {
	[STATUS_STAGE_NONE] = BLACK,
	[STATUS_STAGE_STARTED] = WHITE,
	[STATUS_STAGE_SCREEN] = BLUE,
	[STATUS_STAGE_CARD] = CYAN,
	[STATUS_STAGE_INI] = MAGENTA,
	[STATUS_STAGE_BIOS] = GREEN,
	[STATUS_STAGE_RUNNING] = GREEN,
	[STATUS_STAGE_PC_OFF] = WHITE,
};

static const STATUS_DRAM struct {
	uint32_t rgb;
	uint8_t blinks;
} err_look[STATUS_ERR_COUNT] = {
	[STATUS_OK] = { BLACK, 0 },
	[STATUS_ERR_NO_CARD] = { RED, 1 },
	[STATUS_ERR_NO_INI] = { RED, 2 },
	[STATUS_ERR_BIOS] = { RED, 3 },
	[STATUS_ERR_INI] = { RED, 4 },
	[STATUS_ERR_STOPPED] = { RED, 5 },
	[STATUS_ERR_PSRAM] = { YELLOW, 1 },
	[STATUS_ERR_HDMI_CHIP] = { YELLOW, 2 },
	[STATUS_ERR_NO_MONITOR] = { YELLOW, 3 },
	[STATUS_ERR_KEYBOARD] = { YELLOW, 4 },
	[STATUS_ERR_STALLED] = { YELLOW, 5 },
	[STATUS_ERR_PANIC] = { RED, 0 },        /* red and white by turns */
};

/* a blink: on, then off; after the last, a longer pause */
#define BLINK_ON_MS     300
#define BLINK_OFF_MS    300
#define BLINK_PAUSE_MS  1200
/* the crash: red and white by turns */
#define PANIC_HALF_MS   250
#define PANIC_CYCLE_MS  2000
/* the PC running: dark for a moment every 2 s */
#define BEAT_PERIOD_MS  2000
#define BEAT_DARK_MS    100
/* the PC switched off: slow white */
#define OFF_HALF_MS     1000
/* the replay's lead-in: one quick white flash per cause, then a gap */
#define LEAD_ON_MS      150
#define LEAD_OFF_MS     150
#define LEAD_GAP_MS     600
#define REPLAY_STAGE_MS 2000
#define REPLAY_CYCLES   2

static const char *const stage_names[STATUS_STAGE_COUNT] = {
	"not started", "started", "screen up", "card mounted", "ini found",
	"BIOS loaded", "PC running", "PC switched off",
};

static const char *const error_names[STATUS_ERR_COUNT] = {
	"none", "no card", "no ini file", "a BIOS file missing",
	"problems in the ini", "tiny386 stopped",
	"stopped before tiny386 started (PSRAM not found, or failing its test?)",
	"the MS7210 not found or not answering", "no monitor", "keyboard path not working",
	"the emulator stalled", "a crash",
};

const char *status_stage_name(int stage)
{
	return stage >= 0 && stage < STATUS_STAGE_COUNT ? stage_names[stage] : "?";
}

const char *status_error_name(int err)
{
	return err >= 0 && err < STATUS_ERR_COUNT ? error_names[err] : "?";
}

STATUS_IRAM uint32_t status_stage_rgb(int stage)
{
	return stage >= 0 && stage < STATUS_STAGE_COUNT ? stage_rgb[stage] : BLACK;
}

STATUS_IRAM uint32_t status_error_rgb(int err)
{
	return err > 0 && err < STATUS_ERR_COUNT ? err_look[err].rgb : BLACK;
}

STATUS_IRAM int status_error_blinks(int err)
{
	return err > 0 && err < STATUS_ERR_COUNT ? err_look[err].blinks : 0;
}

STATUS_IRAM uint32_t status_error_cycle_ms(int err)
{
	if (err == STATUS_ERR_PANIC)
		return PANIC_CYCLE_MS;
	int n = status_error_blinks(err);
	if (n <= 0)
		return 0;
	return (uint32_t) n * (BLINK_ON_MS + BLINK_OFF_MS) + BLINK_PAUSE_MS;
}

STATUS_IRAM uint32_t status_error_pattern(int err, uint32_t ms)
{
	if (err == STATUS_ERR_PANIC)
		return (ms / PANIC_HALF_MS) & 1 ? WHITE : RED;
	uint32_t cycle = status_error_cycle_ms(err);
	if (!cycle)
		return BLACK;
	uint32_t t = ms % cycle;
	uint32_t i = t / (BLINK_ON_MS + BLINK_OFF_MS);
	if (i < (uint32_t) status_error_blinks(err) &&
	    t % (BLINK_ON_MS + BLINK_OFF_MS) < BLINK_ON_MS)
		return status_error_rgb(err);
	return BLACK;
}

STATUS_IRAM uint32_t status_panic_pattern(bool early, uint32_t ms)
{
	return status_error_pattern(early ? STATUS_ERR_PSRAM : STATUS_ERR_PANIC, ms);
}

/* ==== brightness ==== */

/* The level every frame is sent at: STATUS_BRIGHTNESS_DEFAULT from the
   image's own data (so also for a panic before app_main() and for the
   replay after a reset) until the ini sets it.  In internal RAM, as the
   panic path reads it; one aligned word, written by the ini's reader and
   read by the LED task and the panic path. */
static STATUS_DRAM volatile uint32_t s_level = STATUS_BRIGHTNESS_DEFAULT;

STATUS_IRAM uint32_t status_scale(uint32_t rgb, uint32_t level)
{
	uint32_t out = 0;

	if (level > STATUS_BRIGHTNESS_MAX)
		level = STATUS_BRIGHTNESS_MAX;
	for (int shift = 0; shift <= 16; shift += 8) {
		uint32_t c = (rgb >> shift) & 0xff;
		/* c * level / 255 to the nearest step, in 32 bits (at most
		   255 * 255 + 127): no 64-bit division, which the panic path
		   would have to reach in flash */
		uint32_t v = (c * level + 127) / 255;
		/* a lit channel stays lit, so that no colour turns into
		   another at a low level (yellow into red) */
		if (!v && c && level)
			v = 1;
		out |= v << shift;
	}
	return out;
}

void status_brightness_set(int level)
{
	s_level = level < 0 ? 0 : level > STATUS_BRIGHTNESS_MAX ? STATUS_BRIGHTNESS_MAX :
		  (uint32_t) level;
}

uint32_t status_brightness(void)
{
	return s_level;
}

STATUS_IRAM uint32_t status_frame_grb(uint32_t rgb)
{
	return status_grb(status_scale(rgb, s_level));
}

/* ==== the state ==== */

static uint32_t lead_ms(int cause)
{
	return (uint32_t) cause * (LEAD_ON_MS + LEAD_OFF_MS) + LEAD_GAP_MS;
}

void status_replay_setup(struct status_state *s, int cause, int stage, int error)
{
	s->prev_cause = cause;
	s->prev_stage = stage;
	s->prev_error = error;
	s->replay_end_ms = 0;
	if (cause <= STATUS_CAUSE_NONE || cause > STATUS_CAUSE_POWER)
		return;
	uint32_t body = error ? REPLAY_CYCLES * status_error_cycle_ms(error) : REPLAY_STAGE_MS;
	s->replay_end_ms = lead_ms(cause) + body;
}

static uint32_t replay_pattern(const struct status_state *s, uint32_t now)
{
	uint32_t lead = lead_ms(s->prev_cause);
	if (now < lead) {
		uint32_t i = now / (LEAD_ON_MS + LEAD_OFF_MS);
		if (i < s->prev_cause && now % (LEAD_ON_MS + LEAD_OFF_MS) < LEAD_ON_MS)
			return WHITE;
		return BLACK;
	}
	if (s->prev_error)
		return status_error_pattern(s->prev_error, now - lead);
	return status_stage_rgb(s->prev_stage);
}

int status_shown_error(const struct status_state *s)
{
	if (s->fatal)
		return s->fatal;
	for (int e = 1; e < STATUS_ERR_COUNT; e++)
		if (s->active & (1u << e))
			return e;
	return STATUS_OK;
}

int status_state_stage(struct status_state *s, int stage)
{
	if (stage >= 0 && stage < STATUS_STAGE_COUNT)
		s->stage = stage;
	return status_shown_error(s);
}

int status_state_error(struct status_state *s, int err, bool on)
{
	if (err > 0 && err < STATUS_ERR_COUNT) {
		if (on)
			s->active |= 1u << err;
		else
			s->active &= ~(1u << err);
	}
	return status_shown_error(s);
}

int status_state_fatal(struct status_state *s, int err)
{
	if (err > 0 && err < STATUS_ERR_COUNT && !s->fatal)
		s->fatal = err;
	return status_shown_error(s);
}

uint32_t status_pattern(const struct status_state *s, uint32_t now_ms)
{
	if (s->replay_end_ms && now_ms < s->replay_end_ms)
		return replay_pattern(s, now_ms);
	int e = status_shown_error(s);
	if (e)
		return status_error_pattern(e, now_ms);
	switch (s->stage) {
	case STATUS_STAGE_RUNNING:
		return now_ms % BEAT_PERIOD_MS < BEAT_DARK_MS ? BLACK : GREEN;
	case STATUS_STAGE_PC_OFF:
		return (now_ms / OFF_HALF_MS) & 1 ? BLACK : WHITE;
	default:
		return status_stage_rgb(s->stage);
	}
}

/* ==== no progress ==== */

bool status_watch_step(struct status_watch *w, uint32_t beats, uint32_t now_ms,
		       bool running)
{
	if (!running || beats != w->last_beats) {
		w->last_beats = beats;
		w->last_change_ms = now_ms;
		w->stalled = false;
	} else if ((uint32_t) (now_ms - w->last_change_ms) >= STATUS_STALL_MS) {
		w->stalled = true;
	}
	return w->stalled;
}

/* ==== the record ==== */

static STATUS_IRAM uint32_t record_word(const struct status_record *r)
{
	return r->stage | (uint32_t) r->error << 8 |
	       (uint32_t) r->spare[0] << 16 | (uint32_t) r->spare[1] << 24;
}

static STATUS_IRAM uint32_t record_check(const struct status_record *r)
{
	return (r->magic ^ 0x13572468u) ^ ~(record_word(r) * 2654435761u);
}

STATUS_IRAM void status_record_put(struct status_record *r, int stage, int error)
{
	r->magic = STATUS_RECORD_MAGIC;
	r->stage = stage;
	r->error = error;
	r->spare[0] = r->spare[1] = 0;
	r->check = record_check(r);
}

bool status_record_valid(const struct status_record *r)
{
	return r->magic == STATUS_RECORD_MAGIC && r->check == record_check(r) &&
	       r->stage < STATUS_STAGE_COUNT && r->error < STATUS_ERR_COUNT;
}

/* ==== the wire ==== */

#define PS_TO_RMT(ps) ((uint32_t) (((uint64_t) (ps) * STATUS_RMT_HZ + 500000000000ull) / \
				   1000000000000ull))

static uint32_t sym(uint32_t d0, int l0, uint32_t d1, int l1)
{
	return (d0 & 0x7fff) | (uint32_t) (l0 & 1) << 15 |
	       (d1 & 0x7fff) << 16 | (uint32_t) (l1 & 1) << 31;
}

void status_rmt_frame(uint32_t grb, uint32_t out[STATUS_RMT_SYMBOLS])
{
	const uint32_t reset = (uint32_t) ((uint64_t) STATUS_RESET_US * STATUS_RMT_HZ / 1000000u);
	const uint32_t t0h = PS_TO_RMT(STATUS_T0H_PS), t0l = PS_TO_RMT(STATUS_T0_PS) - t0h;
	const uint32_t t1h = PS_TO_RMT(STATUS_T1H_PS), t1l = PS_TO_RMT(STATUS_T1_PS) - t1h;

	out[0] = sym(reset / 2, 0, reset - reset / 2, 0);
	for (int i = 0; i < 24; i++) {
		int one = (grb >> (23 - i)) & 1;
		out[1 + i] = one ? sym(t1h, 1, t1l, 0) : sym(t0h, 1, t0l, 0);
	}
}

STATUS_IRAM void status_bb_timing(struct status_bb *t, uint32_t cycles_per_us)
{
	/* to the nearest cycle; no 64-bit division, which the panic handler
	   would have to reach in flash (the panic before app_main() has no
	   status_led_init() to have done it) */
	t->t0h = (STATUS_T0H_PS / 100 * cycles_per_us + 5000) / 10000;
	t->t0 = (STATUS_T0_PS / 100 * cycles_per_us + 5000) / 10000;
	t->t1h = (STATUS_T1H_PS / 100 * cycles_per_us + 5000) / 10000;
	t->t1 = (STATUS_T1_PS / 100 * cycles_per_us + 5000) / 10000;
	t->reset = STATUS_RESET_US * cycles_per_us;
}
