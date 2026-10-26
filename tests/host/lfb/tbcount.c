/*
 * Counts host instructions from the log of
 *
 *     qemu-riscv32-static -d in_asm,exec,nochain -D /dev/stderr PROGRAM ...
 *
 * in stretches: a stretch starts each time the code at MARK runs (the
 * program's perf_mark(), test_lfb.c), so stretch N is everything carried
 * out from the Nth call of it up to the next, the mark's own few
 * instructions included.  The blocks' instructions come from the IN:
 * listings (each block's instructions, as qemu translated it), and each
 * "Trace" line is one run of a block; nochain makes every run show.
 * Loads and stores are told by their mnemonics.  After the research
 * harness's jtrace.c (research/jit-prototype), cut down to the totals.
 *
 *     tbcount MARK_HEX < log
 *
 * prints "stretch N total T loads L stores S" for each stretch.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXBLOCK 1024
#define NSLOTS (1u << 20)

static struct block {
	uint32_t start;
	uint32_t n, loads, stores;
	int used;
} *blocks;

static struct block *find(uint32_t start, int create)
{
	uint32_t h = (start * 2654435761u) & (NSLOTS - 1);
	while (blocks[h].used) {
		if (blocks[h].start == start)
			return &blocks[h];
		h = (h + 1) & (NSLOTS - 1);
	}
	if (!create)
		return NULL;
	blocks[h].used = 1;
	blocks[h].start = start;
	return &blocks[h];
}

/* load 1, store 2, else 0 */
static int kind(const char *m)
{
	static const char *ld[] = { "lb", "lbu", "lh", "lhu", "lw", "lwsp", "lr.w", "flw",
				    "fld", "flwsp", "fldsp", NULL };
	static const char *st[] = { "sb", "sh", "sw", "swsp", "sc.w", "fsw", "fsd", "fswsp",
				    "fsdsp", NULL };
	if (!strncmp(m, "c.", 2))
		m += 2;
	for (int i = 0; ld[i]; i++)
		if (!strcmp(m, ld[i]))
			return 1;
	for (int i = 0; st[i]; i++)
		if (!strcmp(m, st[i]))
			return 2;
	return !strncmp(m, "amo", 3) ? 2 : 0;
}

static int hex8(const char *p, uint32_t *v)
{
	*v = 0;
	for (int i = 0; i < 8; i++) {
		int c = p[i], d;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return 0;
		*v = *v << 4 | d;
	}
	return 1;
}

int main(int argc, char **argv)
{
	static char buf[1 << 20];
	char line[512];
	uint64_t total = 0, loads = 0, stores = 0, unknown = 0;
	uint32_t mark, n = 0, nl = 0, ns = 0, first = 0;
	int stretch = 0, inblock = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: tbcount MARK_HEX < log\n");
		return 2;
	}
	mark = strtoul(argv[1], NULL, 16);
	blocks = calloc(NSLOTS, sizeof(*blocks));
	if (!blocks)
		return 2;
	setvbuf(stdin, buf, _IOFBF, sizeof(buf));
	while (fgets(line, sizeof(line), stdin)) {
		uint32_t pc;
		if (inblock && !(line[0] == '0' && line[1] == 'x')) {
			struct block *b = find(first, 1);
			b->n = n;
			b->loads = nl;
			b->stores = ns;
			inblock = 0;
		}
		if (!strncmp(line, "Trace ", 6)) {
			/* "Trace 0: 0x7f... [00000000/000104a4/00000000/ff020000] f" */
			const char *p = strchr(line, '[');
			if (!p || strlen(p) < 18 || p[9] != '/' || !hex8(p + 10, &pc))
				continue;
			if (pc == mark) {
				if (stretch)
					printf("stretch %d total %llu loads %llu stores %llu\n", stretch,
					       (unsigned long long) total, (unsigned long long) loads,
					       (unsigned long long) stores);
				stretch++;
				total = loads = stores = 0;
			}
			struct block *b = find(pc, 0);
			if (b && b->n) {
				total += b->n;
				loads += b->loads;
				stores += b->stores;
			} else if (stretch) {
				unknown++;
			}
		} else if (!strncmp(line, "IN:", 3)) {
			inblock = 1;
			n = nl = ns = 0;
		} else if (inblock && hex8(line + 2, &pc) && line[10] == ':') {
			/* "0x000101a0:  00001197          auipc           gp,4096" */
			char word[64], mnem[64] = "";
			if (!n)
				first = pc;
			sscanf(line + 11, "%63s %63s", word, mnem);
			int k = kind(mnem);
			n++;
			nl += k == 1;
			ns += k == 2;
		}
	}
	if (stretch)
		printf("stretch %d total %llu loads %llu stores %llu (to the end)\n", stretch,
		       (unsigned long long) total, (unsigned long long) loads,
		       (unsigned long long) stores);
	if (unknown)
		fprintf(stderr, "tbcount: %llu runs of blocks the log did not list\n",
			(unsigned long long) unknown);
	return 0;
}
