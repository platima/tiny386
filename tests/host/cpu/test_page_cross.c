/*
 * 16 and 32-bit accesses that cross a 4 KiB page (v4.6): each byte goes
 * where its own physical address says.  i386.c sent the whole access by
 * its first byte.  Without paging, a dword stored at 9FFFFh put three
 * bytes into phys_mem from 0xA0000, where the board keeps the devices'
 * state (pcmalloc), instead of giving them to the VGA.  With paging, the
 * part on the second page went into phys_mem with no test at all: into
 * any page of that window, or past the end of guest RAM.  Loads read the
 * same bytes back.
 *
 * Small guests run on i386.c with 16 MiB of memory and 64 KiB past it
 * that the CPU must not touch.  The window (0xA0000 to 0xBFFFF) is filled
 * with A5h and the 64 KiB past RAM with 5Ah; the VGA's callbacks log what
 * they are given and read as 77h.  The guests come from the checks of
 * 2026-10-25 (cases A to M; N is new).
 *
 *   test_page_cross [before]
 *
 * "before": i386.c as v4.5 had it, where the bytes for the window and past
 * RAM have to land in phys_mem (the test sees the fault), and the
 * crossings from RAM to RAM have to be the same as now.  Exit status 0
 * when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i386.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
	} else { \
		printf("  ok: "); \
	} \
	printf(__VA_ARGS__); \
	printf("\n"); \
} while (0)

/* i386.c's allocations on the board (BUILD_ESP32) */
void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

#define MEM (16u << 20)
#define PAST 65536u
static uint8_t *mem;

/* what the VGA was given */
static struct {
	int bits;
	uword addr;
	u32 val;
} wr[32];
static int nwr, nstr;
static uword str_addr;
static int str_len;

static void logw(int bits, uword a, u32 v)
{
	if (nwr < 32) {
		wr[nwr].bits = bits;
		wr[nwr].addr = a;
		wr[nwr].val = v;
	}
	nwr++;
}

static u8 mm_r8(void *o, uword a) { return 0x77; }
static u16 mm_r16(void *o, uword a) { return 0x7777; }
static u32 mm_r32(void *o, uword a) { return 0x77777777; }
static void mm_w8(void *o, uword a, u8 v) { logw(8, a, v); }
static void mm_w16(void *o, uword a, u16 v) { logw(16, a, v); }
static void mm_w32(void *o, uword a, u32 v) { logw(32, a, v); }
static bool mm_wstr(void *o, uword a, uint8_t *b, int len)
{
	nstr++;
	str_addr = a;
	str_len = len;
	return true;
}
static int pic_read(void *p) { return 8; }
static u8 io_r8(void *o, int a) { return 0xbe; }
static u16 io_r16(void *o, int a) { return 0xbabe; }
static u32 io_r32(void *o, int a) { return 0xcafebabe; }
static void io_w8(void *o, int a, u8 v) { }
static void io_w16(void *o, int a, u16 v) { }
static void io_w32(void *o, int a, u32 v) { }
static int io_rs(void *o, int a, uint8_t *b, int size, int count) { return 0; }
static int io_ws(void *o, int a, uint8_t *b, int size, int count) { return 0; }

static CPUI386_State st;

/* Runs a guest to its HLT: in real mode from 0:7C00 (a far jump at the
   reset vector), or in 32-bit protected mode from 0x10000 with page
   tables laid out for it to turn paging on.  Linear 0x400000 up:
     0x400000 RAM 9F000h,  0x401000 window A1000h,
     0x402000 RAM 7F000h,  0x403000 the first page past RAM,
     0x404000 RAM 9F000h,  0x405000 window A0000h,
     0x406000 RAM 30000h,  0x407000 RAM 50000h;
   the first 4 MiB as they are.  False if the guest did not stop at its
   HLT, its last byte (the state's EIP is the HLT's own). */
static bool run(const char *name, const uint8_t *code, int len, bool pm)
{
	memset(mem, 0, MEM);
	memset(mem + 0xa0000, 0xa5, 0x20000);
	memset(mem + MEM, 0x5a, PAST);
	nwr = nstr = 0;
	printf("== %s\n", name);

	CPU_CB *cb;
	CPUI386 *cpu = cpui386_new(3, (char *) mem, MEM, &cb);
	cb->pic = NULL;
	cb->pic_read_irq = pic_read;
	cb->io = NULL;
	cb->io_read8 = io_r8;
	cb->io_write8 = io_w8;
	cb->io_read16 = io_r16;
	cb->io_write16 = io_w16;
	cb->io_read32 = io_r32;
	cb->io_write32 = io_w32;
	cb->io_read_string = io_rs;
	cb->io_write_string = io_ws;
	cb->iomem = NULL;
	cb->iomem_read8 = mm_r8;
	cb->iomem_write8 = mm_w8;
	cb->iomem_read16 = mm_r16;
	cb->iomem_write16 = mm_w16;
	cb->iomem_read32 = mm_r32;
	cb->iomem_write32 = mm_w32;
	cb->iomem_write_string = mm_wstr;
	uword end;
	if (pm) {
		uint32_t pd[2] = { 0x22000 | 7, 0x21000 | 7 };
		uint32_t pt1[8] = { 0x9f000, 0xa1000, 0x7f000, MEM, 0x9f000, 0xa0000, 0x30000, 0x50000 };
		memcpy(mem + 0x20000, pd, sizeof(pd));
		for (uint32_t i = 0; i < 1024; i++) {
			uint32_t pte = (i << 12) | 7;
			memcpy(mem + 0x22000 + i * 4, &pte, 4);
		}
		for (int i = 0; i < 8; i++) {
			pt1[i] |= 7;
			memcpy(mem + 0x21000 + i * 4, &pt1[i], 4);
		}
		cpui386_reset_pm(cpu, 0x10000);
		memcpy(mem + 0x10000, code, len);
		end = 0x10000 + len - 1;
	} else {
		static const uint8_t jmp[] = { 0xea, 0x00, 0x7c, 0x00, 0x00 }; /* jmp 0:7C00 */
		cpui386_reset(cpu);
		memcpy(mem + 0xffff0, jmp, sizeof(jmp));
		memcpy(mem + 0x7c00, code, len);
		end = 0x7c00 + len - 1;
	}
	for (int i = 0; i < 64; i++)
		cpui386_step(cpu, 1);
	cpui386_get_state(cpu, &st);
	cpui386_delete(cpu);

	printf("  eax %08x, eip %08x, VGA given:", st.gpr[0], st.ip);
	for (int i = 0; i < nwr && i < 32; i++) {
		if (wr[i].bits == 8)
			printf(" %x=%02x", wr[i].addr, wr[i].val);
		else
			printf(" %x/%d=%x", wr[i].addr, wr[i].bits, wr[i].val);
	}
	if (nstr)
		printf(" string %x len %d", str_addr, str_len);
	printf("\n  window and past RAM changed:");
	for (uint32_t i = 0xa0000; i < 0xc0000; i++)
		if (mem[i] != 0xa5)
			printf(" %x=%02x", i, mem[i]);
	for (uint32_t i = MEM; i < MEM + PAST; i++)
		if (mem[i] != 0x5a)
			printf(" %x=%02x", i, mem[i]);
	printf("\n");
	return st.ip == end;
}

static int changed_window(void)
{
	int n = 0;
	for (uint32_t i = 0xa0000; i < 0xc0000; i++)
		n += mem[i] != 0xa5;
	return n;
}

static int changed_past(void)
{
	int n = 0;
	for (uint32_t i = MEM; i < MEM + PAST; i++)
		n += mem[i] != 0x5a;
	return n;
}

/* "a0000=33 a0001=22": the VGA was given exactly these writes, in this
   order, a byte each ("bfffe/32=11223344" for a dword); "" for none */
static bool vga_is(const char *want)
{
	int n = 0, used;
	unsigned a, v, bits;
	for (const char *p = want; *p; p += used) {
		while (*p == ' ')
			p++;
		bits = 8;
		if (sscanf(p, "%x/%u=%x%n", &a, &bits, &v, &used) != 3) {
			bits = 8;
			if (sscanf(p, "%x=%x%n", &a, &v, &used) != 2)
				return false;
		}
		if (n >= nwr || wr[n].bits != (int) bits || wr[n].addr != a || wr[n].val != v)
			return false;
		n++;
	}
	return n == nwr && nstr == 0;
}

/* "9ffff=44 a0000=33": these bytes of mem[], RAM, window or past RAM */
static bool mem_is(const char *want)
{
	int used;
	unsigned a, v;
	for (const char *p = want; *p; p += used) {
		while (*p == ' ')
			p++;
		if (sscanf(p, "%x=%x%n", &a, &v, &used) != 2 || a >= MEM + PAST || mem[a] != v)
			return false;
	}
	return true;
}

#define PAGING_ON \
	0xb8, 0x00, 0x00, 0x02, 0x00,		/* mov eax, 20000h */ \
	0x0f, 0x22, 0xd8,			/* mov cr3, eax */ \
	0x0f, 0x20, 0xc0,			/* mov eax, cr0 */ \
	0x0d, 0x00, 0x00, 0x00, 0x80,		/* or eax, 80000000h */ \
	0x0f, 0x22, 0xc0			/* mov cr0, eax */

#define RUN(name, code, pm) run(name, code, sizeof(code), pm)

int main(int argc, char **argv)
{
	bool before = argc > 1 && !strcmp(argv[1], "before");
	bool ran;
	setvbuf(stdout, NULL, _IONBF, 0);
	mem = malloc(MEM + PAST);
	if (!mem)
		return 2;

	/* A to H and K, M, N: accesses into the window or past RAM */
	static const uint8_t a[] = {
		0xb8, 0x00, 0x90,		/* mov ax, 9000h */
		0x8e, 0xd8,			/* mov ds, ax */
		0x66, 0xb8, 0x44, 0x33, 0x22, 0x11, /* mov eax, 11223344h */
		0x66, 0xa3, 0xff, 0xff,		/* mov [0ffffh], eax */
		0xf4 };
	ran = RUN("A: real mode, a dword stored at 9FFFFh", a, false);
	if (before)
		CHECK(ran && mem_is("9ffff=44 a0000=33 a0001=22 a0002=11") && changed_window() == 3 &&
		      vga_is(""), "v4.5: three bytes into phys_mem at A0000h, none to the VGA");
	else
		CHECK(ran && mem_is("9ffff=44") && changed_window() == 0 &&
		      vga_is("a0000=33 a0001=22 a0002=11"), "one byte to RAM, three to the VGA");

	static const uint8_t b[] = {
		0xb8, 0x00, 0x90,		/* mov ax, 9000h */
		0x8e, 0xc0,			/* mov es, ax */
		0xbf, 0xff, 0xff,		/* mov di, 0ffffh */
		0xba, 0xf0, 0x01,		/* mov dx, 1f0h */
		0x66, 0x6d,			/* insd */
		0xf4 };
	ran = RUN("B: real mode, INSD to 9000:FFFF", b, false);
	if (before)
		CHECK(ran && mem_is("9ffff=be a0000=ba a0001=fe a0002=ca") && changed_window() == 3 &&
		      vga_is(""), "v4.5: three bytes into phys_mem at A0000h");
	else
		CHECK(ran && mem_is("9ffff=be") && changed_window() == 0 &&
		      vga_is("a0000=ba a0001=fe a0002=ca"), "one byte to RAM, three to the VGA");

	static const uint8_t c[] = {
		0xb8, 0x00, 0x90,		/* mov ax, 9000h */
		0x8e, 0xc0,			/* mov es, ax */
		0xbf, 0xfd, 0xff,		/* mov di, 0fffdh */
		0xb9, 0x02, 0x00,		/* mov cx, 2 */
		0xba, 0xf0, 0x01,		/* mov dx, 1f0h */
		0xf3, 0x6d,			/* rep insw */
		0xf4 };
	ran = RUN("C: real mode, REP INSW of two words from 9000:FFFD", c, false);
	if (before)
		CHECK(ran && mem_is("9fffd=be 9fffe=ba 9ffff=be a0000=ba") && changed_window() == 1 &&
		      vga_is(""), "v4.5: one byte into phys_mem at A0000h");
	else
		CHECK(ran && mem_is("9fffd=be 9fffe=ba 9ffff=be") && changed_window() == 0 &&
		      vga_is("a0000=ba"), "three bytes to RAM, one to the VGA");

	static const uint8_t d[] = {
		PAGING_ON,
		0xb8, 0x44, 0x33, 0x22, 0x11,	/* mov eax, 11223344h */
		0xa3, 0xff, 0x0f, 0x40, 0x00,	/* mov [400fffh], eax: RAM 9F000h, then window A1000h */
		0xb8, 0x88, 0x77, 0x66, 0x55,	/* mov eax, 55667788h */
		0xa3, 0xff, 0x2f, 0x40, 0x00,	/* mov [402fffh], eax: RAM 7F000h, then past RAM */
		0xb8, 0xcc, 0xbb, 0xaa, 0x99,	/* mov eax, 99aabbcch */
		0xa3, 0xfd, 0x4f, 0x40, 0x00,	/* mov [404ffdh], eax: RAM 9F000h, then window A0000h */
		0xf4 };
	ran = RUN("D: paging on, three dwords split over two pages", d, true);
	if (before)
		CHECK(ran && mem_is("7ffff=88 9fffd=cc 9fffe=bb 9ffff=aa a1000=33 a1001=22 a1002=11 a0000=99") &&
		      changed_window() == 4 && mem_is("1000000=77 1000001=66 1000002=55") &&
		      changed_past() == 3 && vga_is(""),
		      "v4.5: four bytes into the window (A1000h and A0000h), three past RAM");
	else
		CHECK(ran && mem_is("7ffff=88 9fffd=cc 9fffe=bb 9ffff=aa") && changed_window() == 0 &&
		      changed_past() == 0 && vga_is("a1000=33 a1001=22 a1002=11 a0000=99"),
		      "the window's bytes to the VGA, those past RAM nowhere");

	static const uint8_t e[] = {
		PAGING_ON,
		0xa1, 0xff, 0x0f, 0x40, 0x00,	/* mov eax, [400fffh]: RAM 9F000h, then window A1000h */
		0xf4 };
	ran = RUN("E: paging on, a dword loaded over two pages", e, true);
	if (before)
		CHECK(ran && st.gpr[0] == 0xa5a5a500, "v4.5: three bytes of phys_mem from A1000h (%08x)",
		      st.gpr[0]);
	else
		CHECK(ran && st.gpr[0] == 0x77777700, "one byte from RAM, three from the VGA (%08x)",
		      st.gpr[0]);

	static const uint8_t g[] = {
		0xb8, 0xff, 0x9f,		/* mov ax, 9fffh */
		0x8e, 0xc0,			/* mov es, ax */
		0xbf, 0x0d, 0x00,		/* mov di, 0dh */
		0xb9, 0x02, 0x00,		/* mov cx, 2 */
		0xf3, 0xa5,			/* rep movsw, from 0:0 */
		0xf4 };
	ran = RUN("G: real mode, REP MOVSW of two words to 9FFF:000D (misaligned)", g, false);
	if (before)
		CHECK(ran && mem_is("a0000=00") && changed_window() == 1 && vga_is(""),
		      "v4.5: one byte into phys_mem at A0000h");
	else
		CHECK(ran && changed_window() == 0 && vga_is("a0000=00"), "one byte to the VGA");

	static const uint8_t h[] = {
		0xb8, 0xff, 0x9f,		/* mov ax, 9fffh */
		0x8e, 0xd0,			/* mov ss, ax */
		0xbc, 0x13, 0x00,		/* mov sp, 13h */
		0x66, 0x6a, 0xee,		/* push dword -12h: 9FFFFh to A0002h */
		0xf4 };
	ran = RUN("H: real mode, a dword pushed across A0000h", h, false);
	if (before)
		CHECK(ran && mem_is("9ffff=ee a0000=ff a0001=ff a0002=ff") && changed_window() == 3 &&
		      vga_is(""), "v4.5: three bytes into phys_mem at A0000h");
	else
		CHECK(ran && mem_is("9ffff=ee") && changed_window() == 0 &&
		      vga_is("a0000=ff a0001=ff a0002=ff"), "one byte to RAM, three to the VGA");

	static const uint8_t k[] = {
		0xb8, 0x00, 0x90,		/* mov ax, 9000h */
		0x8e, 0xd8,			/* mov ds, ax */
		0x66, 0xa1, 0xfe, 0xff,		/* mov eax, [0fffeh] */
		0xf4 };
	ran = RUN("K: real mode, a dword loaded from 9FFFEh", k, false);
	if (before)
		CHECK(ran && st.gpr[0] == 0xa5a50000, "v4.5: two bytes of phys_mem from A0000h (%08x)",
		      st.gpr[0]);
	else
		CHECK(ran && st.gpr[0] == 0x77770000, "two bytes from RAM, two from the VGA (%08x)",
		      st.gpr[0]);

	static const uint8_t m[] = {
		0xb8, 0x00, 0xb0,		/* mov ax, 0b000h */
		0x8e, 0xd8,			/* mov ds, ax */
		0x66, 0xb8, 0x44, 0x33, 0x22, 0x11, /* mov eax, 11223344h */
		0x66, 0xa3, 0xfe, 0xff,		/* mov [0fffeh], eax: BFFFEh to C0001h */
		0xf4 };
	ran = RUN("M: real mode, a dword stored from the window's top into C0000h", m, false);
	if (before)
		CHECK(ran && mem_is("c0000=00 c0001=00") && changed_window() == 0 &&
		      vga_is("bfffe/32=11223344"), "v4.5: all four bytes to the VGA");
	else
		CHECK(ran && mem_is("c0000=22 c0001=11") && changed_window() == 0 &&
		      vga_is("bfffe=44 bffff=33"), "two bytes to the VGA, two to RAM at C0000h");

	static const uint8_t n[] = {
		0xb8, 0x44, 0x33, 0x22, 0x11,	/* mov eax, 11223344h */
		0xa3, 0xfe, 0xff, 0xff, 0x00,	/* mov [0fffffeh], eax: the last two bytes of RAM */
		0x31, 0xc0,			/* xor eax, eax */
		0xa1, 0xfe, 0xff, 0xff, 0x00,	/* mov eax, [0fffffeh] */
		0xf4 };
	ran = RUN("N: protected mode, paging off, a dword stored and loaded at the end of RAM", n, true);
	if (before)
		CHECK(ran && mem_is("fffffe=44 ffffff=33 1000000=22 1000001=11") && changed_past() == 2 &&
		      st.gpr[0] == 0x11223344, "v4.5: two bytes past RAM, and read back (%08x)", st.gpr[0]);
	else
		CHECK(ran && mem_is("fffffe=44 ffffff=33") && changed_past() == 0 &&
		      st.gpr[0] == 0x00003344, "two bytes to RAM, none past it, read as 0 (%08x)",
		      st.gpr[0]);

	static const uint8_t n2[] = {
		0x66, 0xc7, 0x05, 0xff, 0xff, 0xff, 0x00, 0xcd, 0xab, /* mov word [0ffffffh], 0abcdh */
		0x31, 0xc0,			/* xor eax, eax */
		0x66, 0xa1, 0xff, 0xff, 0xff, 0x00, /* mov ax, [0ffffffh] */
		0xf4 };
	ran = RUN("N2: the same with a word at the last byte of RAM", n2, true);
	if (before)
		CHECK(ran && mem_is("ffffff=cd 1000000=ab") && changed_past() == 1 &&
		      st.gpr[0] == 0xabcd, "v4.5: one byte past RAM, and read back (%08x)", st.gpr[0]);
	else
		CHECK(ran && mem_is("ffffff=cd") && changed_past() == 0 && st.gpr[0] == 0x00cd,
		      "one byte to RAM, none past it (%08x)", st.gpr[0]);

	/* F, I, J1 to J4: the same as before */
	static const uint8_t f[] = {
		0xb8, 0xff, 0x9f,		/* mov ax, 9fffh */
		0x8e, 0xc0,			/* mov es, ax */
		0xbf, 0x08, 0x00,		/* mov di, 8 */
		0xb9, 0x04, 0x00,		/* mov cx, 4 */
		0x66, 0xf3, 0xa5,		/* rep movsd, from 0:0 */
		0xf4 };
	ran = RUN("F: real mode, REP MOVSD of four dwords to 9FFF:0008 (aligned)", f, false);
	CHECK(ran && changed_window() == 0 && nwr == 0 && nstr == 1 && str_addr == 0xa0000 &&
	      str_len == 8, "the window's eight bytes to the VGA as a string, as before");

	static const uint8_t i_[] = {
		0xb8, 0x00, 0x01,		/* mov ax, 100h */
		0x8e, 0xd8,			/* mov ds, ax */
		0x66, 0xb8, 0x44, 0x33, 0x22, 0x11, /* mov eax, 11223344h */
		0x66, 0xa3, 0xfe, 0x0f,		/* mov [0ffeh], eax: 1FFEh */
		0x66, 0x31, 0xc0,		/* xor eax, eax */
		0x66, 0xa1, 0xfe, 0x0f,		/* mov eax, [0ffeh] */
		0xf4 };
	ran = RUN("I: real mode, a dword stored and loaded across 2000h", i_, false);
	CHECK(ran && st.gpr[0] == 0x11223344 && mem_is("1ffe=44 1fff=33 2000=22 2001=11") &&
	      vga_is(""), "RAM to RAM (%08x)", st.gpr[0]);

	static const uint8_t j1[] = {
		PAGING_ON,
		0xb8, 0x88, 0x77, 0x66, 0x55,	/* mov eax, 55667788h */
		0xa3, 0xfe, 0x1f, 0x00, 0x00,	/* mov [1ffeh], eax */
		0x31, 0xc0,			/* xor eax, eax */
		0xa1, 0xfe, 0x1f, 0x00, 0x00,	/* mov eax, [1ffeh] */
		0xf4 };
	ran = RUN("J1: paging on, a dword at 1FFEh", j1, true);
	CHECK(ran && st.gpr[0] == 0x55667788 && mem_is("1ffe=88 1fff=77 2000=66 2001=55") &&
	      vga_is(""), "RAM to RAM (%08x)", st.gpr[0]);

	static const uint8_t j2[] = {
		PAGING_ON,
		0xb8, 0xd4, 0xc3, 0xb2, 0xa1,	/* mov eax, 0a1b2c3d4h */
		0xa3, 0xff, 0x1f, 0x00, 0x00,	/* mov [1fffh], eax */
		0x31, 0xc0,			/* xor eax, eax */
		0xa1, 0xff, 0x1f, 0x00, 0x00,	/* mov eax, [1fffh] */
		0xf4 };
	ran = RUN("J2: paging on, a dword at 1FFFh", j2, true);
	CHECK(ran && st.gpr[0] == 0xa1b2c3d4 && mem_is("1fff=d4 2000=c3 2001=b2 2002=a1") &&
	      vga_is(""), "RAM to RAM (%08x)", st.gpr[0]);

	static const uint8_t j3[] = {
		PAGING_ON,
		0xb8, 0x0d, 0xf0, 0xad, 0xde,	/* mov eax, 0deadf00dh */
		0xa3, 0xfd, 0x1f, 0x00, 0x00,	/* mov [1ffdh], eax */
		0x66, 0xc7, 0x05, 0xff, 0x1f, 0x00, 0x00, 0xcd, 0xab, /* mov word [1fffh], 0abcdh */
		0x31, 0xc0,			/* xor eax, eax */
		0xa1, 0xfd, 0x1f, 0x00, 0x00,	/* mov eax, [1ffdh] */
		0xf4 };
	ran = RUN("J3: paging on, a dword at 1FFDh, then a word at 1FFFh", j3, true);
	CHECK(ran && st.gpr[0] == 0xabcdf00d && mem_is("1ffd=0d 1ffe=f0 1fff=cd 2000=ab") &&
	      vga_is(""), "RAM to RAM (%08x)", st.gpr[0]);

	static const uint8_t j4[] = {
		PAGING_ON,
		0xb8, 0x44, 0x33, 0x22, 0x11,	/* mov eax, 11223344h */
		0xa3, 0xfe, 0x6f, 0x40, 0x00,	/* mov [406ffeh], eax: RAM 30000h, then RAM 50000h */
		0x66, 0xc7, 0x05, 0xff, 0x6f, 0x40, 0x00, 0xcd, 0xab, /* mov word [406fffh], 0abcdh */
		0x31, 0xc0,			/* xor eax, eax */
		0xa1, 0xfe, 0x6f, 0x40, 0x00,	/* mov eax, [406ffeh] */
		0xf4 };
	ran = RUN("J4: paging on, a dword and a word over two pages far apart", j4, true);
	CHECK(ran && st.gpr[0] == 0x11abcd44 && mem_is("30ffe=44 30fff=cd 50000=ab 50001=11") &&
	      mem_is("31000=00 31001=00 4ffff=00") && vga_is(""),
	      "each part in its own page (%08x)", st.gpr[0]);

	printf("test_page_cross%s: %d checks, %d failed\n", before ? " (before)" : "", checks, fails);
	return fails ? 1 : 0;
}
