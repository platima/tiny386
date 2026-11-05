/*
 * membench.c: what the ESP32-S31's memory can do, measured in one run
 * (v5.2; the study is PSRAM-v52.md section 5, its check CHECK-mem).
 *
 * Opt-in: [esp] membench = 1 in an ini (the card's membench.ini), picked in
 * the boot menu.  It runs where [esp] memtest runs, before the PC is built,
 * with the panel's DMA running as it does in use, on core 1 (core 0 for the
 * two-core lines).  Each line goes to say(), which esp_main.c points at the
 * console and a buffer it writes to MEMBENCH.TXT beside the ini at the end.
 * It stores only into its own buffers in the emulator's pool, and the PC is
 * not built after it: the board waits for RESET.
 *
 * A line of figures:
 *   name  bytes  MB/s  cycles a line  fills  writebacks  stall events  conflicts
 * the time the median of MB_RUNS passes, the last four ESP-IDF's cache
 * counters a line from the last pass, summed over both cores' data buses.
 *
 * Changes from the study's copy (CHECK-mem): the PIE kernels are plain
 * loops (membench_pie.S); pl_wait() gives up after PL_WAIT_MS, and the
 * engine's tests stop if it ever does; and a two-core line runs no-load
 * fills on core 1 while core 0 writes back its own buffer and reads the
 * one being filled, then checks every word.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_cache_cnt.h"
#include "esp_async_memcpy.h"
#include "esp_clk_tree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/cache_reg.h"
#include "soc/soc.h"
#include "esp32s31/rom/cache.h"
#include "membench.h"

#define LINE 64u
#define MB_RUNS 5
#define PS_BIG (1024u * 1024u)     /* well past the 64 KiB cache */
#define PS_SMALL (16u * 1024u)     /* well inside it */
#define WAY (32u * 1024u)          /* 64 KiB in 2 ways: a way's span */
#define PL_WAIT_MS 50              /* the preload engine's most, for one chunk */

static membench_say_fn say;

/* the buffers: A and B in PSRAM, C for the eviction checks, S in internal RAM */
static uint8_t *pa, *pb, *pc, *si;
static uint32_t mhz;

/* ==== kernels, in internal RAM, so that their fetches do not touch the bus
   under test (the PIE ones in membench_pie.S) ==== */
void mb_pie_rd64(const void *p16, uint32_t n64);
void mb_pie_fill64(void *p16, uint32_t v, uint32_t n64);
void mb_pie_copy64(void *d16, const void *s16, uint32_t n64);
void mb_pie_copy64u(void *d16, const void *s, uint32_t n64);

static uint32_t IRAM_ATTR rd_words(const uint8_t *p, uint32_t n)
{
	const volatile uint32_t *q = (const void *) p;
	uint32_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0;
	for (uint32_t i = 0; i < n / 32; i++, q += 8) {
		a += q[0]; b += q[1]; c += q[2]; d += q[3];
		e += q[4]; f += q[5]; g += q[6]; h += q[7];
	}
	return a + b + c + d + e + f + g + h;
}

static void IRAM_ATTR wr_words(uint8_t *p, uint32_t n, uint32_t v)
{
	volatile uint32_t *q = (void *) p;
	for (uint32_t i = 0; i < n / 32; i++, q += 8) {
		q[0] = v; q[1] = v; q[2] = v; q[3] = v;
		q[4] = v; q[5] = v; q[6] = v; q[7] = v;
	}
}

/* one load or store a line: a miss with nothing else */
static uint32_t IRAM_ATTR rd_line(const uint8_t *p, uint32_t n)
{
	uint32_t a = 0;
	for (uint32_t i = 0; i < n; i += LINE)
		a += *(const volatile uint32_t *) (p + i);
	return a;
}

static void IRAM_ATTR wr_line(uint8_t *p, uint32_t n, uint32_t v)
{
	for (uint32_t i = 0; i < n; i += LINE)
		*(volatile uint32_t *) (p + i) = v;
}

/* ==== the cache's engine: a preload of whole lines, with or without loading
   them (CACHE_L1_DCACHE_PRELOAD_MODE, in ESP-IDF's register header only) ==== */
static volatile bool s_pl_stuck;       /* the engine once did not finish: no more of it */

static inline void pl_start(const void *p, uint32_t n, bool noload)
{
	REG_WRITE(CACHE_L1_DCACHE_PRELOAD_ADDR_REG, (uint32_t) p);
	REG_WRITE(CACHE_L1_DCACHE_PRELOAD_SIZE_REG, n);
	REG_WRITE(CACHE_L1_DCACHE_PRELOAD_CTRL_REG,
		  (noload ? CACHE_L1_DCACHE_PRELOAD_MODE : 0) | CACHE_L1_DCACHE_PRELOAD_ENA);
}

/* true when done; false, and s_pl_stuck set, after PL_WAIT_MS without it */
static inline bool pl_wait(void)
{
	uint32_t t0 = esp_cpu_get_cycle_count(), most = PL_WAIT_MS * 1000u * mhz;
	while (!(REG_READ(CACHE_L1_DCACHE_PRELOAD_CTRL_REG) & CACHE_L1_DCACHE_PRELOAD_DONE)) {
		if (esp_cpu_get_cycle_count() - t0 > most) {
			s_pl_stuck = true;
			return false;
		}
	}
	return true;
}

/* ==== cold and warm ==== */
static void wb(const void *p, uint32_t n)
{
	esp_cache_msync((void *) p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static void wbi(const void *p, uint32_t n)
{
	esp_cache_msync((void *) p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

/* ==== one measured thing ==== */
struct cnt {
	uint32_t fills, wbs, stalls, conflicts;
};

static void cnt_read(struct cnt *c)
{
	memset(c, 0, sizeof(*c));
	for (size_t u = 0; u < esp_cache_cnt_num_units(); u++) {
		esp_cache_cnt_unit_info_t info;
		esp_cache_cnt_data_t d;
		if (esp_cache_cnt_get_unit_info(u, &info) != ESP_OK ||
		    info.traffic_type != CACHE_PROFILE_TRAFFIC_DATA ||
		    esp_cache_cnt_get(u, &d) != ESP_OK)
			continue;
		c->fills += d.line_fills;
		c->wbs += d.writebacks;
		c->stalls += d.stall_events;
		c->conflicts += d.conflicts;
	}
}

typedef void (*op_fn)(uint32_t arg);

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
	return x < y ? -1 : x > y;
}

/* prep (not timed), then op, MB_RUNS times; one line of results */
static void measure(const char *name, uint32_t bytes, op_fn prep, op_fn op, uint32_t arg)
{
	uint32_t t[MB_RUNS];
	struct cnt c = { 0 };
	for (int i = 0; i < MB_RUNS; i++) {
		if (prep)
			prep(arg);
		esp_cache_cnt_clear();
		uint32_t t0 = esp_cpu_get_cycle_count();
		op(arg);
		t[i] = esp_cpu_get_cycle_count() - t0;
		cnt_read(&c);
	}
	qsort(t, MB_RUNS, sizeof(t[0]), cmp_u32);
	uint32_t lines = bytes / LINE, m = t[MB_RUNS / 2];
	say("%-28s %8u %7.1f MB/s %6.1f cy/line  fill %.2f wb %.2f stall %.0f confl %.2f",
	    name, (unsigned) bytes, (double) bytes * mhz / m, (double) m / lines,
	    (double) c.fills / lines, (double) c.wbs / lines,
	    (double) c.stalls / lines, (double) c.conflicts / lines);
}

/* ==== the tests: arg is what the op works on (a size, mostly) ==== */
static volatile uint32_t sink;

static void prep_cold_a(uint32_t n) { wbi(pa, n); }
static void prep_cold_ab(uint32_t n) { wbi(pa, n); wbi(pb, n + WAY); }
static void prep_warm_a(uint32_t n) { memset(pa, 0x5a, n); }
static void prep_warm_ab(uint32_t n) { memset(pa, 0x5a, n); memset(pb, 0xa5, n); }
static void prep_cold_big(uint32_t a) { wbi(pa, PS_BIG); (void) a; }
static void prep_cold_big2(uint32_t a) { wbi(pa, PS_BIG); wbi(pb, PS_BIG); (void) a; }

static void op_rd_words(uint32_t n) { sink += rd_words(pa, n); }
static void op_rd_line(uint32_t n) { sink += rd_line(pa, n); }
static void op_rd_pie(uint32_t n) { mb_pie_rd64(pa, n / 64); }
static void op_wr_words(uint32_t n) { wr_words(pa, n, 0x12345678); }
static void op_wr_line(uint32_t n) { wr_line(pa, n, 0x12345678); }
static void op_wr_pie(uint32_t n) { mb_pie_fill64(pa, 0x12345678, n / 64); }
static void op_memset(uint32_t n) { memset(pa, 0x77, n); }
static void op_memcpy(uint32_t n) { memcpy(pb, pa, n); }
static void op_memcpy_off(uint32_t n) { memcpy(pb + WAY / 2, pa, n); }
static void op_pie_copy(uint32_t n) { mb_pie_copy64(pb, pa, n / 64); }
static void op_pie_copyu(uint32_t n) { mb_pie_copy64u(pb, pa + 2, n / 64); }
static void op_wb(uint32_t n) { wb(pa, n); }

/* a word from each line in an order the prefetcher cannot follow (an odd step
   through a power of two lines visits them all) */
static void IRAM_ATTR op_rd_rand(uint32_t n)
{
	uint32_t a = 0, lines = n / LINE, i = 0;
	for (uint32_t k = 0; k < lines; k++) {
		a += *(const volatile uint32_t *) (pa + i * LINE);
		i = (i + 5413) & (lines - 1);
	}
	sink += a;
}

/* the same memory by the alias that goes round the cache (gdma_link.c uses
   it for its descriptors).  What is stored this way is not in the cache: the
   range must not be there (prep_cold_*).  Core 1 only, for the PIE. */
#define UC(p) ((p) + SOC_NON_CACHEABLE_OFFSET_PSRAM)
static void op_uc_rd(uint32_t n) { sink += rd_words(UC(pa), n); }
static void op_uc_wr(uint32_t n) { wr_words(UC(pa), n, 0x12345678); }
static void op_uc_pie(uint32_t n) { mb_pie_fill64(UC(pa), 0x12345678, n / 64); }
static void op_uc_cpy(uint32_t n) { mb_pie_copy64(UC(pb), pa, n / 64); }

/* internal RAM, the same kernels */
static void op_si_rd(uint32_t n) { sink += rd_words(si, n); }
static void op_si_wr(uint32_t n) { wr_words(si, n, 0x12345678); }
static void op_si_cpy(uint32_t n) { memcpy(si + n, si, n); }
static void op_si_pie(uint32_t n) { mb_pie_copy64(si + n, si, n / 64); }

/* fill and copy with each destination chunk's lines made without loading
   them; arg is the chunk size, over all of the part of PS_BIG it divides */
static void IRAM_ATTR op_nl_fill(uint32_t ch)
{
	uint32_t n = PS_BIG / ch * ch;
	for (uint32_t o = 0; o < n && !s_pl_stuck; o += ch) {
		pl_start(pa + o, ch, true);
		if (!pl_wait())
			return;
		mb_pie_fill64(pa + o, 0x12345678, ch / 64);
	}
}

static void IRAM_ATTR op_nl_copy(uint32_t ch)
{
	uint32_t n = PS_BIG / ch * ch;
	for (uint32_t o = 0; o < n && !s_pl_stuck; o += ch) {
		pl_start(pb + o, ch, true);
		if (!pl_wait())
			return;
		mb_pie_copy64(pb + o, pa + o, ch / 64);
	}
}

/* also the source's next chunk loaded by the engine while this one is copied */
static void IRAM_ATTR op_nl_pl_copy(uint32_t ch)
{
	uint32_t n = PS_BIG / ch * ch - ch;
	pl_start(pa, ch, false);
	if (!pl_wait())
		return;
	for (uint32_t o = 0; o < n && !s_pl_stuck; o += ch) {
		pl_start(pb + o, ch, true);
		if (!pl_wait())
			return;
		pl_start(pa + o + ch, ch, false);
		mb_pie_copy64(pb + o, pa + o, ch / 64);
		if (!pl_wait())
			return;
	}
}

/* the engine alone, no loading: its cost a chunk */
static void IRAM_ATTR op_nl_cost(uint32_t ch)
{
	uint32_t n = PS_BIG / ch * ch;
	for (uint32_t o = 0; o < n && !s_pl_stuck; o += ch) {
		pl_start(pa + o, ch, true);
		if (!pl_wait())
			return;
	}
}

/* the engine loading, then the CPU reading what it loaded: is the engine
   quicker than demand misses? */
static void IRAM_ATTR op_pl_read(uint32_t ch)
{
	uint32_t n = PS_BIG / ch * ch;
	for (uint32_t o = 0; o < n && !s_pl_stuck; o += ch) {
		pl_start(pa + o, ch, false);
		if (!pl_wait())
			return;
		sink += rd_line(pa + o, ch);
	}
}

/* ==== what the no-load preload does, checked before it is trusted ==== */
static uint32_t count_words(const uint8_t *p, uint32_t n, uint32_t v)
{
	uint32_t c = 0;
	for (uint32_t i = 0; i < n; i += 4)
		c += *(const volatile uint32_t *) (p + i) == v;
	return c;
}

static bool engine_ok(const char *where)
{
	if (!s_pl_stuck)
		return true;
	say("THE PRELOAD ENGINE DID NOT FINISH within %d ms (%s): its tests stop here", PL_WAIT_MS, where);
	return false;
}

static void noload_checks(void)
{
	const uint32_t N = 4096, W = N / 4;
	/* 1: lines not in the cache: what does a read give after it? */
	wr_words(pa, N, 0xAAAA5555);
	wbi(pa, N);
	pl_start(pa, N, true);
	if (!pl_wait() || !engine_ok("check 1"))
		return;
	uint32_t same = count_words(pa, N, 0xAAAA5555), zero = count_words(pa, N, 0);
	say("no-load: of %u words read after it, %u memory's, %u zero, %u other "
	    "(all memory's: it loaded anyway)",
	    (unsigned) W, (unsigned) same, (unsigned) zero, (unsigned) (W - same - zero));
	/* 2: left clean?  Written back and dropped, memory must be as it was */
	wbi(pa, N);
	same = count_words(pa, N, 0xAAAA5555);
	say("no-load: not stored into: %u of %u words right in memory after write-back (%s)",
	    (unsigned) same, (unsigned) W, same == W ? "clean" : "DIRTY: it writes rubbish back");
	/* 3: a dirty line already there is kept? */
	wr_words(pa, N, 0x11223344);
	pl_start(pa, N, true);
	if (!pl_wait() || !engine_ok("check 3"))
		return;
	same = count_words(pa, N, 0x11223344);
	say("no-load: over dirty lines: %u of %u words kept (%s)", (unsigned) same, (unsigned) W,
	    same == W ? "hits left alone" : "LOST: only ever for lines stored whole");
	wbi(pa, N);
	/* 4: whole lines stored after it reach memory? */
	pl_start(pa, N, true);
	if (!pl_wait() || !engine_ok("check 4"))
		return;
	wr_words(pa, N, 0xCAFEF00D);
	wbi(pa, N);
	same = count_words(pa, N, 0xCAFEF00D);
	say("no-load: stored whole, written back, read again: %u of %u right",
	    (unsigned) same, (unsigned) W);
	/* 5: 64 KiB of dirty lines elsewhere, then no-load over 64 KiB: are the
	   lines it pushes out written back? */
	wr_words(pc, 64 * 1024, 0x0BADBEEF);
	for (uint32_t o = 0; o < 64 * 1024; o += 4096) {
		pl_start(pa + o, 4096, true);
		if (!pl_wait() || !engine_ok("check 5"))
			return;
	}
	wbi(pa, 64 * 1024);
	wbi(pc, 64 * 1024);
	same = count_words(pc, 64 * 1024, 0x0BADBEEF);
	say("no-load: dirty lines it pushed out: %u of %u words right in memory%s",
	    (unsigned) same, (unsigned) (64 * 1024 / 4),
	    same == 64 * 1024 / 4 ? "" : " (LOST: other dirty data in PSRAM could go the same way)");
}

/* stored round the cache, then read through it at once: does the read see
   the store?  (Is a fence needed between the two paths?) */
static void uncached_checks(void)
{
	const uint32_t N = 4096, W = N / 4;
	wbi(pa, N);
	mb_pie_fill64(UC(pa), 0x5AA5C33C, N / 64);
	uint32_t same = count_words(pa, N, 0x5AA5C33C);
	say("uncached: stored round the cache, read through it at once: %u of %u right",
	    (unsigned) same, (unsigned) W);
	wbi(pa, N);
	same = count_words(pa, N, 0x5AA5C33C);
	say("uncached: and after dropping those lines: %u of %u right", (unsigned) same, (unsigned) W);
}

/* ==== the DMA ==== */
static volatile bool s_dma_done;

static bool IRAM_ATTR dma_cb(async_memcpy_handle_t h, async_memcpy_event_t *e, void *arg)
{
	(void) h; (void) e; (void) arg;
	s_dma_done = true;
	return false;
}

/* false if a piece did not finish within a second */
static bool dma_run(async_memcpy_handle_t h, uint8_t *d, const uint8_t *s, uint32_t n)
{
	for (uint32_t o = 0; o < n; ) {
		uint32_t k = n - o > 64 * 1024 ? 64 * 1024 : n - o;
		s_dma_done = false;
		if (esp_async_memcpy(h, d + o, (void *) (s + o), k, dma_cb, NULL) != ESP_OK)
			return false;
		uint32_t t0 = esp_cpu_get_cycle_count();
		while (!s_dma_done)
			if (esp_cpu_get_cycle_count() - t0 > 1000000u * mhz)
				return false;
		o += k;
	}
	return true;
}

typedef esp_err_t (*dma_install)(const async_memcpy_config_t *, async_memcpy_handle_t *);

static void dma_tests(const char *bus, dma_install install, unsigned burst)
{
	async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
	async_memcpy_handle_t h;
	cfg.backlog = 2;
	cfg.dma_burst_size = burst;
	if (install(&cfg, &h) != ESP_OK) {
		say("DMA %s burst %u: would not start", bus, burst);
		return;
	}
	static const uint32_t sizes[] = { 1024, 4096, 64 * 1024, PS_BIG };
	for (int k = 0; k < 4; k++) {
		uint32_t n = sizes[k], m = n > 32 * 1024 ? 32 * 1024 : n, t[3][MB_RUNS];
		bool ok = true;
		for (int i = 0; i < MB_RUNS && ok; i++) {
			uint32_t t0 = esp_cpu_get_cycle_count();
			ok &= dma_run(h, pb, pa, n);
			t[0][i] = esp_cpu_get_cycle_count() - t0;
			t0 = esp_cpu_get_cycle_count();
			ok &= dma_run(h, pa, si, m);
			t[1][i] = esp_cpu_get_cycle_count() - t0;
			t0 = esp_cpu_get_cycle_count();
			ok &= dma_run(h, si, pa, m);
			t[2][i] = esp_cpu_get_cycle_count() - t0;
		}
		if (!ok) {
			say("DMA %s burst %u %u: a transfer did not finish; the rest of this bus left out", bus, burst, (unsigned) n);
			break;
		}
		for (int w = 0; w < 3; w++)
			qsort(t[w], MB_RUNS, sizeof(uint32_t), cmp_u32);
		say("DMA %s burst %3u %8u: PSRAM to PSRAM %6.1f; %u: internal to PSRAM %6.1f, "
		    "PSRAM to internal %6.1f MB/s",
		    bus, burst, (unsigned) n, (double) n * mhz / t[0][MB_RUNS / 2], (unsigned) m,
		    (double) m * mhz / t[1][MB_RUNS / 2], (double) m * mhz / t[2][MB_RUNS / 2]);
	}
	esp_async_memcpy_uninstall(h);
}

/* ==== the two cores together: a stream on core 1, core 0 measured ==== */
static volatile int s_load;     /* 0 none, 1 PSRAM write stream, 2 read stream; -1 end */
static volatile bool s_load_up;

static void load_task(void *arg)
{
	(void) arg;
	while (s_load >= 0) {
		int l = s_load;
		s_load_up = true;
		if (l == 1)
			wr_words(pa, PS_BIG, 0x87654321);
		else if (l == 2)
			sink += rd_words(pa, PS_BIG);
		else
			vTaskDelay(1);
	}
	s_load = -2;
	vTaskDelete(NULL);
}

static void core0_part(void *arg)
{
	static const char *const nm[] = { "nothing", "a write stream", "a read stream" };
	TaskHandle_t back = arg;
	xTaskCreatePinnedToCore(load_task, "mb_load", 4096, NULL, 2, NULL, 1);
	for (int l = 0; l < 3; l++) {
		s_load = l;
		s_load_up = false;
		while (!s_load_up)
			vTaskDelay(1);
		uint32_t t0, a, b, c, d;
		t0 = esp_cpu_get_cycle_count();
		for (int i = 0; i < 64; i++)
			sink += rd_words(si, 32 * 1024);
		a = esp_cpu_get_cycle_count() - t0;
		sink += rd_words(pb, PS_SMALL);
		t0 = esp_cpu_get_cycle_count();
		for (int i = 0; i < 64; i++)
			sink += rd_words(pb, PS_SMALL);
		b = esp_cpu_get_cycle_count() - t0;
		t0 = esp_cpu_get_cycle_count();
		sink += rd_words(pc, PS_BIG);
		c = esp_cpu_get_cycle_count() - t0;
		t0 = esp_cpu_get_cycle_count();
		wr_words(pc, PS_BIG, l);
		d = esp_cpu_get_cycle_count() - t0;
		say("core 0, core 1 running %-14s: internal read %6.1f, PSRAM hits %6.1f, "
		    "PSRAM read %6.1f, PSRAM write %6.1f MB/s",
		    nm[l], 64.0 * 32 * 1024 * mhz / a, 64.0 * PS_SMALL * mhz / b,
		    (double) PS_BIG * mhz / c, (double) PS_BIG * mhz / d);
	}
	s_load = -1;
	while (s_load != -2)
		vTaskDelay(1);
	xTaskNotifyGive(back);
	vTaskDelete(NULL);
}

static void two_cores(void)
{
	xTaskCreatePinnedToCore(core0_part, "mb_core0", 6144, xTaskGetCurrentTaskHandle(), 3, NULL, 0);
	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

/* ==== no-load fills while core 0 works the cache, as the firmware would
   use them (CHECK-mem section 4): core 1 fills 1 MiB of A a 4 KiB chunk at
   a time, the engine making each chunk's lines without loading them and
   whole stores after, a pattern of its own for each chunk; core 0 meanwhile
   stores into 64 KiB of B, writes it back and drops it, as vga_task does
   with the panel's buffer, and reads A, as the panel would scan a frame
   buffer.  Then A is written back, dropped and checked word for word. ==== */
static volatile int s_nl0;      /* 1 run, 0 stop, -1 stopped */
static volatile uint32_t s_nl0_rounds;

static void nl_core0(void *arg)
{
	(void) arg;
	uint32_t k = 0;
	while (s_nl0 > 0) {
		wr_words(pb, 64 * 1024, 0x0C0C0000u | k++);
		wbi(pb, 64 * 1024);
		sink += rd_words(pa, 64 * 1024);
		s_nl0_rounds++;
	}
	s_nl0 = -1;
	vTaskDelete(NULL);
}

static uint32_t nl_pattern(uint32_t o) { return 0x3C5A0000u | (o >> 12); }

static void nl_two_cores(void)
{
	const uint32_t ch = 4096, n = PS_BIG;
	wbi(pa, n);
	wbi(pb, 64 * 1024);
	s_nl0_rounds = 0;
	s_nl0 = 1;
	xTaskCreatePinnedToCore(nl_core0, "mb_nl0", 4096, NULL, 3, NULL, 0);
	while (!s_nl0_rounds)
		vTaskDelay(1);
	uint32_t t0 = esp_cpu_get_cycle_count(), done = 0;
	for (uint32_t o = 0; o < n; o += ch, done += ch) {
		pl_start(pa + o, ch, true);
		if (!pl_wait())
			break;
		mb_pie_fill64(pa + o, nl_pattern(o), ch / 64);
	}
	uint32_t t = esp_cpu_get_cycle_count() - t0, rounds = s_nl0_rounds;
	s_nl0 = 0;
	while (s_nl0 != -1)
		vTaskDelay(1);
	if (!engine_ok("the two-core no-load line"))
		return;
	wbi(pa, n);
	uint32_t right = 0;
	for (uint32_t o = 0; o < n; o += ch)
		right += count_words(pa + o, ch, nl_pattern(o));
	say("no-load fill under core 0's write-backs and reads: %u bytes %7.1f MB/s %6.1f cy/line; "
	    "core 0 %u rounds; %u of %u words right after (%s)",
	    (unsigned) done, (double) done * mhz / t, (double) t / (done / LINE), (unsigned) rounds,
	    (unsigned) right, (unsigned) (n / 4), right == n / 4 ? "all kept" : "SOME LOST");
}

/* ==== the hardware's prefetch, on for the buffers alone ==== */
static void autoload_tests(void)
{
	struct l1_dcache_autoload_config cfg = {
		.order = CACHE_AUTOLOAD_POSITIVE,
		.trigger = CACHE_AUTOLOAD_MISS_TRIGGER,
		.ena0 = 1, .addr0 = (uint32_t) pa, .size0 = PS_BIG,
		.ena1 = 1, .addr1 = (uint32_t) pb, .size1 = PS_BIG,
	};
	bool was = REG_READ(CACHE_L1_DCACHE_AUTOLOAD_CTRL_REG) & CACHE_L1_DCACHE_AUTOLOAD_ENA;
	Cache_Config_L1_DCache_Autoload(&cfg);
	Cache_Enable_L1_DCache_Autoload();
	measure("autoload: read words", PS_BIG, prep_cold_a, op_rd_words, PS_BIG);
	measure("autoload: read a word a line", PS_BIG, prep_cold_a, op_rd_line, PS_BIG);
	measure("autoload: memcpy", PS_BIG, prep_cold_ab, op_memcpy, PS_BIG);
	if (!was)
		Cache_Disable_L1_DCache_Autoload();
}

/* ==== all of it ==== */
static void run_all(void)
{
	say("membench: CPU %u MHz; D-cache %u KiB, %u-byte lines; autoload ctrl %08x, wrap ctrl %08x",
	    (unsigned) mhz, CONFIG_CACHE_L1_DCACHE_SIZE / 1024, CONFIG_CACHE_L1_DCACHE_LINE_SIZE,
	    (unsigned) REG_READ(CACHE_L1_DCACHE_AUTOLOAD_CTRL_REG),
	    (unsigned) REG_READ(CACHE_L1_CACHE_WRAP_AROUND_CTRL_REG));

	say("== internal RAM, 32 KiB");
	measure("internal read", 32 * 1024, NULL, op_si_rd, 32 * 1024);
	measure("internal write", 32 * 1024, NULL, op_si_wr, 32 * 1024);
	measure("internal memcpy", 16 * 1024, NULL, op_si_cpy, 16 * 1024);
	measure("internal PIE copy", 16 * 1024, NULL, op_si_pie, 16 * 1024);

	say("== PSRAM in the cache, 16 KiB");
	measure("hits: read words", PS_SMALL, prep_warm_a, op_rd_words, PS_SMALL);
	measure("hits: read PIE", PS_SMALL, prep_warm_a, op_rd_pie, PS_SMALL);
	measure("hits: write words", PS_SMALL, prep_warm_a, op_wr_words, PS_SMALL);
	measure("hits: write PIE", PS_SMALL, prep_warm_a, op_wr_pie, PS_SMALL);
	measure("hits: memcpy", PS_SMALL, prep_warm_ab, op_memcpy, PS_SMALL);
	measure("hits: PIE copy", PS_SMALL, prep_warm_ab, op_pie_copy, PS_SMALL);

	say("== PSRAM not in the cache, 1 MiB (the panel's DMA running)");
	measure("miss: read a word a line", PS_BIG, prep_cold_a, op_rd_line, PS_BIG);
	measure("miss: a word a line, shuffled", PS_BIG, prep_cold_a, op_rd_rand, PS_BIG);
	measure("miss: read words", PS_BIG, prep_cold_a, op_rd_words, PS_BIG);
	measure("miss: read PIE", PS_BIG, prep_cold_a, op_rd_pie, PS_BIG);
	measure("miss: store a word a line", PS_BIG, prep_cold_a, op_wr_line, PS_BIG);
	measure("miss: write words", PS_BIG, prep_cold_a, op_wr_words, PS_BIG);
	measure("miss: write PIE", PS_BIG, prep_cold_a, op_wr_pie, PS_BIG);
	measure("miss: memset", PS_BIG, prep_cold_a, op_memset, PS_BIG);
	measure("miss: memcpy", PS_BIG, prep_cold_ab, op_memcpy, PS_BIG);
	measure("miss: memcpy, 16 KiB off", PS_BIG, prep_cold_ab, op_memcpy_off, PS_BIG);
	measure("miss: PIE copy", PS_BIG, prep_cold_ab, op_pie_copy, PS_BIG);
	measure("miss: PIE copy, source +2", PS_BIG - WAY, prep_cold_ab, op_pie_copyu, PS_BIG - WAY);

	say("== write-back");
	measure("64 KiB dirty, written back", 64 * 1024, prep_warm_a, op_wb, 64 * 1024);
	measure("64 KiB clean, written back", 64 * 1024, NULL, op_wb, 64 * 1024);

	say("== the preload engine");
	noload_checks();
	static const uint32_t chunks[] = { 256, 1024, 4096, 16320 };
	for (int i = 0; i < 4 && engine_ok("the chunk lines"); i++) {
		char nm[32];
		uint32_t ch = chunks[i], n = PS_BIG / ch * ch;
		snprintf(nm, sizeof(nm), "engine alone, %u", (unsigned) ch);
		measure(nm, n, prep_cold_big, op_nl_cost, ch);
		snprintf(nm, sizeof(nm), "no-load fill, %u", (unsigned) ch);
		measure(nm, n, prep_cold_big, op_nl_fill, ch);
		snprintf(nm, sizeof(nm), "no-load copy, %u", (unsigned) ch);
		measure(nm, n, prep_cold_big2, op_nl_copy, ch);
		snprintf(nm, sizeof(nm), "loads ahead, no-load, %u", (unsigned) ch);
		measure(nm, n - ch, prep_cold_big2, op_nl_pl_copy, ch);
		snprintf(nm, sizeof(nm), "engine loads, read, %u", (unsigned) ch);
		measure(nm, n, prep_cold_big, op_pl_read, ch);
	}
	if (engine_ok("before the two-core no-load line"))
		nl_two_cores();

	say("== round the cache");
	uncached_checks();
	measure("uncached: read words", PS_BIG, prep_cold_a, op_uc_rd, PS_BIG);
	measure("uncached: write words", PS_BIG, prep_cold_a, op_uc_wr, PS_BIG);
	measure("uncached: write PIE", PS_BIG, prep_cold_a, op_uc_pie, PS_BIG);
	measure("uncached: PIE copy to it", PS_BIG, prep_cold_ab, op_uc_cpy, PS_BIG);

	say("== the hardware's prefetch (autoload)");
	autoload_tests();

	say("== DMA (each call waited for; the driver's cache work included)");
	memset(si, 0x3c, 32 * 1024);
	dma_tests("AHB", esp_async_memcpy_install_gdma_ahb, 32);
	dma_tests("AHB", esp_async_memcpy_install_gdma_ahb, 64);
	dma_tests("AXI", esp_async_memcpy_install_gdma_axi, 32);
	dma_tests("AXI", esp_async_memcpy_install_gdma_axi, 64);
	dma_tests("AXI", esp_async_memcpy_install_gdma_axi, 128);

	say("== the two cores");
	two_cores();
	say("membench: done%s", s_pl_stuck ? " (the preload engine stuck: its tests were cut short)" : "");
}

/* everything from core 1, as the PC's task runs */
static TaskHandle_t s_waiter;

static void mb_task(void *arg)
{
	(void) arg;
	esp_cache_cnt_start();
	run_all();
	esp_cache_cnt_stop();
	xTaskNotifyGive(s_waiter);
	vTaskDelete(NULL);
}

int membench_run(membench_say_fn out, uint8_t *pool, size_t pool_len)
{
	say = out;
	if (pool_len < 3 * (PS_BIG + WAY) + 4096)
		return -1;
	pa = pool;
	pb = pa + PS_BIG + WAY;
	pc = pb + PS_BIG + WAY;
	/* 32 KiB of internal RAM and at most 18 KiB of stacks, out of the 127
	   KiB free before the PC is built */
	si = heap_caps_aligned_alloc(LINE, 32 * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
	if (!si)
		return -2;
	uint32_t hz = 0;
	esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
	mhz = hz / 1000000;
	s_pl_stuck = false;
	s_waiter = xTaskGetCurrentTaskHandle();
	xTaskCreatePinnedToCore(mb_task, "membench", 8192, NULL, 3, NULL, 1);
	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	heap_caps_free(si);
	return s_pl_stuck ? 1 : 0;
}
