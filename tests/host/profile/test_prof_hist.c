/*
 * tests/host/profile: the profiler's counts (esp/main/prof_hist.c), its
 * reader of the guest's state (esp/main/prof_cpu.h) and PROFILE.TXT, as
 * the board builds them, on the PC (PROF_HOST).
 *
 *   test_prof_hist                    the checks; exit status 0 if all pass
 *   test_prof_hist --time             also how long a sample and a write take
 *   test_prof_hist --demo FILE SHA    a PROFILE.TXT whose host PCs are in this
 *                                     program's own functions (build it with
 *                                     -no-pie), for profile_report.py's test
 *
 * The checks: the tables (a count for each key, a full table counting its
 * keys as lost while the totals keep every sample, the coarse table taking
 * what the host table could not), the guest's key (the code page while the
 * fast interpreter runs, the step's start when there is none, CS:EIP for
 * tiny386), the instruction key (prefixes, 0Fh, ModRM, bytes running out),
 * the cache counters, the file (every line, "end" last, the size returned,
 * a refused write said), and the reader on a CPU's fields in each mode and
 * area, with the instruction's bytes read through the fetch cache's mapping.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "prof_hist.h"
#include "prof_cpu.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct prof P;
static void *mem;

static void setup(uint32_t host, uint32_t coarse, uint32_t guest, uint32_t ops)
{
	struct prof_sizes sz = { host, coarse, guest, ops };
	free(mem);
	mem = calloc(1, prof_mem_bytes(&sz));
	prof_init(&P, mem, &sz);
	prof_set_range(&P, PROF_REGION_IRAM, 0x2f000000, 0x2f040000);
	prof_set_range(&P, PROF_REGION_FLASH, 0x40000000, 0x40100000);
	prof_set_range(&P, PROF_REGION_ROM, 0x2f800000, 0x2f850000);
}

static struct prof_guest guest(int area, int mode, uint32_t cs, uint32_t base, uint32_t eip,
			       uint32_t page)
{
	struct prof_guest g;
	memset(&g, 0, sizeof(g));
	g.area = (uint8_t) area;
	g.mode = (uint8_t) mode;
	g.cs_sel = cs;
	g.cs_base = base;
	g.eip = eip;
	g.code_page = page;
	g.cpl = mode == PROF_MODE_V86 ? 3 : 0;
	return g;
}

static uint32_t host_count(uint32_t pc, int area)
{
	for (uint32_t i = 0; i < P.host.n; i++)
		if (P.host.s[i].count && P.host.s[i].key == pc && P.host.s[i].aux == (uint32_t) area)
			return P.host.s[i].count;
	return 0;
}

static uint64_t table_sum(const struct prof_table *t)
{
	uint64_t n = 0;
	for (uint32_t i = 0; i < t->n; i++)
		n += t->s[i].count;
	return n;
}

static uint32_t guest_count(uint32_t key, uint32_t cs)
{
	for (uint32_t i = 0; i < P.guest.n; i++)
		if (P.guest.s[i].count && P.guest.s[i].key == key && P.guest.s[i].aux == cs)
			return P.guest.s[i].count;
	return 0;
}

/* ==== 1. the tables ==== */
static void test_tables(void)
{
	printf("tables:\n");
	setup(16, 8, 16, 8);
	struct prof_guest g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0xc0001234, 0xc0001);
	for (int i = 0; i < 5; i++)
		prof_sample(&P, 0x2f000100, &g);
	prof_sample(&P, 0x40000200, &g);
	g.area = PROF_AREA_EXEC1;
	prof_sample(&P, 0x2f000100, &g);       /* the same PC under another area: its own count */
	prof_sample(&P, 0x2f800010, &g);       /* ROM */
	prof_sample(&P, 0x12345678, &g);       /* none of the regions */
	CHECK(P.samples == 9, "samples %llu", (unsigned long long) P.samples);
	CHECK(host_count(0x2f000100, PROF_AREA_FAST) == 5, "fast count %u",
	      host_count(0x2f000100, PROF_AREA_FAST));
	CHECK(host_count(0x2f000100, PROF_AREA_EXEC1) == 1, "exec1 count %u",
	      host_count(0x2f000100, PROF_AREA_EXEC1));
	CHECK(P.region[PROF_REGION_IRAM] == 6 && P.region[PROF_REGION_FLASH] == 1 &&
	      P.region[PROF_REGION_ROM] == 1 && P.region[PROF_REGION_OTHER] == 1,
	      "regions %llu %llu %llu %llu", (unsigned long long) P.region[0],
	      (unsigned long long) P.region[1], (unsigned long long) P.region[2],
	      (unsigned long long) P.region[3]);
	CHECK(P.area[PROF_AREA_FAST] == 6 && P.area[PROF_AREA_EXEC1] == 3, "areas");
	CHECK(P.area_region[PROF_AREA_EXEC1][PROF_REGION_IRAM] == 1 &&
	      P.area_region[PROF_AREA_EXEC1][PROF_REGION_ROM] == 1, "area against region");
	CHECK(P.mode_area[PROF_MODE_PM32][PROF_AREA_FAST] == 6, "mode against area");

	/* full: 12 of 16 host slots, then the coarse table (6 of 8), then lost */
	setup(16, 8, 16, 8);
	g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0x1000, 1);
	for (uint32_t i = 0; i < 100; i++)
		prof_sample(&P, 0x2f000000 + i * 64 + 2, &g);   /* 100 PCs, each in its own line */
	CHECK(P.host.used == 12, "host used %u (of 16, at most 12)", P.host.used);
	CHECK(P.coarse.used == 6, "coarse used %u (of 8, at most 6)", P.coarse.used);
	CHECK(P.host.lost == 88, "host lost %llu", (unsigned long long) P.host.lost);
	CHECK(P.coarse.lost == 82, "coarse lost %llu", (unsigned long long) P.coarse.lost);
	CHECK(table_sum(&P.host) + table_sum(&P.coarse) + P.coarse.lost == P.samples,
	      "host + coarse + lost = samples (%llu)", (unsigned long long) P.samples);
	CHECK(P.region[PROF_REGION_IRAM] == 100, "the totals keep every sample");
	/* a key already there still counts in a full table */
	prof_sample(&P, 0x2f000002, &g);
	CHECK(host_count(0x2f000002, PROF_AREA_FAST) == 2, "a known key in a full table: %u",
	      host_count(0x2f000002, PROF_AREA_FAST));
	/* coarse lines are the PC's 64-byte line */
	for (uint32_t i = 0; i < P.coarse.n; i++)
		if (P.coarse.s[i].count)
			CHECK((P.coarse.s[i].key & 63) == 0, "coarse key %08x", P.coarse.s[i].key);
}

/* ==== 2. the guest's key ==== */
static void test_guest(void)
{
	printf("the guest's key:\n");
	setup(64, 16, 64, 16);
	/* the fast interpreter: the fetch cache's page, not the step's start */
	struct prof_guest g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0xc0001ff0, 0xc0003);
	prof_sample(&P, 0x2f000000, &g);
	uint32_t k = 0xc0003 | PROF_MODE_PM32 << 20 | 0 << 22 | PROF_AREA_FAST << 24;
	CHECK(guest_count(k, 0x28) == 1, "fast: the code page");
	/* no page: the step's start, marked */
	g.code_page = PROF_NO_PAGE;
	prof_sample(&P, 0x2f000000, &g);
	k = 0xc0001 | PROF_MODE_PM32 << 20 | PROF_AREA_FAST << 24 | 1u << 26;
	CHECK(guest_count(k, 0x28) == 1 && P.page_guess == 1, "fast without a page: CS:EIP, guessed");
	/* tiny386: CS:EIP whatever the cache holds; real mode base */
	g = guest(PROF_AREA_EXEC1, PROF_MODE_REAL, 0xf000, 0xf0000, 0xe05b, 0x12);
	prof_sample(&P, 0x40000000, &g);
	k = 0xfe | PROF_MODE_REAL << 20 | PROF_AREA_EXEC1 << 24;
	CHECK(guest_count(k, 0xf000) == 1, "exec1: CS:EIP's page");
	CHECK(P.op_unread == 1, "exec1 with no bytes: op_unread %llu", (unsigned long long) P.op_unread);
	/* virtual-8086 mode at level 3, halted */
	g = guest(PROF_AREA_HALT, PROF_MODE_V86, 0x1234, 0x12340, 0x10, PROF_NO_PAGE);
	prof_sample(&P, 0x40000010, &g);
	k = 0x12 | PROF_MODE_V86 << 20 | 3 << 22 | PROF_AREA_HALT << 24;
	CHECK(guest_count(k, 0x1234) == 1, "halted in v86: CS:EIP's page, level 3");
	CHECK(P.page_guess == 1, "halted: not a guess");
}

/* ==== 3. the instruction's key ==== */
struct opcase {
	const char *what;
	int area, mode, n;
	uint8_t b[8];
	uint32_t key;
};

#define B(...) { __VA_ARGS__ }
static const struct opcase opcases[] = {
	{ "nop", PROF_AREA_EXEC1, PROF_MODE_REAL, 1, B(0x90), 0x90 << 8 },
	{ "66 0f b6 c0 (movzx)", PROF_AREA_EXEC1, PROF_MODE_PM32, 4, B(0x66, 0x0f, 0xb6, 0xc0),
	  3u << 24 | 1u << 18 | 1u << 16 | 1u << 17 | 0xb6 << 8 | 0xc0 },
	{ "f3 a5 (rep movsd)", PROF_AREA_EXEC1, PROF_MODE_PM32, 2, B(0xf3, 0xa5),
	  3u << 24 | 1u << 20 | 0xa5 << 8 },
	{ "f3 a5 with more bytes", PROF_AREA_EXEC1, PROF_MODE_PM32, 8, B(0xf3, 0xa5, 0x90, 0x90),
	  3u << 24 | 1u << 20 | 0xa5 << 8 },
	{ "26 8b 07 (es: mov)", PROF_AREA_EXEC1, PROF_MODE_REAL, 3, B(0x26, 0x8b, 0x07),
	  1u << 23 | 1u << 17 | 0x8b << 8 | 0x07 },
	{ "f7 f1 (div)", PROF_AREA_ALONE, PROF_MODE_PM16, 2, B(0xf7, 0xf1),
	  2u << 24 | 1u << 26 | 1u << 17 | 0xf7 << 8 | 0xf1 },
	{ "0f 01 15 (lgdt)", PROF_AREA_EXEC1, PROF_MODE_PM32, 7, B(0x0f, 0x01, 0x15, 0, 0, 0, 0),
	  3u << 24 | 1u << 16 | 1u << 17 | 0x01 << 8 | 0x15 },
	{ "cd 21 (int)", PROF_AREA_EXEC1, PROF_MODE_V86, 2, B(0xcd, 0x21), 1u << 24 | 0xcd << 8 },
	{ "d9 c0 (fld)", PROF_AREA_EXEC1, PROF_MODE_PM32, 2, B(0xd9, 0xc0),
	  3u << 24 | 1u << 17 | 0xd9 << 8 | 0xc0 },
	{ "0f 80 (jo rel32: no ModRM)", PROF_AREA_EXEC1, PROF_MODE_PM32, 6, B(0x0f, 0x80, 0, 0, 0, 0),
	  3u << 24 | 1u << 16 | 0x80 << 8 },
	{ "0f c8 (bswap)", PROF_AREA_EXEC1, PROF_MODE_PM32, 2, B(0x0f, 0xc8),
	  3u << 24 | 1u << 16 | 0xc8 << 8 },
	{ "f0 0f c1 (lock xadd)", PROF_AREA_EXEC1, PROF_MODE_PM32, 4, B(0xf0, 0x0f, 0xc1, 0x0a),
	  3u << 24 | 1u << 22 | 1u << 16 | 1u << 17 | 0xc1 << 8 | 0x0a },
	{ "0f alone (out of bytes)", PROF_AREA_EXEC1, PROF_MODE_PM32, 1, B(0x0f),
	  3u << 24 | 0x0f << 8 | 1u << 27 },
	{ "8b with no ModRM byte left", PROF_AREA_EXEC1, PROF_MODE_PM32, 1, B(0x8b),
	  3u << 24 | 1u << 17 | 0x8b << 8 | 1u << 27 },
	{ "five prefixes", PROF_AREA_EXEC1, PROF_MODE_PM32, 8,
	  B(0x66, 0x66, 0x66, 0x66, 0x66, 0x90), 3u << 24 | 1u << 18 | 1u << 27 },
	{ "f2 ae (repne scasb), 67", PROF_AREA_EXEC1, PROF_MODE_PM16, 3, B(0x67, 0xf2, 0xae),
	  2u << 24 | 1u << 19 | 1u << 21 | 0xae << 8 },
};

static void test_ops(void)
{
	printf("the instruction's key:\n");
	for (size_t i = 0; i < sizeof(opcases) / sizeof(opcases[0]); i++) {
		const struct opcase *c = &opcases[i];
		struct prof_guest g = guest(c->area, c->mode, 8, 0, 0x100, 0);
		g.nop = (uint8_t) c->n;
		memcpy(g.op, c->b, 8);
		uint32_t k = prof_op_key(&g);
		CHECK(k == c->key, "%s: key %08x, expected %08x", c->what, k, c->key);
	}
	/* the bytes past the page's end are not read: a short instruction is
	   whole, a longer one is marked */
	setup(64, 16, 64, 16);
	struct prof_guest g = guest(PROF_AREA_ALONE, PROF_MODE_PM32, 8, 0, 0x100, 0);
	g.nop = 1;
	g.op[0] = 0xc3;
	prof_sample(&P, 0x40000000, &g);
	CHECK(P.ops.used == 1, "ret counted");
	uint64_t n = table_sum(&P.ops);
	CHECK(n == 1, "ops counted %llu", (unsigned long long) n);
	g.area = PROF_AREA_FAST;
	prof_sample(&P, 0x2f000000, &g);
	CHECK(table_sum(&P.ops) == 1, "the fast interpreter's samples have no instruction key");
}

/* ==== 4. the cache counters and the file ==== */
static const struct prof_meta META = {
	.firmware = "v5.1-test", .board = "the PC", .idf = "none",
	.elf_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
	.sampler = "host-test", .ini = "/sdcard/tiny386/test.ini", .why = "timer",
	.fast_cpu = 1, .rate_hz = 2000, .file_no = 3, .run = 0x5eed0b07, .cpu_mhz = 320,
	.uptime_us = 123456789, .sampling_us = 120000000, .paused_us = 150000,
	.last_write_us = 48000, .guest_insns = 333000000, .run_us = 110000000, .halt_us = 9000000,
};

static char *slurp(FILE *f, long *len)
{
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	rewind(f);
	char *s = malloc((size_t) *len + 1);
	if (fread(s, 1, (size_t) *len, f) != (size_t) *len)
		*len = -1;
	s[*len < 0 ? 0 : *len] = 0;
	return s;
}

static void test_file(void)
{
	printf("the cache counters and PROFILE.TXT:\n");
	setup(64, 16, 64, 16);
	prof_cache_describe(&P, 0, "l1-cache-inst-core1", 1, 'i', 0x0f);
	prof_cache_describe(&P, 1, "l1-cache-data-core1", 1, 'd', 0x1f);
	uint32_t v[PROF_CACHE_COUNTERS] = { 1000, 50, 3, 20, 7 };
	prof_cache_add(&P, 0, v);
	prof_cache_add(&P, 0, v);
	prof_cache_add(&P, 1, v);
	CHECK(P.cache[0][0] == 2000 && P.cache[0][3] == 40 && P.cache_reads == 2, "cache sums");
	prof_cost_add(&P, 700);
	prof_cost_add(&P, 1300);
	CHECK(P.cost_n == 2 && P.cost_cycles == 2000 && P.cost_max == 1300, "cost");

	struct prof_guest g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0xc0001234, 0xc0001);
	for (int i = 0; i < 7; i++)
		prof_sample(&P, 0x2f000100 + 2 * (uint32_t) i, &g);
	g = guest(PROF_AREA_EXEC1, PROF_MODE_V86, 0x1234, 0x12340, 0x10, PROF_NO_PAGE);
	g.nop = 2;
	g.op[0] = 0xee;     /* out dx, al */
	prof_sample(&P, 0x40000300, &g);

	char buf[300];
	FILE *f = tmpfile();
	long r = prof_write(&P, &META, f, buf, sizeof(buf));
	long len;
	char *s = slurp(f, &len);
	fclose(f);
	CHECK(r > 0 && r == len, "returned %ld, file %ld bytes", r, len);
	CHECK(!strncmp(s, "tiny386 profile 1\nfirmware v5.1-test\nboard the PC\n", 50),
	      "the first lines");
	CHECK(len > 4 && !strcmp(s + len - 4, "end\n"), "end last");
	const char *want[] = {
		"\nelf_sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
		"\nsampler host-test 2000\n", "\nini /sdcard/tiny386/test.ini\n", "\nfast_cpu 1\n",
		"\nfile 3 timer\nrun 5eed0b07\n", "\ncpu_mhz 320\n",
		"\ntime_us uptime 123456789 sampling 120000000 paused 150000\n",
		"\nlast_write_us 48000\n", "\nguest insns 333000000 run_us 110000000 halt_us 9000000\n",
		"\nsampler_cost samples 2 cycles 2000 max 1300\n",
		"\nrange iram 2f000000 2f040000\n", "\nrange flash 40000000 40100000\n",
		"\nrange rom 2f800000 2f850000\n", "\nsamples 8\n",
		"\narea fast 7 exec1 1 alone 0 halt 0\n", "\nmode real 0 v86 1 pm16 0 pm32 7\n",
		"\nregion iram 7 flash 1 rom 0 other 0\n",
		"\narea_region fast 7 0 0 0 exec1 0 1 0 0 alone 0 0 0 0 halt 0 0 0 0\n",
		"\nmode_area real 0 0 0 0 v86 0 1 0 0 pm16 0 0 0 0 pm32 7 0 0 0\n",
		"\nop_unread 0\n", "\npage_guess 0\n",
		"\ntable host slots 64 used 8 lost 0\n", "\ntable coarse slots 16 used 0 lost 0\n",
		"\ntable guest slots 64 used 2 lost 0\n", "\ntable ops slots 16 used 1 lost 0\n",
		"\ncache l1-cache-inst-core1 1 i hit 2000 miss 100 conflict 6 nxtlvl_rd 40 nxtlvl_wr -\n",
		"\ncache l1-cache-data-core1 1 d hit 1000 miss 50 conflict 3 nxtlvl_rd 20 nxtlvl_wr 7\n",
		"\ncache_reads 2\n", "\n[host] pc area count\n", "\n2f000100 0 1\n", "\n40000300 1 1\n",
		"\n[coarse] line area count\n[guest] area mode cpl cs page guess count\n",
		"\n0 3 0 0028 c0001 0 7\n", "\n1 1 3 1234 00012 0 1\n", "\n[ops] key count\n",
		"\n0100ee00 1\nend\n",
	};
	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
		CHECK(strstr(s, want[i]), "the file has \"%.*s\"", (int) strlen(want[i]) - 2, want[i] + 1);
	free(s);

	/* refused: a full disk, and a buffer too small */
	f = fopen("/dev/full", "w");
	if (f) {
		r = prof_write(&P, &META, f, buf, sizeof(buf));
		CHECK(r == -1, "a refused write gives -1 (%ld)", r);
		fclose(f);
	}
	f = tmpfile();
	CHECK(prof_write(&P, &META, f, buf, 100) == -1, "a buffer under 256 bytes is refused");
	fclose(f);
	/* two files at once (Ctrl+Alt+F8: PROFILE.TXT and PROFnn.TXT): the same
	   bytes in both; and the second refusing is said */
	{
		FILE *a = tmpfile(), *b = tmpfile();
		long ra = prof_write2(&P, &META, a, b, buf, sizeof(buf)), la, lb;
		char *sa = slurp(a, &la), *sb = slurp(b, &lb);
		CHECK(ra > 0 && la == ra && lb == ra && !memcmp(sa, sb, (size_t) la),
		      "prof_write2(): the same %ld bytes in both files (%ld, %ld)", ra, la, lb);
		free(sa);
		free(sb);
		fclose(a);
		fclose(b);
		a = tmpfile();
		b = fopen("/dev/full", "w");
		if (b) {
			CHECK(prof_write2(&P, &META, a, b, buf, sizeof(buf)) == -1,
			      "prof_write2(): the second file refused gives -1");
			fclose(b);
		}
		fclose(a);
	}
	/* a second write after more samples: the counts so far, from the start */
	for (int i = 0; i < 3; i++)
		prof_sample(&P, 0x2f000100, &g);
	f = tmpfile();
	prof_write(&P, &META, f, buf, sizeof(buf));
	s = slurp(f, &len);
	fclose(f);
	CHECK(strstr(s, "\nsamples 11\n") && strstr(s, "\n2f000100 1 3\n"), "counts go on adding up");
	free(s);
}

/* ==== 5. the reader of a CPU's fields ==== */
static void test_reader(void)
{
	printf("the reader of the guest's state:\n");
	uint32_t ip = 0x1000, next_ip = 0x2000, flags = 0x202, cr0 = 0x11, sel = 0x28, base = 0;
	int cpl = 0;
	bool code16 = false, halt = false, fast_on = true, exec1_one = false;
	unsigned long laddr = 0x401000;
	uint32_t xaddr = 0x401000 ^ 0x5000;     /* linear page 401h is physical page 5h */
	long cycle = 0;
	static uint8_t ram[64 * 1024];
	struct cpui386_prof_fields f = {
		&ip, &next_ip, &flags, &cr0, &sel, &base, &cpl, &code16, &halt, &laddr, &xaddr,
		&fast_on, &exec1_one, &cycle, ram, sizeof(ram)
	};
	struct prof_guest g;
	prof_read_guest(&f, &g);
	CHECK(g.mode == PROF_MODE_PM32 && g.area == PROF_AREA_FAST && g.eip == 0x2000 &&
	      g.code_page == 0x401 && g.nop == 0, "fast, 32-bit: next_ip, the cache's page, no bytes");
	exec1_one = true;
	ip = 0x401ffd;
	ram[0x5ffd] = 0x0f;
	ram[0x5ffe] = 0x01;
	ram[0x5fff] = 0x15;
	prof_read_guest(&f, &g);
	CHECK(g.area == PROF_AREA_EXEC1 && g.eip == 0x401ffd && g.nop == 3 && g.op[0] == 0x0f &&
	      g.op[2] == 0x15, "exec1: ip, three bytes to the page's end through the mapping "
	      "(area %d eip %x nop %d)", g.area, g.eip, g.nop);
	ip = 0x402000;  /* not in the cached page: no bytes */
	prof_read_guest(&f, &g);
	CHECK(g.nop == 0, "an instruction outside the cached page: no bytes");
	ip = 0x401100;
	ram[0x5100] = 0xee;
	prof_read_guest(&f, &g);
	CHECK(g.nop == 8 && g.op[0] == 0xee, "eight bytes inside the page");
	fast_on = false;
	prof_read_guest(&f, &g);
	CHECK(g.area == PROF_AREA_ALONE && g.nop == 8, "fast_cpu = 0: alone");
	halt = true;
	prof_read_guest(&f, &g);
	CHECK(g.area == PROF_AREA_HALT && g.eip == 0x2000 && g.nop == 0, "halted: next_ip, no bytes");
	halt = false;
	flags |= 0x20000;
	cpl = 3;
	prof_read_guest(&f, &g);
	CHECK(g.mode == PROF_MODE_V86 && g.cpl == 3, "v86");
	flags &= ~0x20000u;
	code16 = true;
	prof_read_guest(&f, &g);
	CHECK(g.mode == PROF_MODE_PM16, "16-bit protected mode");
	cr0 = 0x10;
	prof_read_guest(&f, &g);
	CHECK(g.mode == PROF_MODE_REAL, "real mode");
	laddr = (unsigned long) -1;
	prof_read_guest(&f, &g);
	CHECK(g.code_page == PROF_NO_PAGE && g.nop == 0, "no cached page");
	laddr = 0x401000;
	xaddr = 0x401000 ^ 0xff000;     /* a page past the end of RAM: not read */
	prof_read_guest(&f, &g);
	CHECK(g.nop == 0, "a page outside guest RAM is not read");
	/* without I386_FAST: no fast_on or exec1_one, so tiny386 alone */
	f.fast_on = f.exec1_one = NULL;
	xaddr = 0x401000 ^ 0x5000;
	prof_read_guest(&f, &g);
	CHECK(g.area == PROF_AREA_ALONE, "no fast interpreter in the build: alone");
}

/* ==== the costs, on this PC ==== */
static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void timing(void)
{
	printf("costs on this PC (the board's own are in PROFILE.TXT: sampler_cost, last_write_us):\n");
	setup(8192, 2048, 4096, 512);           /* the board's sizes (profile.c) */
	struct prof_guest g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0, 0);
	uint32_t x = 1;
	const int N = 4000000;
	double t0 = now_s();
	for (int i = 0; i < N; i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		/* 30,000 PCs of 120 KB of code, a few hot */
		uint32_t pc = 0x2f008000 + ((x % 4 ? x % 2000 : x % 60000) * 2);
		g.code_page = 0xc0000 + (x >> 20) % 3000;
		g.area = (uint8_t) (x % 50 == 0);
		g.nop = g.area ? 4 : 0;
		g.op[0] = (uint8_t) (x >> 8);
		prof_sample(&P, pc, &g);
	}
	double t1 = now_s();
	printf("    prof_sample(): %.0f ns a sample (%d samples; tables: host %u used, coarse %u, "
	       "guest %u, ops %u)\n", (t1 - t0) * 1e9 / N, N, P.host.used, P.coarse.used,
	       P.guest.used, P.ops.used);
	char *buf = malloc(4096);
	FILE *f = fopen("/dev/null", "w");
	t0 = now_s();
	long r = prof_write(&P, &META, f, buf, 4096);
	t1 = now_s();
	fclose(f);
	printf("    prof_write() of those full tables: %ld bytes in %.2f ms (to /dev/null)\n", r,
	       (t1 - t0) * 1e3);
	free(buf);
}

/* ==== a file for profile_report.py's test ==== */
__attribute__((noinline)) int demo_hot(int n)
{
	volatile int s = 0;
	for (int i = 0; i < n; i++)
		s += i;
	return s;
}

__attribute__((noinline)) int demo_warm(int n)
{
	volatile int s = 1;
	for (int i = 0; i < n; i++)
		s *= 3;
	return s;
}

static int demo(const char *path, const char *sha)
{
	setup(1024, 64, 256, 64);
	uint32_t hot = (uint32_t) (uintptr_t) demo_hot, warm = (uint32_t) (uintptr_t) demo_warm;
	/* this program's code as the "flash" region, so that the script's
	   regions have something to say */
	prof_set_range(&P, PROF_REGION_IRAM, 0, 0);
	prof_set_range(&P, PROF_REGION_FLASH, hot < warm ? hot : warm, (hot > warm ? hot : warm) + 64);
	struct prof_meta m = META;
	m.elf_sha256 = sha;
	char buf[4096];
	struct prof_guest g = guest(PROF_AREA_FAST, PROF_MODE_PM32, 0x28, 0, 0, 0xc0001);
	for (int i = 0; i < 750; i++) {
		prof_sample(&P, hot + 4, &g);
		if (i == 499) {
			/* as Ctrl+Alt+F8 would: PATH.early, file 2; the stretch from
			   it to the end is 250 hot and 250 warm samples */
			char early[512];
			snprintf(early, sizeof(early), "%s.early", path);
			m.why = "key";
			m.file_no = 2;
			FILE *f = fopen(early, "w");
			if (!f || prof_write(&P, &m, f, buf, sizeof(buf)) < 0 || fclose(f) != 0)
				return 1;
		}
	}
	g = guest(PROF_AREA_EXEC1, PROF_MODE_V86, 0x1234, 0x12340, 0x10, PROF_NO_PAGE);
	g.nop = 1;
	g.op[0] = 0xee;
	for (int i = 0; i < 250; i++)
		prof_sample(&P, warm + 4, &g);
	m.why = "end";
	m.file_no = 3;
	FILE *f = fopen(path, "w");
	if (!f || prof_write(&P, &m, f, buf, sizeof(buf)) < 0)
		return 1;
	return fclose(f) != 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "--demo"))
		return demo(argv[2], argv[3]);
	test_tables();
	test_guest();
	test_ops();
	test_file();
	test_reader();
	if (argc > 1 && !strcmp(argv[1], "--time"))
		timing();
	printf("test_prof_hist: %d checks, %d failed\n", checks, fails);
	free(mem);
	return fails != 0;
}
