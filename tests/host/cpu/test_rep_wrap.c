/*
 * REP STOS, MOVS, INS and OUTS with 16-bit addresses across the 64 KiB
 * wrap (v5.2).  i386.c does a REP in runs that end at a page's end, and
 * looked at SI and DI again only after each run.  With the segment's base
 * off a page boundary a 16-bit SI or DI wraps inside a page, so the run
 * went on past FFFFh to the segment's base + 10000h.  QEMU (7.2, -cpu 486)
 * wraps, as the 386 does.  i386_fast.h hands a piece that would wrap to
 * tiny386's own, so the fix is in i386.c alone.
 *
 * guest_rep_wrap.S does each of them in real mode, forwards and backwards;
 * the answers below are QEMU's for the same guest.
 *
 *   test_rep_wrap guest.bin [before]
 *
 * Runs the guest with tiny386's interpreter and with the fast one.  Every
 * answer must be QEMU's; with "before" (a copy of i386.c without the fix)
 * every answer must be the one of running on, which shows the test sees
 * the bug.  Exit status 0 when every check passes.
 */
#define I386_FAST
#include "i386.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSZ (8u << 20)

void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

static int pic_read(void *o) { return 8; }
static char e9[64];
static int ne9;
static void out8(void *o, int port, u8 v)
{
	if (port == 0xe9 && ne9 < (int) sizeof(e9) - 1)
		e9[ne9++] = v;
}
static u8 in8(void *o, int port) { return 0xff; }
uint32_t get_uticks(void) { return 0; }

static const char *what[9] = {
	"STOSW forward: ES:0",
	"STOSW forward: ES + 10000h",
	"STOSB backward: ES:FFFEh",
	"STOSB backward: ES - 2",
	"MOVSW forward, the source wrapping: what came after DS:FFFFh",
	"MOVSD (a16) forward, the destination wrapping: ES:0",
	"MOVSD (a16) forward, the destination wrapping: ES + 10000h",
	"INSB forward: ES:0 (low) and ES + 10000h (high)",
	"MOVSB backward, the source wrapping below 0",
};
/* QEMU's, and those of running on past the wrap (v5.1's i386.c) */
static const uint32_t want[9] = {
	0xbeefbeef, 0, 0x7777, 0, 0x44443333, 0xc3c3c3c3, 0, 0x0000ffff, 0x22114433,
};
static const uint32_t runon[9] = {
	0, 0xbeefbeef, 0, 0x7777, 0x66665555, 0, 0xc3c3c3c3, 0xffff0000, 0x22116655,
};

static int checks, fails;

static void check(int ok, const char *fmt, const char *a, int fast)
{
	checks++;
	if (!ok)
		fails++;
	printf("  %s: ", ok ? "ok" : "FAIL");
	printf(fmt, a, fast);
	printf("\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: test_rep_wrap guest.bin [before]\n");
		return 2;
	}
	int before = argc > 2 && !strcmp(argv[2], "before");
	static uint8_t guest[0x8000];
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	size_t len = fread(guest, 1, sizeof(guest), f);
	fclose(f);
	if (!len)
		return 2;

	for (int fast = 0; fast <= 1; fast++) {
		char *m = calloc(1, MEMSZ);
		memcpy(m + 0x1000, guest, len);
		static const uint8_t jmp[] = { 0xea, 0x00, 0x10, 0x00, 0x00 };
		memcpy(m + 0xffff0, jmp, sizeof(jmp));
		CPU_CB *cb;
		CPUI386 *cpu = cpui386_new(3, m, MEMSZ, &cb);
		cb->pic_read_irq = pic_read;
		cb->io_write8 = out8;
		cb->io_read8 = in8;
		cpui386_set_fast(cpu, fast);
		ne9 = 0;
		memset(e9, 0, sizeof(e9));
		for (int i = 0; i < 2000 && !*(uint32_t *) (m + 0x7fc); i++)
			cpui386_step(cpu, 1000);
		check(*(uint32_t *) (m + 0x7fc) == 1, "%sthe guest ran to its end (fast_cpu %d)", "", fast);
		const uint32_t *r = (const uint32_t *) (m + 0x800);
		for (int i = 0; i < 9; i++) {
			uint32_t w = before ? runon[i] : want[i];
			char line[160];
			snprintf(line, sizeof(line), "%s %08x (%s %08x)", what[i], r[i],
				 before ? "running on:" : "QEMU:", w);
			check(r[i] == w, "%s, fast_cpu %d", line, fast);
		}
		char line[96];
		snprintf(line, sizeof(line), "OUTSB forward to port E9h: \"%s\" (%s)", e9,
			 before ? "running on: \"abXY\"" : "QEMU: \"abcd\"");
		check(!strcmp(e9, before ? "abXY" : "abcd"), "%s, fast_cpu %d", line, fast);
		free(m);
	}
	printf("test_rep_wrap: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
