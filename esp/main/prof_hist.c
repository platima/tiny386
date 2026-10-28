/*
 * The profiler's counts: see prof_hist.h and s31/PROFILE.md.
 */
#include <string.h>

#include "prof_hist.h"

/* prof_sample() and what it calls run in core 0's timer interrupt: in
   internal RAM on the board, unless the build says otherwise */
#if defined(PROF_HOST) || defined(PROFILE_IN_FLASH)
#define PROF_IRAM
#else
#include "esp_attr.h"
#define PROF_IRAM IRAM_ATTR
#endif

/* slots looked at before a table counts a key as lost (with at most 3/4
   of the slots used, a key not there is found missing after about 8.5 on
   average, one there after about 2.5) */
#define PROF_PROBES 16

size_t prof_mem_bytes(const struct prof_sizes *sz)
{
	return (size_t) (sz->host + sz->coarse + sz->guest + sz->ops) * sizeof(struct prof_slot);
}

static void table_init(struct prof_table *t, struct prof_slot *s, uint32_t n)
{
	t->s = s;
	t->n = n;
	t->used = 0;
	t->max = n - n / 4;
	t->lost = 0;
	memset(s, 0, (size_t) n * sizeof(*s));
}

void prof_init(struct prof *p, void *mem, const struct prof_sizes *sz)
{
	struct prof_slot *s = mem;
	memset(p, 0, sizeof(*p));
	table_init(&p->host, s, sz->host);
	s += sz->host;
	table_init(&p->coarse, s, sz->coarse);
	s += sz->coarse;
	table_init(&p->guest, s, sz->guest);
	s += sz->guest;
	table_init(&p->ops, s, sz->ops);
	for (int r = 0; r < 3; r++)
		p->range_lo[r] = p->range_hi[r] = 0;
	for (int u = 0; u < PROF_CACHE_UNITS; u++) {
		p->cache_name[u] = NULL;
		p->cache_core[u] = -1;
		p->cache_kind[u] = '-';
	}
}

void prof_set_range(struct prof *p, int r, uint32_t lo, uint32_t hi)
{
	if (r >= 0 && r < 3) {
		p->range_lo[r] = lo;
		p->range_hi[r] = hi;
	}
}

static inline uint32_t PROF_IRAM hash(uint32_t key, uint32_t aux)
{
	uint32_t h = (key ^ (aux * 0x85ebca77u)) * 0x9e3779b1u;
	return h ^ (h >> 15);
}

/* one more for (key, aux); false if it found no slot */
static bool PROF_IRAM add(struct prof_table *t, uint32_t key, uint32_t aux)
{
	uint32_t mask = t->n - 1;
	uint32_t h = hash(key, aux);
	for (int i = 0; i < PROF_PROBES; i++) {
		struct prof_slot *s = &t->s[(h + i) & mask];
		if (s->count == 0) {
			if (t->used >= t->max)
				break;
			s->key = key;
			s->aux = aux;
			s->count = 1;
			t->used++;
			return true;
		}
		if (s->key == key && s->aux == aux) {
			s->count++;
			return true;
		}
	}
	t->lost++;
	return false;
}

/* whether an opcode has a ModRM byte: one byte, and after 0Fh */
static inline bool PROF_IRAM modrm1(uint8_t b)
{
	if (b < 0x40)
		return (b & 7) < 4;     /* the ALU rows' x0 to x3 and x8 to xB */
	if ((b >= 0x80 && b <= 0x8f) || (b >= 0xd0 && b <= 0xd3) || (b >= 0xd8 && b <= 0xdf))
		return true;
	switch (b) {
	case 0x62: case 0x63: case 0x69: case 0x6b: case 0xc0: case 0xc1: case 0xc4:
	case 0xc5: case 0xc6: case 0xc7: case 0xf6: case 0xf7: case 0xfe: case 0xff:
		return true;
	}
	return false;
}

static inline bool PROF_IRAM modrm2(uint8_t b)
{
	if (b <= 0x03 || b == 0x0d || (b >= 0x10 && b <= 0x2f) || (b >= 0x90 && b <= 0x9f) ||
	    (b >= 0xb0 && b <= 0xc7) || b >= 0xd0)
		return true;
	if (b >= 0x40 && b <= 0x7f)
		return b != 0x77;
	return b == 0xa3 || b == 0xa4 || b == 0xa5 || (b >= 0xab && b <= 0xaf);
}

uint32_t PROF_IRAM prof_op_key(const struct prof_guest *g)
{
	uint32_t k = (uint32_t) (g->mode & 3) << 24;
	if (g->area == PROF_AREA_ALONE)
		k |= 1u << 26;
	int i = 0, n = g->nop > 8 ? 8 : g->nop;
	for (int np = 0; ; np++) {
		if (i >= n || np > 4)
			return k | 1u << 27;
		uint8_t b = g->op[i];
		uint32_t bit = 0;
		switch (b) {
		case 0x66: bit = 1u << 18; break;
		case 0x67: bit = 1u << 19; break;
		case 0xf3: bit = 1u << 20; break;
		case 0xf2: bit = 1u << 21; break;
		case 0xf0: bit = 1u << 22; break;
		case 0x26: case 0x2e: case 0x36: case 0x3e: case 0x64: case 0x65:
			bit = 1u << 23;
			break;
		}
		if (!bit)
			break;
		k |= bit;
		i++;
	}
	uint8_t op = g->op[i++];
	bool m;
	if (op == 0x0f) {
		if (i >= n)
			return k | 0x0fu << 8 | 1u << 27;
		op = g->op[i++];
		k |= 1u << 16;
		m = modrm2(op);
	} else {
		m = modrm1(op);
	}
	k |= (uint32_t) op << 8;
	if (m) {
		k |= 1u << 17;
		if (i >= n)
			return k | 1u << 27;
		k |= g->op[i];
	}
	return k;
}

void PROF_IRAM prof_sample(struct prof *p, uint32_t pc, const struct prof_guest *g)
{
	int area = g->area & 3, mode = g->mode & 3;
	int region = PROF_REGION_OTHER;
	for (int r = 0; r < 3; r++) {
		if (pc - p->range_lo[r] < p->range_hi[r] - p->range_lo[r]) {
			region = r;
			break;
		}
	}
	p->samples++;
	p->area[area]++;
	p->mode[mode]++;
	p->region[region]++;
	p->area_region[area][region]++;
	p->mode_area[mode][area]++;

	if (!add(&p->host, pc, (uint32_t) area))
		add(&p->coarse, pc & ~63u, (uint32_t) area);

	/* the guest: its code page, as tiny386's fetch cache has it while the
	   fast interpreter runs, else the page of CS:EIP */
	uint32_t page, guess = 0;
	if (area == PROF_AREA_FAST && g->code_page != PROF_NO_PAGE) {
		page = g->code_page & 0xfffff;
	} else {
		page = (g->cs_base + g->eip) >> 12;
		if (area == PROF_AREA_FAST) {
			guess = 1;
			p->page_guess++;
		}
	}
	uint32_t key = page | (uint32_t) mode << 20 | (uint32_t) (g->cpl & 3) << 22 |
		(uint32_t) area << 24 | guess << 26;
	add(&p->guest, key, g->cs_sel & 0xffff);

	if (area == PROF_AREA_EXEC1 || area == PROF_AREA_ALONE) {
		if (g->nop)
			add(&p->ops, prof_op_key(g), 0);
		else
			p->op_unread++;
	}
}

void prof_cache_describe(struct prof *p, int u, const char *name, int core, char kind,
			 uint32_t have)
{
	if (u < 0 || u >= PROF_CACHE_UNITS)
		return;
	p->cache_name[u] = name;
	p->cache_core[u] = (int8_t) core;
	p->cache_kind[u] = kind;
	p->cache_have[u] = have;
}

void PROF_IRAM prof_cache_add(struct prof *p, int u, const uint32_t v[PROF_CACHE_COUNTERS])
{
	if (u < 0 || u >= PROF_CACHE_UNITS)
		return;
	for (int c = 0; c < PROF_CACHE_COUNTERS; c++)
		p->cache[u][c] += v[c];
	if (u == 0)
		p->cache_reads++;
}

void PROF_IRAM prof_cost_add(struct prof *p, uint32_t cycles)
{
	p->cost_n++;
	p->cost_cycles += cycles;
	if (cycles > p->cost_max)
		p->cost_max = cycles;
}

/* ==== the file ==== */

/* text into buf, out to f (and f2, if there is one) a buffer at a time */
struct out {
	FILE *f, *f2;
	char *buf;
	size_t size, n;
	long total;
	bool err;
};

static void out_flush(struct out *o)
{
	if (o->n && !o->err) {
		if (fwrite(o->buf, 1, o->n, o->f) != o->n ||
		    (o->f2 && fwrite(o->buf, 1, o->n, o->f2) != o->n))
			o->err = true;
		else
			o->total += (long) o->n;
	}
	o->n = 0;
}

static void out_ch(struct out *o, char c)
{
	if (o->n >= o->size)
		out_flush(o);
	o->buf[o->n++] = c;
}

static void out_s(struct out *o, const char *s)
{
	while (*s)
		out_ch(o, *s++);
}

/* a string with no line break in it (a name from the firmware or the ini) */
static void out_str(struct out *o, const char *s)
{
	if (!s || !*s) {
		out_ch(o, '-');
		return;
	}
	for (; *s; s++)
		out_ch(o, *s == '\n' || *s == '\r' ? ' ' : *s);
}

static void out_u(struct out *o, uint64_t v)
{
	char t[24];
	int n = 0;
	do {
		t[n++] = (char) ('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		out_ch(o, t[--n]);
}

static void out_x(struct out *o, uint32_t v, int digits)
{
	static const char hex[] = "0123456789abcdef";
	for (int i = digits - 1; i >= 0; i--)
		out_ch(o, hex[(v >> (4 * i)) & 15]);
}

static void out_kv(struct out *o, const char *k, uint64_t v)
{
	out_ch(o, ' ');
	out_s(o, k);
	out_ch(o, ' ');
	out_u(o, v);
}

static const char *const area_names[PROF_AREAS] = { "fast", "exec1", "alone", "halt" };
static const char *const mode_names[PROF_MODES] = { "real", "v86", "pm16", "pm32" };
static const char *const region_names[PROF_REGIONS] = { "iram", "flash", "rom", "other" };
static const char *const counter_names[PROF_CACHE_COUNTERS] = {
	"hit", "miss", "conflict", "nxtlvl_rd", "nxtlvl_wr"
};

static void out_table_line(struct out *o, const char *name, const struct prof_table *t)
{
	out_s(o, "table ");
	out_s(o, name);
	out_kv(o, "slots", t->n);
	out_kv(o, "used", t->used);
	out_kv(o, "lost", t->lost);
	out_ch(o, '\n');
}

long prof_write(const struct prof *p, const struct prof_meta *m, FILE *f, char *buf,
		size_t size)
{
	return prof_write2(p, m, f, NULL, buf, size);
}

long prof_write2(const struct prof *p, const struct prof_meta *m, FILE *f, FILE *f2,
		 char *buf, size_t size)
{
	struct out o = { f, f2, buf, size, 0, 0, false };
	if (!f || !buf || size < 256)
		return -1;

	out_s(&o, "tiny386 profile ");
	out_u(&o, PROF_FORMAT);
	out_s(&o, "\nfirmware ");
	out_str(&o, m->firmware);
	out_s(&o, "\nboard ");
	out_str(&o, m->board);
	out_s(&o, "\nidf ");
	out_str(&o, m->idf);
	out_s(&o, "\nelf_sha256 ");
	out_str(&o, m->elf_sha256);
	out_s(&o, "\nsampler ");
	out_str(&o, m->sampler);
	out_ch(&o, ' ');
	out_u(&o, m->rate_hz);
	out_s(&o, "\nini ");
	out_str(&o, m->ini);
	out_s(&o, "\nfast_cpu ");
	out_u(&o, (uint64_t) (m->fast_cpu ? 1 : 0));
	out_s(&o, "\nfile ");
	out_u(&o, m->file_no);
	out_ch(&o, ' ');
	out_str(&o, m->why);
	out_s(&o, "\ncpu_mhz ");
	out_u(&o, m->cpu_mhz);
	out_s(&o, "\ntime_us");
	out_kv(&o, "uptime", m->uptime_us);
	out_kv(&o, "sampling", m->sampling_us);
	out_kv(&o, "paused", m->paused_us);
	out_s(&o, "\nlast_write_us ");
	out_u(&o, m->last_write_us);
	out_s(&o, "\nguest");
	out_kv(&o, "insns", m->guest_insns);
	out_kv(&o, "run_us", m->run_us);
	out_kv(&o, "halt_us", m->halt_us);
	out_s(&o, "\nsampler_cost");
	out_kv(&o, "samples", p->cost_n);
	out_kv(&o, "cycles", p->cost_cycles);
	out_kv(&o, "max", p->cost_max);
	out_ch(&o, '\n');
	for (int r = 0; r < 3; r++) {
		out_s(&o, "range ");
		out_s(&o, region_names[r]);
		out_ch(&o, ' ');
		out_x(&o, p->range_lo[r], 8);
		out_ch(&o, ' ');
		out_x(&o, p->range_hi[r], 8);
		out_ch(&o, '\n');
	}
	out_s(&o, "samples ");
	out_u(&o, p->samples);
	out_s(&o, "\narea");
	for (int a = 0; a < PROF_AREAS; a++)
		out_kv(&o, area_names[a], p->area[a]);
	out_s(&o, "\nmode");
	for (int i = 0; i < PROF_MODES; i++)
		out_kv(&o, mode_names[i], p->mode[i]);
	out_s(&o, "\nregion");
	for (int r = 0; r < PROF_REGIONS; r++)
		out_kv(&o, region_names[r], p->region[r]);
	out_s(&o, "\narea_region");
	for (int a = 0; a < PROF_AREAS; a++) {
		out_ch(&o, ' ');
		out_s(&o, area_names[a]);
		for (int r = 0; r < PROF_REGIONS; r++) {
			out_ch(&o, ' ');
			out_u(&o, p->area_region[a][r]);
		}
	}
	out_s(&o, "\nmode_area");
	for (int i = 0; i < PROF_MODES; i++) {
		out_ch(&o, ' ');
		out_s(&o, mode_names[i]);
		for (int a = 0; a < PROF_AREAS; a++) {
			out_ch(&o, ' ');
			out_u(&o, p->mode_area[i][a]);
		}
	}
	out_s(&o, "\nop_unread ");
	out_u(&o, p->op_unread);
	out_s(&o, "\npage_guess ");
	out_u(&o, p->page_guess);
	out_ch(&o, '\n');
	out_table_line(&o, "host", &p->host);
	out_table_line(&o, "coarse", &p->coarse);
	out_table_line(&o, "guest", &p->guest);
	out_table_line(&o, "ops", &p->ops);
	for (int u = 0; u < PROF_CACHE_UNITS; u++) {
		if (!p->cache_name[u])
			continue;
		out_s(&o, "cache ");
		out_str(&o, p->cache_name[u]);
		out_ch(&o, ' ');
		if (p->cache_core[u] < 0)
			out_ch(&o, '-');
		else
			out_u(&o, (uint64_t) p->cache_core[u]);
		out_ch(&o, ' ');
		out_ch(&o, p->cache_kind[u]);
		for (int c = 0; c < PROF_CACHE_COUNTERS; c++) {
			out_ch(&o, ' ');
			out_s(&o, counter_names[c]);
			out_ch(&o, ' ');
			if (p->cache_have[u] & (1u << c))
				out_u(&o, p->cache[u][c]);
			else
				out_ch(&o, '-');
		}
		out_ch(&o, '\n');
	}
	out_s(&o, "cache_reads ");
	out_u(&o, p->cache_reads);
	out_ch(&o, '\n');

	/* the tables: one line a slot in use, in the table's order */
	out_s(&o, "[host] pc area count\n");
	for (uint32_t i = 0; i < p->host.n; i++) {
		const struct prof_slot *s = &p->host.s[i];
		if (!s->count)
			continue;
		out_x(&o, s->key, 8);
		out_ch(&o, ' ');
		out_u(&o, s->aux);
		out_ch(&o, ' ');
		out_u(&o, s->count);
		out_ch(&o, '\n');
	}
	out_s(&o, "[coarse] line area count\n");
	for (uint32_t i = 0; i < p->coarse.n; i++) {
		const struct prof_slot *s = &p->coarse.s[i];
		if (!s->count)
			continue;
		out_x(&o, s->key, 8);
		out_ch(&o, ' ');
		out_u(&o, s->aux);
		out_ch(&o, ' ');
		out_u(&o, s->count);
		out_ch(&o, '\n');
	}
	out_s(&o, "[guest] area mode cpl cs page guess count\n");
	for (uint32_t i = 0; i < p->guest.n; i++) {
		const struct prof_slot *s = &p->guest.s[i];
		if (!s->count)
			continue;
		out_u(&o, (s->key >> 24) & 3);
		out_ch(&o, ' ');
		out_u(&o, (s->key >> 20) & 3);
		out_ch(&o, ' ');
		out_u(&o, (s->key >> 22) & 3);
		out_ch(&o, ' ');
		out_x(&o, s->aux, 4);
		out_ch(&o, ' ');
		out_x(&o, s->key & 0xfffff, 5);
		out_ch(&o, ' ');
		out_u(&o, (s->key >> 26) & 1);
		out_ch(&o, ' ');
		out_u(&o, s->count);
		out_ch(&o, '\n');
	}
	out_s(&o, "[ops] key count\n");
	for (uint32_t i = 0; i < p->ops.n; i++) {
		const struct prof_slot *s = &p->ops.s[i];
		if (!s->count)
			continue;
		out_x(&o, s->key, 8);
		out_ch(&o, ' ');
		out_u(&o, s->count);
		out_ch(&o, '\n');
	}
	out_s(&o, "end\n");
	out_flush(&o);
	if (!o.err && (fflush(f) != 0 || (f2 && fflush(f2) != 0)))
		o.err = true;
	return o.err ? -1 : o.total;
}
