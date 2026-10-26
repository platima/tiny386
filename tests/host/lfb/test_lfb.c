/*
 * The linear frame buffer's own path in i386.c (v5.0), on the whole PC:
 * pc_new() with this tree's pc.c, vga.c and i386.c and the Korvo-1's board
 * header, VBE at 800x480 in 16 bits set by the guest through its ports and
 * the VGA's PCI BAR, and the small guests of guest_lfb.S storing into and
 * loading from video memory every way the CPU can: 8, 16 and 32 bits at
 * every alignment, across 256-byte blocks, across pages with paging on (to
 * other video memory, to RAM, past the end of video memory, to the bank
 * window), past the end of video memory, REP STOS and REP MOVS up and down,
 * aligned and not, with 16-bit addressing, overlapping both ways, between
 * RAM, the frame buffer and the bank window, LODS, CMPS and SCAS, and with
 * the BAR taken away and moved.  Each case runs on video memory of 768K and
 * of 1M, with vbe_blocks 1 and 0.
 *
 * After each case it prints what changed: the 256-byte blocks of video
 * memory and the 4 KiB pages of RAM that differ from what was there before,
 * each with a 64-bit hash of its bytes, every dirty mark set (vga.h), the
 * flag vga_wrote, and the registers.  run_lfb.sh builds this file twice,
 * against this tree and against v4.6's (whose path went through pc.c's
 * iomem calls for every access), and the two outputs must be the same, line
 * for line.  And each build checks for itself that video memory changed
 * only where the case stores, that the marks are exactly the blocks it
 * stores into (none with vbe_blocks 0), and vga_wrote.
 *
 *   test_lfb GUEST.bin GUEST.sym            the cases
 *   test_lfb GUEST.bin GUEST.sym bench NAME  bench_NAME once (768K, marks
 *                                             on), perf_mark() at each of
 *                                             its OUT 80h, for counting
 *                                             under qemu (run_lfb.sh)
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc.h"

#define MEM (2u << 20)
#define LFB 0xE0000000u
#define PATRAM 0x100000u	/* RAM from here to the end has a pattern */

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ==== what the core wants from the board's side (BUILD_ESP32) ==== */

void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

void pcmalloc_init(void *ptr, long len)
{
	(void) ptr;
	(void) len;
}

void *bigmalloc(size_t size)
{
	return malloc(size);
}

uint32_t get_uticks(void)
{
	return 0;
}

/* ==== the CPU's callbacks, as pc_new() makes them ==== */

CPUI386 *__real_cpui386_new(int gen, char *phys_mem, long phys_mem_size, CPU_CB **cb);
static CPU_CB *the_cb;

CPUI386 *__wrap_cpui386_new(int gen, char *phys_mem, long phys_mem_size, CPU_CB **cb)
{
	CPUI386 *cpu = __real_cpui386_new(gen, phys_mem, phys_mem_size, cb);
	the_cb = *cb;
	return cpu;
}

static void (*io_write8_pc)(void *, int, u8);
static int marks;

/* OUT 80h from a bench_ guest: the counting (run_lfb.sh) cuts its log at
   each run of this function */
void __attribute__((noinline)) perf_mark(void)
{
	__asm__ volatile("" ::: "memory");
	marks++;
}

static void io_write8_hook(void *o, int port, u8 val)
{
	if (port == 0x80) {
		perf_mark();
		return;
	}
	io_write8_pc(o, port, val);
}

static void redraw(void *o, int x, int y, int w, int h)
{
	(void) o; (void) x; (void) y; (void) w; (void) h;
}

/* ==== the guest ==== */

static uint8_t *guest;
static long guest_len;
static struct sym {
	char name[64];
	uint32_t addr;
} syms[256];
static int nsyms;

static uint32_t sym(const char *name)
{
	for (int i = 0; i < nsyms; i++)
		if (!strcmp(syms[i].name, name))
			return syms[i].addr;
	fprintf(stderr, "no symbol %s\n", name);
	exit(2);
}

static void load_guest(const char *bin, const char *symfile)
{
	FILE *f = fopen(bin, "rb");
	if (!f) {
		perror(bin);
		exit(2);
	}
	guest = malloc(65536);
	guest_len = fread(guest, 1, 65536, f);
	fclose(f);
	f = fopen(symfile, "r");
	if (!f) {
		perror(symfile);
		exit(2);
	}
	char line[256], type[8], name[200];
	unsigned addr;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%x %7s %199s", &addr, type, name) == 3 && nsyms < 256 &&
		    strlen(name) < sizeof(syms[0].name)) {
			strcpy(syms[nsyms].name, name);
			syms[nsyms].addr = addr;
			nsyms++;
		}
	fclose(f);
}

/* ==== one PC ==== */

static PC *pc;
static uint8_t *ram0, *vram0;	/* each case's start */
static uint32_t vsize, vspan, dirty_n;

static uint8_t vpat(uint32_t i)
{
	return (uint8_t) ((i * 2654435761u) >> 24);
}

static uint8_t rpat(uint32_t i)
{
	return (uint8_t) (((i * 40503u) >> 8) ^ 0xa5);
}

/* the page tables at 0x20000: the first 4 MiB as they are; linear 0x400000
   onwards page by page as guest_lfb.S expects */
static void page_map(uint8_t *mem)
{
	uint32_t pd[2] = { 0x22000 | 7, 0x21000 | 7 };
	uint32_t pt1[9] = {
		LFB + 0x3000,		/* 0x400000 */
		LFB + 0x8000,		/* 0x401000 */
		0x150000,		/* 0x402000 RAM */
		LFB + 0x4000,		/* 0x403000 */
		LFB + vsize - 0x1000,	/* 0x404000 the last page */
		LFB + vsize,		/* 0x405000 past video memory */
		0xa0000,		/* 0x406000 the window */
		0x151000,		/* 0x407000 RAM */
		LFB + 0x3000,		/* 0x408000 = 0x400000 */
	};
	memcpy(mem + 0x20000, pd, sizeof(pd));
	for (uint32_t i = 0; i < 1024; i++) {
		uint32_t pte = (i << 12) | 7;
		memcpy(mem + 0x22000 + i * 4, &pte, 4);
	}
	for (int i = 0; i < 9; i++) {
		uint32_t pte = pt1[i] | 7;
		memcpy(mem + 0x21000 + i * 4, &pte, 4);
	}
}

static void prepare(void)
{
	uint8_t *mem = (uint8_t *) pc->phys_mem;
	memset(mem, 0, MEM);
	for (uint32_t i = PATRAM; i < MEM; i++)
		mem[i] = rpat(i);
	memcpy(mem + 0x10000, guest, guest_len);
	page_map(mem);
	uint32_t cfg = 0x80000000u | (uint32_t) pci_device_get_devfn(pc->pci_vga) << 8;
	memcpy(mem + 0x8000, &vsize, 4);
	memcpy(mem + 0x8004, &cfg, 4);
	for (uint32_t i = 0; i < vspan; i++)
		pc->vga_mem[i] = vpat(i);
	if (pc->vga_dirty)
		memset(pc->vga_dirty, 0, dirty_n);
	vga_wrote = 0;
	memcpy(ram0, mem, MEM);
	memcpy(vram0, pc->vga_mem, vspan);
}

static CPUI386_State st;

/* from entry to its HLT at "done"; false if it did not get there */
static bool run(uint32_t entry, int slice, long limit)
{
	cpui386_reset_pm(pc->cpu, entry);
	uint32_t done = sym("done");
	for (long n = 0; n < limit; n += slice) {
		cpui386_step(pc->cpu, slice);
		cpui386_get_state(pc->cpu, &st);
		if (st.ip == done)
			return true;
	}
	return false;
}

static uint64_t hash(const uint8_t *p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* ==== the cases: what each stores into video memory ==== */

/* a region of video memory: off bytes from the start, or from the end
   when FROM_END is set */
#define FROM_END 0x40000000
#define E(n) (FROM_END | (n))
struct region {
	uint32_t off, len;
};

static const struct expect {
	const char *name;
	bool wrote;		/* vga_wrote set at the end */
	struct region r[10];
} cases[] = {
	{ "st8", true, { { 0, 2 }, { 0x100, 1 }, { 0x1ff, 1 }, { E(1), 1 } } },
	{ "st16", true, { { 0x1000, 2 }, { 0x1101, 2 }, { 0x12ff, 2 }, { 0x13fe, 2 }, { E(2), 2 } } },
	{ "st32", true, { { 0x2000, 4 }, { 0x2101, 4 }, { 0x2202, 4 }, { 0x23fd, 4 },
			  { 0x24fe, 4 }, { 0x25ff, 4 }, { E(4), 4 } } },
	{ "rmw", true, { { 0x3000, 4 }, { 0x3101, 2 }, { 0x32ff, 1 }, { 0x3400, 4 },
			 { 0x35fa, 6 }, { 0x3700, 2 }, { 0x37fe, 4 } } },
	{ "ld", false, { } },
	{ "pgx", true, { { 0x3ffe, 2 }, { 0x8000, 2 }, { 0x8fff, 1 }, { 0x4000, 1 },
			 { E(2), 2 }, { 0, 1 }, { 0x3010, 4 }, { 0x3020, 2 }, { 0x3030, 1 } } },
	{ "stosb_f", true, { { 0x5003, 1000 } } },
	{ "stosw_f", true, { { 0x6002, 2777 * 2 } } },
	{ "stosd_f", true, { { 0x8000, 0x4000 } } },
	{ "stosd_b", true, { { 0xbf70, 400 } } },
	{ "stosw_b", true, { { 0xd00e - 299 * 2, 600 } } },
	{ "stosb_b", true, { { 0xe010 - 332, 333 } } },
	{ "stosd_mis", true, { { 0xf001, 4800 } } },
	{ "stosd_a16", true, { { 0xfff8, 8 }, { 0, 8 } } },
	{ "stosd_end", true, { { E(0x1002), 0x1002 } } },
	{ "stos_short", true, { { 0x17000, 4 }, { 0x17100, 7 } } },
	{ "stosd_pg", true, { { 0x3f00, 0x100 }, { 0x8000, 0x700 }, { E(0x80), 0x80 },
			      { 0x4000, 0x14 } } },
	{ "stos_win", true, { { 0x20100, 1200 }, { 0x2fffc, 4 }, { 0x20001, 1 }, { 0x20003, 2 },
			      { 0x20006, 4 } } },
	{ "movsd_r2l_f", true, { { 0x10000, 0x2000 } } },
	{ "movsd_r2l_b", true, { { 0x12000, 0x1000 } } },
	{ "movsw_r2l", true, { { 0x13802, 0xa02 }, { 0x14ed6, 0x132 } } },
	{ "movsb_r2l", true, { { 0x16001, 0x777 }, { 0x16edd, 0x123 } } },
	{ "movsd_l2r", false, { } },
	{ "movsd_l2l", true, { { 0x30000, 0x4000 } } },
	{ "movsd_l2l_up", true, { { 0x40000, 0x4000 } } },
	{ "movsd_l2l_rep", true, { { 0x50010, 0x400 }, { 0x51004, 0x200 } } },
	{ "movsd_l2l_down", true, { { 0x60640, 0x4000 } } },
	{ "movsd_l2l_back", true, { { 0x705fc, 0x200 } } },
	{ "movsw_l2l", true, { { 0x72fa2, 0x600 }, { 0x73b66, 0x4aa }, { 0x75000, 0x999 } } },
	{ "movsd_pg", true, { { 0x3000, 0xc00 }, { 0x3010, 0x100 }, { 0x8f80, 0x80 },
			      { E(0x100), 0x100 } } },
	{ "movsd_end", true, { { E(8), 8 } } },
	{ "movs_win", true, { { 0x10200, 0x400 }, { 0x81000, 0x400 }, { 0x12000, 0x400 } } },
	{ "win_more", true, { { 0x30800, 0x200 }, { 0x31800, 0x800 }, { 0x33001, 0x101 },
			      { 0x34002, 0x132 }, { 0x35003, 0x40 } } },
	{ "movs_short", true, { { 0x82000, 7 }, { 0x82200, 4 } } },
	{ "lods", false, { } },
	{ "bar_off", true, { { 0x100, 4 }, { 0x204, 2 }, { 0x307, 1 }, { 0x400, 0x100 },
			     { 0x800, 0x100 } } },
	{ "bar_move", true, { { 0x100, 4 }, { 0x400, 0x100 }, { 0x900, 4 }, { 0xa00, 4 } } },
};

static uint32_t roff(uint32_t off)
{
	return off & FROM_END ? vsize - (off & ~FROM_END) : off;
}

static bool in_regions(const struct expect *c, uint32_t i)
{
	for (int k = 0; k < 10 && c->r[k].len; k++) {
		uint32_t o = roff(c->r[k].off);
		if (i >= o && i - o < c->r[k].len)
			return true;
	}
	return false;
}

/* prints what the case changed; checks it against what it should */
static void report(const struct expect *c, int blocks, bool ran)
{
	uint8_t *mem = (uint8_t *) pc->phys_mem;
	printf("== %s, %uK, vbe_blocks %d: %s\n", c->name, vsize >> 10, blocks,
	       ran ? "at done" : "DID NOT GET TO done");
	CHECK(ran, "%s did not get to its HLT (eip %08x)", c->name, st.ip);
	printf("  regs");
	for (int i = 0; i < 8; i++)
		printf(" %08x", st.gpr[i]);
	printf(" flags %08x cr0 %08x\n", st.flags, st.cr0);
	printf("  vga_wrote %d\n", vga_wrote);
	CHECK(!!vga_wrote == c->wrote, "%s: vga_wrote %d", c->name, vga_wrote);

	/* video memory: the blocks changed, and nothing outside the case's
	   regions */
	int outside = 0;
	printf("  video memory changed:");
	for (uint32_t b = 0; b < vspan; b += 256) {
		uint32_t n = vspan - b < 256 ? vspan - b : 256;
		if (memcmp(pc->vga_mem + b, vram0 + b, n)) {
			printf(" %x:%016llx", b, (unsigned long long) hash((uint8_t *) pc->vga_mem + b, n));
			for (uint32_t i = b; i < b + n; i++)
				if (pc->vga_mem[i] != (char) vram0[i] && !in_regions(c, i))
					outside++;
		}
	}
	printf("\n");
	CHECK(outside == 0, "%s: %d bytes of video memory changed outside its regions", c->name,
	      outside);

	/* the marks: exactly the blocks of the regions, or none */
	if (pc->vga_dirty) {
		int wrong = 0, set = 0;
		printf("  marks:");
		for (uint32_t b = 0; b < dirty_n; b++) {
			bool want = false;
			if (blocks)
				for (int k = 0; k < 10 && c->r[k].len; k++) {
					uint32_t o = roff(c->r[k].off);
					if (b >= o >> VGA_DIRTY_SHIFT &&
					    b <= (o + c->r[k].len - 1) >> VGA_DIRTY_SHIFT)
						want = true;
				}
			if (pc->vga_dirty[b]) {
				set++;
				printf(" %x", b);
			}
			if (!!pc->vga_dirty[b] != want) {
				if (wrong < 8)
					printf("(%s %x)", want ? "missing" : "extra", b);
				wrong++;
			}
		}
		printf(" (%d)\n", set);
		CHECK(wrong == 0, "%s: %d marks not as the regions say", c->name, wrong);
	}

	/* RAM */
	printf("  RAM changed:");
	for (uint32_t p = 0; p < MEM; p += 4096)
		if (memcmp(mem + p, ram0 + p, 4096))
			printf(" %x:%016llx", p, (unsigned long long) hash(mem + p, 4096));
	printf("\n");
}

static void make_pc(uint32_t vga_mem_size)
{
	PCConfig conf;
	memset(&conf, 0, sizeof(conf));
	conf.mem_size = MEM;
	conf.vga_mem_size = vga_mem_size;
	conf.width = 800;
	conf.height = 480;
	conf.cpu_gen = 4;
	conf.fpu = 0;
	uint8_t *fb = calloc(800 * 480, 2);
	pc = pc_new(redraw, NULL, fb, &conf);
	io_write8_pc = the_cb->io_write8;
	the_cb->io_write8 = io_write8_hook;
	vsize = vga_mem_size;
	vspan = vsize + VGA_MEM_SLACK;
	uint32_t span = vspan < 0x20000 + 4 ? 0x20000 + 4 : vspan;
	dirty_n = (span >> VGA_DIRTY_SHIFT) + 1;
	ram0 = malloc(MEM);
	vram0 = malloc(vspan);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOFBF, 1 << 16);
	if (argc < 3) {
		fprintf(stderr, "usage: test_lfb GUEST.bin GUEST.sym [bench NAME]\n");
		return 2;
	}
	load_guest(argv[1], argv[2]);

	if (argc >= 5 && !strcmp(argv[3], "bench")) {
		char name[80];
		snprintf(name, sizeof(name), "bench_%s", argv[4]);
		make_pc(768 << 10);
		prepare();
		vga_dirty_blocks = 1;
		bool ran = run(sym(name), 1 << 16, 1L << 30);
		printf("%s: %s, %d marks\n", name, ran ? "at done" : "DID NOT GET TO done", marks);
		return ran ? 0 : 1;
	}

	static const uint32_t sizes[] = { 768 << 10, 1 << 20 };
	for (int z = 0; z < 2; z++) {
		make_pc(sizes[z]);
		for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
			char name[80];
			snprintf(name, sizeof(name), "case_%s", cases[k].name);
			for (int blocks = 1; blocks >= 0; blocks--) {
				prepare();
				vga_dirty_blocks = blocks;
				bool ran = run(sym(name), 1, 10000);
				report(&cases[k], blocks, ran);
			}
		}
	}
	vga_dirty_blocks = 1;
	printf("test_lfb: %d checks, %d failed\n", checks, fails);
	fflush(stdout);
	return fails ? 1 : 0;
}
