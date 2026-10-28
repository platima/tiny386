/*
 * [esp] profile = 1 (v5.1, s31/PROFILE.md): where core 1's time goes on the
 * board, stalls on flash and PSRAM included.
 *
 * About PROFILE_HZ times a second, a timer interrupt on core 0 reads core
 * 1's program counter from the bus monitor's PC record register
 * (BUS_MONITOR_CORE_1_RCD_PDEBUGPC_REG: ESP-IDF's start-up has it recording
 * on both cores, cpu_start.c for core 1 and debug_assist.c with
 * CONFIG_ESP_SYSTEM_HW_PC_RECORD, as v5.0 already builds), and the guest's
 * state from the CPU's fields (prof_cpu.h), and counts both (prof_hist.c).
 * The gaps are drawn at random between half and one and a half times the
 * mean, so that nothing periodic on core 1 lines up with them.  Core 1 runs
 * none of this: its time is seen as it is.  Every PROFILE_CACHE_US the
 * same interrupt adds up the cache's access counters (esp_cache_cnt.h):
 * each core's instruction and data traffic into the flash and PSRAM cache.
 *
 * Every PROFILE_WRITE_S seconds, on Ctrl+Alt+F8, and when the PC is
 * switched off, core 1 writes the counts so far to PROFILE.TXT beside the
 * ini, between two steps of the PC (as the Ctrl+Alt keys' settings are
 * saved), with the sampling paused: the guest waits for the write, and the
 * console says how long it took.  Ctrl+Alt+F8 also writes the same text to
 * PROF01.TXT, PROF02.TXT and so on (to PROF99.TXT), so that two presses
 * mark a stretch of the run: profile_report.py's --since takes the earlier
 * file's counts from the later's.  Only those files (and PROFILE.NEW, the
 * next PROFILE.TXT until it is whole) are written, through FatFs on
 * /sdcard: never a partition the guest has whole.  Each file names this
 * boot's run (a random word), so that --since can tell two boots apart.
 *
 * With profile = 0 (the default) profile_setup() returns at once: no table,
 * no timer, no interrupt, no key, and core 1's loop is v5.0's.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "soc/soc_caps.h"

#include "console.h"
#include "kbd_input.h"
#include "speed.h"
#include "profile.h"

#if CONFIG_IDF_TARGET_ESP32S31
#define PROFILE_SUPPORTED 1
#endif

#ifdef PROFILE_SUPPORTED
#include "esp_cpu.h"
#include "esp_cache_cnt.h"
#include "driver/gptimer.h"
#include "soc/soc.h"
#include "soc/bus_monitor_reg.h"

/* the sample path runs in core 0's interrupt: internal RAM, unless the
   build says otherwise (prof_hist.c does the same) */
#ifdef PROFILE_IN_FLASH
#define PROF_ISR_ATTR
#else
#define PROF_ISR_ATTR IRAM_ATTR
#endif
#include "prof_cpu.h"
#include "prof_hist.h"

#define PROFILE_HZ 2000                 /* samples a second, on average */
#define PROFILE_PERIOD_US (1000000 / PROFILE_HZ)
#define PROFILE_WRITE_S 60              /* PROFILE.TXT this often */
#define PROFILE_CACHE_US 10000          /* the cache counters added up this often */
#define PROFILE_KEEP_PSRAM (256 * 1024) /* PSRAM left free beside the tables */
#define PROFILE_BUF 4096                /* the file is written through this */

/* the code's regions, from ESP-IDF's linker script; internal RAM from its
   start, so that the vector table and the interrupt entry and exit below
   _iram_text_start count there, not as "other" (the v5.1 sampler review's
   R7) */
extern char _iram_start[], _iram_text_end[], _text_start[], _text_end[];

static struct prof s_prof;              /* the counts' heads (.bss); the tables in PSRAM */
static struct cpui386_prof_fields s_f;  /* where the guest's state is */
static gptimer_handle_t s_timer;
static char *s_buf;
static bool s_on;
static int s_armed;                     /* the PC runs: samples count */
static int s_pause;                     /* core 1 writes the file: samples do not */
static int s_busy;                      /* core 0's interrupt is in a sample */
static int s_key;                       /* Ctrl+Alt+F8 asked for the next write */
static uint32_t s_rng = 0x2545f491u;
static uint64_t s_next_write_us, s_next_cache_us;       /* on the timer's count */
static int64_t s_t0_us;                 /* sampling started (esp_timer) */
static uint64_t s_paused_us;
static uint32_t s_last_write_us, s_files;
static int s_fast_cpu;
static int s_ncache;
static const char *s_ini_path;
static char s_path[112];
static char s_dir[96];                  /* the ini's folder, where the files go */
static unsigned s_keys;                 /* PROFnn.TXT written so far */
static unsigned s_kib;
static uint32_t s_run;                  /* this boot's run, in every file */

static inline uint32_t PROF_ISR_ATTR prof_rng(void)
{
	uint32_t x = s_rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return s_rng = x;
}

/* the cache counters since the last call, added up, then cleared */
static void PROF_ISR_ATTR prof_cache_read(void)
{
	for (int u = 0; u < s_ncache; u++) {
		esp_cache_cnt_data_t d;
		if (esp_cache_cnt_get((size_t) u, &d) != ESP_OK)
			continue;
		uint32_t v[PROF_CACHE_COUNTERS] = {
			d.accesses, d.stall_events, d.conflicts, d.line_fills, d.writebacks
		};
		prof_cache_add(&s_prof, u, v);
	}
	esp_cache_cnt_clear();
}

static bool PROF_ISR_ATTR prof_alarm(gptimer_handle_t timer,
				     const gptimer_alarm_event_data_t *e, void *ctx)
{
	(void) ctx;
	uint32_t c0 = (uint32_t) esp_cpu_get_cycle_count();
	/* microseconds (the timer counts at 1 MHz): the count now, not the
	   alarm that fired, so that an interrupt held off for a while does not
	   set alarms already past and take a burst of samples (the v5.1
	   sampler review's R6) */
	uint64_t now = e->count_value;
	gptimer_alarm_config_t next = {
		.alarm_count = now + PROFILE_PERIOD_US / 2 + prof_rng() % PROFILE_PERIOD_US,
	};
	gptimer_set_alarm_action(timer, &next);
	__atomic_store_n(&s_busy, 1, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) &&
	    !__atomic_load_n(&s_pause, __ATOMIC_SEQ_CST)) {
		/* core 1's PC and the guest at (nearly) the same moment */
		uint32_t pc = REG_READ(BUS_MONITOR_CORE_1_RCD_PDEBUGPC_REG);
		struct prof_guest g;
		prof_read_guest(&s_f, &g);
		prof_sample(&s_prof, pc, &g);
		if (now >= s_next_cache_us) {
			prof_cache_read();
			s_next_cache_us = now + PROFILE_CACHE_US;
		}
		if (now >= s_next_write_us) {
			s_next_write_us = now + PROFILE_WRITE_S * 1000000ull;
			speed_ask_save(SPEED_SAVE_PROFILE);
		}
		prof_cost_add(&s_prof, (uint32_t) esp_cpu_get_cycle_count() - c0);
	}
	__atomic_store_n(&s_busy, 0, __ATOMIC_RELEASE);
	return false;
}

/* Ctrl+Alt+F8, from the USB task: PROFILE.TXT at the next step */
static void profile_hotkey(void)
{
	__atomic_store_n(&s_key, 1, __ATOMIC_RELEASE);
	speed_ask_save(SPEED_SAVE_PROFILE);
}

bool profile_setup(int on, const char *ini_path, const char *ini_dir, int fast_cpu)
{
	if (!on)
		return false;
	if (strncmp(ini_dir, "/sdcard", 7) != 0 || (ini_dir[7] != 0 && ini_dir[7] != '/')) {
		console_printf(CON_WARN, "profile = 1 is ignored: PROFILE.TXT goes beside the ini, "
			       "and %s is not on the card's first partition (/sdcard).", ini_dir);
		return false;
	}
	snprintf(s_path, sizeof(s_path), "%s/PROFILE.TXT", ini_dir);
	snprintf(s_dir, sizeof(s_dir), "%s", ini_dir);
	s_ini_path = ini_path;
	s_fast_cpu = fast_cpu;

	/* the tables, in PSRAM, smaller if there is not room for them and
	   PROFILE_KEEP_PSRAM besides (the VGA's 16-colour cache comes from
	   there at a mode change: 150 KB at 640x480) */
	static const struct prof_sizes sizes[2] = {
		{ 8192, 2048, 4096, 512 },
		{ 4096, 1024, 2048, 256 },
	};
	size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	void *mem = NULL;
	const struct prof_sizes *sz = NULL;
	for (int i = 0; i < 2 && !mem; i++) {
		size_t need = prof_mem_bytes(&sizes[i]) + PROFILE_BUF;
		if (big >= need + PROFILE_KEEP_PSRAM) {
			mem = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
			sz = &sizes[i];
			s_kib = (unsigned) ((need + 1023) / 1024);
		}
	}
	if (!mem) {
		console_printf(CON_WARN, "profile = 1 is ignored: no room in PSRAM for its tables "
			       "(largest free block %u KiB).", (unsigned) (big / 1024));
		return false;
	}
	prof_init(&s_prof, mem, sz);
	s_buf = (char *) mem + prof_mem_bytes(sz);
	prof_set_range(&s_prof, PROF_REGION_IRAM, (uint32_t) (uintptr_t) _iram_start,
		       (uint32_t) (uintptr_t) _iram_text_end);
	s_run = esp_random();
	prof_set_range(&s_prof, PROF_REGION_FLASH, (uint32_t) (uintptr_t) _text_start,
		       (uint32_t) (uintptr_t) _text_end);
	prof_set_range(&s_prof, PROF_REGION_ROM, SOC_IROM_MASK_LOW, SOC_IROM_MASK_HIGH);

	/* the cache counters: every unit the chip has (S31: each core's
	   instruction and data buses), counting from now */
	s_ncache = (int) esp_cache_cnt_num_units();
	if (s_ncache > PROF_CACHE_UNITS)
		s_ncache = PROF_CACHE_UNITS;
	for (int u = 0; u < s_ncache; u++) {
		esp_cache_cnt_unit_info_t info;
		esp_cache_cnt_data_t d;
		if (esp_cache_cnt_get_unit_info((size_t) u, &info) != ESP_OK ||
		    esp_cache_cnt_get((size_t) u, &d) != ESP_OK)
			continue;
		/* the valid bits are in the counters' order (prof_hist.h) */
		prof_cache_describe(&s_prof, u, info.name, info.core_id,
				    info.traffic_type == CACHE_PROFILE_TRAFFIC_INST ? 'i' :
				    info.traffic_type == CACHE_PROFILE_TRAFFIC_DATA ? 'd' : 'u',
				    d.valid_mask & 0x1f);
	}
	if (s_ncache)
		esp_cache_cnt_start();

	/* the timer: made here, so that its interrupt is on this core (core 0,
	   app_main's), with the drawing and USB; it counts from now, and the
	   samples from profile_start() */
	gptimer_config_t tc = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000,
	};
	gptimer_event_callbacks_t cbs = { .on_alarm = prof_alarm };
	gptimer_alarm_config_t first = { .alarm_count = PROFILE_PERIOD_US };
	esp_err_t err = gptimer_new_timer(&tc, &s_timer);
	if (err == ESP_OK)
		err = gptimer_register_event_callbacks(s_timer, &cbs, NULL);
	if (err == ESP_OK)
		err = gptimer_set_alarm_action(s_timer, &first);
	if (err == ESP_OK)
		err = gptimer_enable(s_timer);
	if (err == ESP_OK)
		err = gptimer_start(s_timer);
	if (err != ESP_OK) {
		console_printf(CON_WARN, "profile = 1 is ignored: its timer could not be started (%s).",
			       esp_err_to_name(err));
		heap_caps_free(mem);
		return false;
	}
	kbd_input_set_hotkey(HID_KEY_F8, profile_hotkey);
	s_on = true;
	console_printf(CON_WARN, "Profile: on ([esp] profile = 1). Core 0 samples core 1 about %d "
		       "times a second while the PC runs, and writes %s every %d s, on Ctrl+Alt+F8 "
		       "(and PROF01.TXT, PROF02.TXT, ... then) and when the PC is switched off; the "
		       "guest waits while it is written. Tables: %u KiB of PSRAM.", PROFILE_HZ, s_path,
		       PROFILE_WRITE_S, s_kib);
	return true;
}

void profile_start(void *cpu)
{
	if (!s_on)
		return;
	uint64_t now = 0;
	cpui386_prof_fields(cpu, &s_f);
	gptimer_get_raw_count(s_timer, &now);
	s_next_cache_us = now + PROFILE_CACHE_US;
	s_next_write_us = now + PROFILE_WRITE_S * 1000000ull;
	if (s_ncache)
		esp_cache_cnt_clear();
	s_t0_us = esp_timer_get_time();
	/* a Ctrl+Alt+F8 from the boot console's time is forgotten, as
	   speed_forget_requests() forgets its request */
	__atomic_store_n(&s_key, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&s_armed, 1, __ATOMIC_RELEASE);
	console_printf(CON_INFO, "Profile: sampling from now.");
}

bool profile_on(void)
{
	return s_on;
}

/* the ELF's SHA-256, all 64 hex digits, from the app's description, where
   esptool put it: esp_app_get_elf_sha256() gives only
   CONFIG_APP_RETRIEVE_LEN_ELF_SHA of them, 9 in this build (the v5.1
   sampler review's R1).  Read through volatile, as ESP-IDF reads it: the
   compiler knows those bytes only as the zeros esptool fills in later. */
static void prof_elf_sha256(char out[65])
{
	static const char hex[] = "0123456789abcdef";
	const volatile uint8_t *s = (const volatile uint8_t *) esp_app_get_description()->app_elf_sha256;
	for (int i = 0; i < 32; i++) {
		uint8_t c = s[i];
		out[2 * i] = hex[c >> 4];
		out[2 * i + 1] = hex[c & 15];
	}
	out[64] = 0;
}

void profile_write(const char *why)
{
	if (!s_on || !__atomic_load_n(&s_armed, __ATOMIC_ACQUIRE))
		return;
	int64_t t0 = esp_timer_get_time();
	/* the interrupt out of the tables first (a sample takes it a few
	   microseconds) */
	__atomic_store_n(&s_pause, 1, __ATOMIC_SEQ_CST);
	while (__atomic_load_n(&s_busy, __ATOMIC_SEQ_CST))
		;
	bool key = __atomic_exchange_n(&s_key, 0, __ATOMIC_ACQ_REL);
	bool end = why != NULL;
	if (!why)
		why = key ? "key" : "timer";
	s_files++;

	char elf[65];
	prof_elf_sha256(elf);
	uint64_t insns = 0, run_us = 0, halt_us = 0;
	speed_totals(&insns, &run_us, &halt_us);
	struct prof_meta m = {
		.firmware = esp_app_get_description()->version,
#ifdef BOARD_NAME
		.board = BOARD_NAME,
#endif
		.idf = esp_get_idf_version(),
		.elf_sha256 = elf,
		.sampler = "core0-pc-record",
		.ini = s_ini_path,
		.why = why,
		.fast_cpu = s_fast_cpu,
		.rate_hz = PROFILE_HZ,
		.file_no = s_files,
		.run = s_run,
		.cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
		.uptime_us = (uint64_t) t0,
		.sampling_us = (uint64_t) (t0 - s_t0_us),
		.paused_us = s_paused_us,
		.last_write_us = s_last_write_us,
		.guest_insns = insns,
		.run_us = run_us,
		.halt_us = halt_us,
	};
	/* PROFILE.TXT is written as PROFILE.NEW and put in its place only once
	   it is whole, so that a full card or a power cut in the write leaves
	   the last good one; Ctrl+Alt+F8's PROFnn.TXT (the same text) takes
	   its number, and the file its file number, only when it was written
	   whole, and a file that failed is taken away (the v5.1 sampler
	   review's R2).  FatFs renames onto no name that is there, so the old
	   PROFILE.TXT goes first. */
	char tmp[112], path2[128] = "";
	unsigned keyno = s_keys < 99 ? s_keys + 1 : 99;
	snprintf(tmp, sizeof(tmp), "%s/PROFILE.NEW", s_dir);
	FILE *f = fopen(tmp, "w"), *f2 = NULL;
	if (key) {
		snprintf(path2, sizeof(path2), "%s/PROF%02u.TXT", s_dir, keyno);
		if (f)
			f2 = fopen(path2, "w");
	}
	bool opened = f && (f2 || !key);
	long bytes = opened ? prof_write2(&s_prof, &m, f, f2, s_buf, PROFILE_BUF) : -1;
	if (f && fclose(f) != 0)
		bytes = -1;
	if (f2 && fclose(f2) != 0)
		bytes = -1;
	bool renamed = false;
	if (bytes >= 0) {
		remove(s_path);
		renamed = rename(tmp, s_path) == 0;
		if (key)
			s_keys = keyno;
	} else {
		if (f)
			remove(tmp);
		if (f2)
			remove(path2);
		s_files--;
	}
	/* what the console lines say, read before the sampling goes on (the
	   v5.1 sampler review's R4: the interrupt changes them again after) */
	uint32_t samples = s_prof.samples, places = s_prof.host.used + s_prof.coarse.used;
	uint32_t other = s_prof.region[PROF_REGION_OTHER];
	uint32_t host_lost = s_prof.host.lost, coarse_lost = s_prof.coarse.lost;
	uint32_t guest_lost = s_prof.guest.lost, ops_lost = s_prof.ops.lost;
	int64_t t1 = esp_timer_get_time();
	s_last_write_us = (uint32_t) (t1 - t0);
	s_paused_us += (uint64_t) (t1 - t0);
	if (end)
		__atomic_store_n(&s_armed, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&s_pause, 0, __ATOMIC_SEQ_CST);

	unsigned ms = (unsigned) ((t1 - t0 + 500) / 1000);
	if (bytes < 0) {
		console_printf(CON_ERROR, "Profile: %s%s%s could not be written (%s; %u ms); the last "
			       "PROFILE.TXT is kept.", s_path, key ? " and " : "", path2,
			       opened ? "a write failed" : "it could not be opened", ms);
		return;
	}
	if (!renamed) {
		console_printf(CON_ERROR, "Profile: written to %s%s%s (%s, file %u), but it could not be "
			       "renamed %s (%u ms).", tmp, key ? " and " : "", path2, why,
			       (unsigned) s_files, s_path, ms);
		return;
	}
	console_printf(CON_INFO, "Profile: %s%s%s written (%s, file %u): %" PRIu32 " samples, %ld "
		       "bytes, in %u ms, the guest waiting.", s_path, key ? " and " : "", path2, why,
		       (unsigned) s_files, samples, bytes, ms);
	/* the PC record moving, and pointing at code: else the host half of
	   the file says nothing (the guest half still holds) */
	if (samples >= 1000 && places < 8)
		console_printf(CON_WARN, "Profile: core 1's PC record gave only %u places in %" PRIu32
			       " samples: it does not seem to follow core 1.", (unsigned) places, samples);
	else if (samples >= 1000 && (uint64_t) other * 10 > samples)
		console_printf(CON_WARN, "Profile: %" PRIu32 " of %" PRIu32 " samples of core 1's PC "
			       "are outside its code.", other, samples);
	if (host_lost + guest_lost + ops_lost)
		console_printf(CON_INFO, "Profile: tables full: %" PRIu32 " samples' host PCs counted "
			       "by 64-byte line (%" PRIu32 " of them in the totals only); %" PRIu32
			       " guest and %" PRIu32 " instruction samples in the totals only.",
			       host_lost, coarse_lost, guest_lost, ops_lost);
}

#else /* !PROFILE_SUPPORTED */

bool profile_setup(int on, const char *ini_path, const char *ini_dir, int fast_cpu)
{
	(void) ini_path;
	(void) ini_dir;
	(void) fast_cpu;
	if (on)
		console_puts(CON_WARN, "profile = 1 is ignored: this chip has no PC record that the "
			     "other core can read (the ESP32-S31 has).");
	return false;
}

void profile_start(void *cpu)
{
	(void) cpu;
}

void profile_write(const char *why)
{
	(void) why;
}

bool profile_on(void)
{
	return false;
}

#endif /* PROFILE_SUPPORTED */
