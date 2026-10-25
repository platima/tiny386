/*
 * ISA DMA writes and the 0xA0000 window (v4.6).  The board keeps the
 * devices' state in phys_mem from 0xA0000 to 0xBFFFF (pcmalloc), where the
 * CPU's own stores go to the VGA instead.  i8257_dma_write_memory() wrote
 * there with no test, so a guest that pointed a DMA channel into the window
 * (SB16 command E2h writes one byte to channel 1's address) wrote over the
 * devices' state.  Now those bytes are dropped; the rest of a transfer goes
 * to RAM as before.
 *
 * i8257.c on its own, with 16 MiB of memory and 64 KiB past it.  The window
 * is filled with A5h and the 64 KiB past RAM with 5Ah.  Channel 1 is set up
 * through the controller's ports as a guest sets it up.
 *
 *   test_dma_window [before]
 *
 * "before": i8257.c as v4.5 had it, where the bytes for the window have to
 * land in phys_mem (the test sees the fault), and the rest has to be the
 * same as now.  Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i8257.h"

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

#define MEM (16u << 20)
#define PAST 65536u
static uint8_t *mem;
static I8257State *dma;

/* channel 1: mode register, then its address and page as a guest writes
   them (ports 0Bh, 0Ch, 02h twice, 83h) */
static void prog(int mode, int page, int addr)
{
	memset(mem, 0, MEM);
	memset(mem + 0xa0000, 0xa5, 0x20000);
	memset(mem + MEM, 0x5a, PAST);
	i8257_write_cont(dma, 0x03, mode, 1);
	i8257_write_cont(dma, 0x04, 0, 1);
	i8257_write_chan(dma, 0x02, addr & 0xff, 1);
	i8257_write_chan(dma, 0x02, (addr >> 8) & 0xff, 1);
	i8257_write_page(dma, 0x03, page);
}

static int dma_write(const char *bytes, int len)
{
	uint8_t buf[16];
	memcpy(buf, bytes, len);
	int r = i8257_dma_write_memory(dma, 1, buf, 0, len);
	printf("  window and past RAM changed:");
	for (uint32_t i = 0xa0000; i < 0xc0000; i++)
		if (mem[i] != 0xa5)
			printf(" %x=%02x", i, mem[i]);
	for (uint32_t i = MEM; i < MEM + PAST; i++)
		if (mem[i] != 0x5a)
			printf(" %x=%02x", i, mem[i]);
	printf("\n");
	return r;
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

/* "9ffff=02 a0000=03": these bytes of mem[] */
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

/* mode register values for channel 1: single mode, write to memory (01b
   in bits 3 and 2), address up or down; and a verify transfer */
#define UP 0x45
#define DOWN 0x65
#define VERIFY 0x41

int main(int argc, char **argv)
{
	bool before = argc > 1 && !strcmp(argv[1], "before");
	int r;
	setvbuf(stdout, NULL, _IONBF, 0);
	mem = malloc(MEM + PAST);
	if (!mem)
		return 2;
	dma = i8257_new((char *) mem, MEM, 0x00, 0x80, 0x480, 0);

	printf("== D1: four bytes from 9FFFEh, address going up\n");
	prog(UP, 0x09, 0xfffe);
	r = dma_write("\x01\x02\x03\x04", 4);
	if (before)
		CHECK(r == 4 && mem_is("9fffe=01 9ffff=02 a0000=03 a0001=04") && changed_window() == 2,
		      "v4.5: two bytes into phys_mem at A0000h");
	else
		CHECK(r == 4 && mem_is("9fffe=01 9ffff=02") && changed_window() == 0,
		      "two bytes to RAM, the two for the window dropped (%d)", r);

	printf("== D2: one byte at A1234h, as SB16 command E2h writes it\n");
	prog(UP, 0x0a, 0x1234);
	r = dma_write("\xee", 1);
	if (before)
		CHECK(r == 1 && mem_is("a1234=ee") && changed_window() == 1,
		      "v4.5: the byte into phys_mem at A1234h");
	else
		CHECK(r == 1 && changed_window() == 0, "the byte dropped (%d)", r);

	printf("== D3: four bytes ending at A0001h, address going down\n");
	prog(DOWN, 0x0a, 0x0002);
	r = dma_write("\x11\x12\x13\x14", 4);
	if (before)
		CHECK(r == 4 && mem_is("9fffe=11 9ffff=12 a0000=13 a0001=14") && changed_window() == 2,
		      "v4.5: two bytes into phys_mem at A0000h");
	else
		CHECK(r == 4 && mem_is("9fffe=11 9ffff=12") && changed_window() == 0,
		      "two bytes to RAM, the two for the window dropped (%d)", r);

	printf("== D4: four bytes from BFFFEh, out of the window's top\n");
	prog(UP, 0x0b, 0xfffe);
	r = dma_write("\x31\x32\x33\x34", 4);
	if (before)
		CHECK(r == 4 && mem_is("bfffe=31 bffff=32 c0000=33 c0001=34") && changed_window() == 2,
		      "v4.5: two bytes into phys_mem at BFFFEh");
	else
		CHECK(r == 4 && mem_is("c0000=33 c0001=34") && changed_window() == 0,
		      "the two for the window dropped, two to RAM at C0000h (%d)", r);

	/* the same as before */
	printf("== D5: four bytes at 12340h, a buffer in RAM\n");
	prog(UP, 0x01, 0x2340);
	r = dma_write("\x21\x22\x23\x24", 4);
	CHECK(r == 4 && mem_is("12340=21 12341=22 12342=23 12343=24 1233f=00 12344=00") &&
	      changed_window() == 0, "all four to RAM, as before (%d)", r);

	printf("== D6: four bytes from FFFFFEh, the end of RAM\n");
	prog(UP, 0xff, 0xfffe);
	r = dma_write("\x41\x42\x43\x44", 4);
	CHECK(r == 4 && mem_is("fffffe=41 ffffff=42") && changed_past() == 0 &&
	      changed_window() == 0, "two to RAM, none past it, as before (%d)", r);

	printf("== D7: a verify transfer at A0000h\n");
	prog(VERIFY, 0x0a, 0x0000);
	r = dma_write("\x51\x52\x53\x54", 4);
	CHECK(r == 4 && changed_window() == 0, "nothing written, as before (%d)", r);

	printf("test_dma_window%s: %d checks, %d failed\n", before ? " (before)" : "", checks, fails);
	return fails ? 1 : 0;
}
