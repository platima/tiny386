/*
 * tests/lockstep: the hook in the PC harness's headless driver.
 *
 * build.sh appends this file to a copy of pc.c (never to the tree's own
 * file) and links the program with --wrap=cpui386_step --wrap=pc_new, so
 * that every step of the CPU, from whichever core the tree has, passes
 * through __wrap_cpui386_step() below.  Nothing here changes what the
 * guest sees: the hook reads the CPU's state through lockstep_cpu.inc
 * (refresh_flags() on a copy), hashes memory, and forwards every port and
 * memory-mapped access to the device unchanged.  It is off unless LS_OUT
 * names a directory.
 *
 * What it writes in LS_OUT (README.md has the formats):
 *   records.txt   a record of the whole machine at the first step boundary
 *                 at or after every LS_EVERY guest instructions, and after
 *                 every step whose end lies in [LS_DENSE_FROM, LS_DENSE_TO];
 *                 a last record (F) when the program ends
 *   irq.txt       every hardware interrupt the CPU takes: its instruction
 *                 count, vector, and the CS:EIP and flags it interrupts
 *   io.txt        every port access in the dense window
 *   children.txt  with LS_SPLIT_STEP=S: the state after 1, 2, 3 ...
 *                 instructions of step S+1, each from its own fork() of the
 *                 process at the step's start
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "lockstep.h"

void __real_cpui386_step(CPUI386 *cpu, int stepcount);
PC *__real_pc_new(SimpleFBDrawFunc *redraw, void *redraw_data, u8 *fb, PCConfig *conf);
/* the harness's disk counters (iohook.c): reads, writes, bytes read and
   written, seeks, flushes; weak, so that any driver links */
extern uint64_t pcr_io[] __attribute__((weak));

static struct {
	int on, in_child;
	char dir[512];
	FILE *rec, *irq, *io;
	int child_fd;
	uint64_t every, next;
	uint64_t dense_from, dense_to;
	int64_t split_step;
	int split_max, split_done;
	uint64_t exit_cycle;
	int64_t exit_step;
	long watch_addr;
	int watch_val, stop_val;
	uint64_t steps, nrec;
	CPUI386 *cpu;
	PC *pc;
	CPU_CB *cb;
	CPU_CB orig;
	uint8_t *fb;
	long fb_bytes;
	/* rolling hashes and counts */
	uint64_t sh;                    /* the CPU's state after every step */
	uint64_t irq_n, irq_h;
	uint64_t io_n, io_h;
	uint64_t mm_n, mm_h;            /* memory-mapped accesses (shown, not compared) */
	uint64_t last_cycle;
	uint64_t cyc_ext;               /* the instruction count, 64 bits */
	struct ls_cpu prev;             /* the state after the step before */
	double t0;
	const char *why;
	/* split: memory at the step's start */
	uint8_t *snap_ram, *snap_vram;
} ls = { .split_step = -1, .exit_step = -1, .watch_addr = -1, .stop_val = -1 };

static double ls_wall(void)
{
	struct timespec ts;
	syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* cpu->cycle is a long: 32 bits in the board's build (and the harness's
   -m32 builds); counted on here in 64, as the harness's own clock does */
static uint64_t ls_ext(uint64_t raw)
{
	ls.cyc_ext += (uint32_t) ((uint32_t) raw - (uint32_t) ls.cyc_ext);
	return ls.cyc_ext;
}

static void ls_read(CPUI386 *cpu, struct ls_cpu *s)
{
	memset(s, 0, sizeof(*s));       /* no stray padding: states are compared whole */
	lockstep_cpu_read(cpu, s);
	s->cycle = ls_ext(s->cycle);
}

static uint64_t ls_env(const char *name, uint64_t dflt)
{
	const char *v = getenv(name);
	return v && *v ? strtoull(v, NULL, 0) : dflt;
}

/* ==== hashing ==== */
static uint64_t ls_hash(const uint8_t *p, size_t n, uint64_t seed)
{
	uint64_t a = seed ^ 0x9e3779b97f4a7c15ull, b = seed ^ 0xc2b2ae3d27d4eb4full,
		c = seed ^ 0x165667b19e3779f9ull, d = seed ^ 0x27d4eb2f165667c5ull;
	const uint64_t k = 0x9fb21c651e98df25ull;
	size_t i = 0;
	for (; i + 32 <= n; i += 32) {
		uint64_t w0, w1, w2, w3;
		memcpy(&w0, p + i, 8);
		memcpy(&w1, p + i + 8, 8);
		memcpy(&w2, p + i + 16, 8);
		memcpy(&w3, p + i + 24, 8);
		a = (a ^ w0) * k; a ^= a >> 29;
		b = (b ^ w1) * k; b ^= b >> 29;
		c = (c ^ w2) * k; c ^= c >> 29;
		d = (d ^ w3) * k; d ^= d >> 29;
	}
	uint64_t t = n;
	for (; i < n; i++)
		t = (t ^ p[i]) * k, t ^= t >> 29;
	return ls_mix(a ^ ls_mix(b ^ ls_mix(c ^ ls_mix(d ^ ls_mix(t)))));
}

/* guest RAM but the devices' window: the ESP-IDF port keeps the devices'
   state (host pointers among it) in mem[0xA0020..0xC0000), which the guest
   reaches only as the VGA's MMIO window */
#define LS_HOLE_LO 0xa0020u
#define LS_HOLE_HI 0xc0000u
#define LS_REGION (1u << 20)
static uint64_t ls_ram_hash(char *rr, size_t rrsz)
{
	const uint8_t *m = (const uint8_t *) ls.pc->phys_mem;
	long size = ls.pc->phys_mem_size;
	uint64_t h = 0x452821e638d01377ull;
	int o = 0;
	for (long r = 0; r < size; r += LS_REGION) {
		long end = r + LS_REGION < size ? r + LS_REGION : size;
		uint64_t rh;
		if (r < LS_HOLE_HI && end > LS_HOLE_LO) {
			rh = ls_hash(m + r, LS_HOLE_LO - r, r);
			if (end > LS_HOLE_HI)
				rh = ls_hash(m + LS_HOLE_HI, end - LS_HOLE_HI, rh);
		} else {
			rh = ls_hash(m + r, end - r, r);
		}
		h = ls_mix(h ^ rh);
		if (rr && o < (int) rrsz - 20)
			o += snprintf(rr + o, rrsz - o, "%s%08x", o ? "," : "", (uint32_t) rh);
	}
	return h;
}

/* the marks of what the guest wrote into video memory since the last
   picture (VGA_DIRTY_BLOCKS, vga.h): what decides which rows the next
   picture draws again; 0 without them */
static uint64_t ls_dirty_hash(void)
{
#ifdef VGA_DIRTY_SHIFT
	if (ls.pc->vga_dirty)
		return ls_hash(ls.pc->vga_dirty, (size_t) ls.pc->vga_mem_size >> VGA_DIRTY_SHIFT, 13);
#endif
	return 0;
}

/* the CPU's state as one number (the step hash) */
static uint64_t ls_cpu_hash(const struct ls_cpu *s)
{
	uint64_t h = ls_mix(s->cycle);
	for (int i = 0; i < 8; i++)
		h = ls_mix(h ^ s->gpr[i] ^ ((uint64_t) i << 40));
	h = ls_mix(h ^ s->eip);
	h = ls_mix(h ^ s->flags ^ ((uint64_t) s->cpl << 32) ^ ((uint64_t) s->halt << 36) ^
		   ((uint64_t) s->intr << 37) ^ ((uint64_t) s->code16 << 38));
	for (int i = 0; i < 8; i++)
		h = ls_mix(h ^ s->seg[i].sel ^ ((uint64_t) s->seg[i].base << 32)) ^
			ls_mix(s->seg[i].limit ^ ((uint64_t) s->seg[i].flags << 32));
	h = ls_mix(h ^ s->cr0 ^ ((uint64_t) s->cr3 << 32));
	h = ls_mix(h ^ s->cr2 ^ ((uint64_t) s->sp_mask << 32));
	h = ls_mix(h ^ s->gdt_base ^ ((uint64_t) s->idt_base << 32));
	h = ls_mix(h ^ s->gdt_limit ^ ((uint64_t) s->idt_limit << 32));
	h = ls_mix(h ^ s->fpu);
	return h;
}

/* ==== the port and memory-mapped accesses, forwarded unchanged ==== */
static inline void ls_io_ev(int dir, int size, int port, uint32_t val)
{
	ls.io_n++;
	ls.io_h = ls_mix(ls.io_h ^ ((uint64_t) val << 24) ^ ((uint64_t) port << 3) ^
			 ((uint64_t) size << 1) ^ (uint64_t) dir);
	if (ls.io && !ls.in_child) {
		uint64_t c = ls_ext((unsigned long) cpui386_get_cycle(ls.cpu));
		if (c >= ls.dense_from && c <= ls.dense_to)
			fprintf(ls.io, "O n=%llu step=%llu cyc=%llu %s%d port=%04x val=%0*x\n",
				(unsigned long long) ls.io_n, (unsigned long long) ls.steps,
				(unsigned long long) c, dir ? "out" : "in", size * 8, port,
				size * 2, val);
	}
}
static inline void ls_mm_ev(int dir, int size, uint32_t addr, uint32_t val)
{
	ls.mm_n++;
	ls.mm_h = ls_mix(ls.mm_h ^ ((uint64_t) val << 32) ^ addr ^ ((uint64_t) size << 29) ^
			 ((uint64_t) dir << 28));
}
static uint32_t ls_elem(const uint8_t *p, int size)
{
	return size == 1 ? p[0] : size == 2 ? (uint32_t) (p[0] | p[1] << 8) :
		(uint32_t) (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24);
}
static u8 ls_io_read8(void *o, int a) { u8 v = ls.orig.io_read8(o, a); ls_io_ev(0, 1, a, v); return v; }
static u16 ls_io_read16(void *o, int a) { u16 v = ls.orig.io_read16(o, a); ls_io_ev(0, 2, a, v); return v; }
static u32 ls_io_read32(void *o, int a) { u32 v = ls.orig.io_read32(o, a); ls_io_ev(0, 4, a, v); return v; }
static void ls_io_write8(void *o, int a, u8 v) { ls_io_ev(1, 1, a, v); ls.orig.io_write8(o, a, v); }
static void ls_io_write16(void *o, int a, u16 v) { ls_io_ev(1, 2, a, v); ls.orig.io_write16(o, a, v); }
static void ls_io_write32(void *o, int a, u32 v) { ls_io_ev(1, 4, a, v); ls.orig.io_write32(o, a, v); }
/* a string transfer counts as the single accesses it stands for, so that a
   core that does them one at a time gives the same hash */
static int ls_io_read_string(void *o, int a, uint8_t *buf, int size, int count)
{
	int r = ls.orig.io_read_string(o, a, buf, size, count);
	for (int i = 0; i < r; i++)
		ls_io_ev(0, size, a, ls_elem(buf + i * size, size));
	return r;
}
static int ls_io_write_string(void *o, int a, uint8_t *buf, int size, int count)
{
	int r = ls.orig.io_write_string(o, a, buf, size, count);
	for (int i = 0; i < r; i++)
		ls_io_ev(1, size, a, ls_elem(buf + i * size, size));
	return r;
}
static u8 ls_mm_read8(void *o, uword a) { u8 v = ls.orig.iomem_read8(o, a); ls_mm_ev(0, 1, a, v); return v; }
static u16 ls_mm_read16(void *o, uword a) { u16 v = ls.orig.iomem_read16(o, a); ls_mm_ev(0, 2, a, v); return v; }
static u32 ls_mm_read32(void *o, uword a) { u32 v = ls.orig.iomem_read32(o, a); ls_mm_ev(0, 4, a, v); return v; }
static void ls_mm_write8(void *o, uword a, u8 v) { ls_mm_ev(1, 1, a, v); ls.orig.iomem_write8(o, a, v); }
static void ls_mm_write16(void *o, uword a, u16 v) { ls_mm_ev(1, 2, a, v); ls.orig.iomem_write16(o, a, v); }
static void ls_mm_write32(void *o, uword a, u32 v) { ls_mm_ev(1, 4, a, v); ls.orig.iomem_write32(o, a, v); }
static bool ls_mm_write_string(void *o, uword a, uint8_t *buf, int len)
{
	bool r = ls.orig.iomem_write_string(o, a, buf, len);
	ls_mm_ev(1, 8, a, (uint32_t) ls_hash(buf, len > 0 ? len : 0, (uint64_t) len << 1 | r));
	return r;
}

static void ls_cpu_line(char *b, size_t n, const struct ls_cpu *s);
static int ls_pic_pit(char *b, size_t n);

/* every hardware interrupt the CPU takes */
static int ls_pic_read_irq(void *pic)
{
	int vec = ls.orig.pic_read_irq(pic);
	struct ls_cpu s;
	ls_read(ls.cpu, &s);
	ls.irq_n++;
	ls.irq_h = ls_mix(ls.irq_h ^ s.cycle ^ ((uint64_t) (vec & 0xff) << 56) ^
			  ((uint64_t) s.eip << 20));
	if (ls.irq && !ls.in_child)
		fprintf(ls.irq, "I n=%llu step=%llu cyc=%llu ut=%u vec=%02x cs=%04x eip=%08x fl=%08x\n",
			(unsigned long long) ls.irq_n, (unsigned long long) ls.steps,
			(unsigned long long) s.cycle, get_uticks(), vec & 0xff, s.seg[1].sel, s.eip,
			s.flags);
	return vec;
}

static void ls_wrap_callbacks(void)
{
	CPU_CB *cb = ls.cb;
	ls.orig = *cb;
#define LS_WRAP(f, w) if (cb->f) cb->f = w
	LS_WRAP(pic_read_irq, ls_pic_read_irq);
	LS_WRAP(io_read8, ls_io_read8);
	LS_WRAP(io_read16, ls_io_read16);
	LS_WRAP(io_read32, ls_io_read32);
	LS_WRAP(io_write8, ls_io_write8);
	LS_WRAP(io_write16, ls_io_write16);
	LS_WRAP(io_write32, ls_io_write32);
	LS_WRAP(io_read_string, ls_io_read_string);
	LS_WRAP(io_write_string, ls_io_write_string);
	LS_WRAP(iomem_read8, ls_mm_read8);
	LS_WRAP(iomem_read16, ls_mm_read16);
	LS_WRAP(iomem_read32, ls_mm_read32);
	LS_WRAP(iomem_write8, ls_mm_write8);
	LS_WRAP(iomem_write16, ls_mm_write16);
	LS_WRAP(iomem_write32, ls_mm_write32);
	LS_WRAP(iomem_write_string, ls_mm_write_string);
#undef LS_WRAP
}

/* ==== records ==== */
static void ls_cpu_line(char *b, size_t n, const struct ls_cpu *s)
{
	static const char *sn[8] = { "es", "cs", "ss", "ds", "fs", "gs", "ldt", "tr" };
	int o = snprintf(b, n,
		"eip=%08x eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x "
		"fl=%08x cpl=%u c16=%u spm=%08x halt=%u intr=%u",
		s->eip, s->gpr[0], s->gpr[1], s->gpr[2], s->gpr[3], s->gpr[4], s->gpr[5],
		s->gpr[6], s->gpr[7], s->flags, s->cpl, s->code16, s->sp_mask, s->halt, s->intr);
	for (int i = 0; i < 8 && o < (int) n; i++)
		o += snprintf(b + o, n - o, " %s=%04x:%08x:%08x:%08x", sn[i], s->seg[i].sel,
			      s->seg[i].base, s->seg[i].limit, s->seg[i].flags);
	if (o < (int) n)
		o += snprintf(b + o, n - o,
			" gdt=%08x:%04x idt=%08x:%04x cr0=%08x cr2=%08x cr3=%08x "
			"dr=%x:%x:%x:%x:%x:%x:%x:%x sys=%x:%x:%x fpu=%s%016llx "
			"_ip=%08x _exc=%x:%x _cc=%x:%x",
			s->gdt_base, s->gdt_limit, s->idt_base, s->idt_limit, s->cr0, s->cr2, s->cr3,
			s->dr[0], s->dr[1], s->dr[2], s->dr[3], s->dr[4], s->dr[5], s->dr[6], s->dr[7],
			s->sysenter[0], s->sysenter[1], s->sysenter[2], s->has_fpu ? "" : "none:",
			(unsigned long long) s->fpu, s->ip, s->excno, s->excerr, s->cc_op, s->cc_mask);
}

static int ls_pic_pit(char *b, size_t n)
{
	uint8_t pic[LS_PIC_BYTES];
	uint32_t pit[LS_PIT_WORDS];
	int o = snprintf(b, n, "pic=");
	lockstep_pic_read(ls.pc->pic, pic);
	for (int i = 0; i < LS_PIC_BYTES; i++)
		o += snprintf(b + o, n - o, "%02x", pic[i]);
	o += snprintf(b + o, n - o, " pit=");
	lockstep_pit_read(ls.pc->pit, pit);
	for (int i = 0; i < LS_PIT_WORDS; i++)
		o += snprintf(b + o, n - o, "%s%x", i ? (i % 17 ? "." : ",") : "", pit[i]);
	return o;
}

static void ls_record(char kind, const struct ls_cpu *s, int full)
{
	char cl[1200], dev[700], rr[400];
	uint64_t ram = 0, vram = 0, fbh = 0, dirty = 0;
	rr[0] = 0;
	if (full) {
		ram = ls_ram_hash(rr, sizeof(rr));
		vram = ls_hash((const uint8_t *) ls.pc->vga_mem, ls.pc->vga_mem_size, 7);
		fbh = ls.fb ? ls_hash(ls.fb, ls.fb_bytes, 11) : 0;
		dirty = ls_dirty_hash();
	}
	ls_cpu_line(cl, sizeof(cl), s);
	ls_pic_pit(dev, sizeof(dev));
	char ramf[120];
	if (full)
		snprintf(ramf, sizeof(ramf), "ram=%016llx vram=%016llx fb=%016llx dirty=%016llx",
			 (unsigned long long) ram, (unsigned long long) vram, (unsigned long long) fbh,
			 (unsigned long long) dirty);
	else
		snprintf(ramf, sizeof(ramf), "ram=- vram=- fb=- dirty=-");
	fprintf(ls.rec, "%c i=%llu step=%llu cyc=%llu ut=%u %s sh=%016llx %s rr=%s %s "
		"irqn=%llu irqh=%016llx ion=%llu ioh=%016llx dk=%llu:%llu:%llu:%llu:%llu:%llu "
		"_mmn=%llu _mmh=%016llx _wall=%.3f%s%s\n",
		kind, (unsigned long long) ls.nrec++, (unsigned long long) ls.steps,
		(unsigned long long) s->cycle, get_uticks(), cl, (unsigned long long) ls.sh,
		ramf, full ? rr : "-", dev,
		(unsigned long long) ls.irq_n, (unsigned long long) ls.irq_h,
		(unsigned long long) ls.io_n, (unsigned long long) ls.io_h,
		(unsigned long long) (pcr_io ? pcr_io[0] : 0), (unsigned long long) (pcr_io ? pcr_io[1] : 0),
		(unsigned long long) (pcr_io ? pcr_io[2] : 0), (unsigned long long) (pcr_io ? pcr_io[3] : 0),
		(unsigned long long) (pcr_io ? pcr_io[4] : 0), (unsigned long long) (pcr_io ? pcr_io[5] : 0),
		(unsigned long long) ls.mm_n, (unsigned long long) ls.mm_h, ls_wall() - ls.t0,
		ls.why ? " _why=" : "", ls.why ? ls.why : "");
}

static void ls_flush(void)
{
	if (ls.rec) fflush(ls.rec);
	if (ls.irq) fflush(ls.irq);
	if (ls.io) fflush(ls.io);
}

static void ls_atexit(void)
{
	if (!ls.on || ls.in_child || !ls.cpu)
		return;
	struct ls_cpu s;
	ls_read(ls.cpu, &s);
	if (!ls.why)
		ls.why = "end";
	ls_record('F', &s, 1);
	ls_flush();
}

/* an emulator abort or crash: the last record and every buffer written
   first, then the harness's own handler (its state line, its picture) */
static void (*ls_prev_handler[32])(int);
static volatile int ls_dying;
static void ls_on_signal(int sig)
{
	if (ls.on && !ls.in_child && ls.cpu && !ls_dying) {
		ls_dying = 1;
		struct ls_cpu s;
		ls_read(ls.cpu, &s);
		ls.why = sig == SIGABRT ? "abort" : "crash";
		ls_record('F', &s, 1);
		ls_flush();
	}
	void (*prev)(int) = sig < 32 ? ls_prev_handler[sig] : NULL;
	if (prev && prev != SIG_DFL && prev != SIG_IGN && prev != ls_on_signal) {
		prev(sig);
	} else {
		signal(sig, SIG_DFL);
		raise(sig);
	}
}

static FILE *ls_open(const char *name)
{
	char p[700];
	snprintf(p, sizeof(p), "%s/%s", ls.dir, name);
	FILE *f = fopen(p, "w");
	if (!f) {
		fprintf(stderr, "[lockstep] %s: %s\n", p, strerror(errno));
		exit(3);
	}
	setvbuf(f, NULL, _IOFBF, 1 << 16);
	return f;
}

/* ==== the split: one child for each prefix of the step ==== */
static void ls_child(CPUI386 *cpu, int m, int n)
{
	ls.in_child = 1;
	for (int sig = 1; sig < 32; sig++)
		if (sig != SIGKILL && sig != SIGSTOP)
			signal(sig, SIG_DFL);
	alarm(30);
	/* the emulator's own lines, if any, go nowhere */
	int nul = open("/dev/null", O_WRONLY);
	if (nul >= 0) {
		dup2(nul, 1);
		dup2(nul, 2);
		close(nul);
	}
	/* every file this process can write (the disk image among them) is
	   opened again read only in this process alone: the child shares
	   nothing it could change with the parent, whose run goes on */
	DIR *d = opendir("/proc/self/fd");
	if (d) {
		struct dirent *e;
		int dfd = dirfd(d);
		while ((e = readdir(d))) {
			int fd = atoi(e->d_name);
			if (fd < 3 || fd == dfd || fd == ls.child_fd)
				continue;
			struct stat st;
			int fl = fcntl(fd, F_GETFL);
			if (fl < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
			    (fl & O_ACCMODE) == O_RDONLY)
				continue;
			char lp[64], path[4096];
			snprintf(lp, sizeof(lp), "/proc/self/fd/%d", fd);
			ssize_t k = readlink(lp, path, sizeof(path) - 1);
			if (k <= 0)
				continue;
			path[k] = 0;
			int nfd = open(path, O_RDONLY);
			if (nfd >= 0) {
				dup2(nfd, fd);
				close(nfd);
			}
		}
		closedir(d);
	}
	if (m > 0)
		__real_cpui386_step(cpu, m);
	struct ls_cpu s;
	ls_read(cpu, &s);
	static char line[16384];
	char cl[1200], dev[700], rr[400];
	uint64_t ram = ls_ram_hash(rr, sizeof(rr));
	uint64_t vram = ls_hash((const uint8_t *) ls.pc->vga_mem, ls.pc->vga_mem_size, 7);
	ls_cpu_line(cl, sizeof(cl), &s);
	ls_pic_pit(dev, sizeof(dev));
	int o = snprintf(line, sizeof(line),
		"C m=%d n=%d step=%llu cyc=%llu ut=%u %s ram=%016llx vram=%016llx dirty=%016llx %s "
		"irqn=%llu irqh=%016llx ion=%llu ioh=%016llx _mmn=%llu",
		m, n, (unsigned long long) ls.steps, (unsigned long long) s.cycle, get_uticks(), cl,
		(unsigned long long) ram, (unsigned long long) vram,
		(unsigned long long) ls_dirty_hash(), dev,
		(unsigned long long) ls.irq_n, (unsigned long long) ls.irq_h,
		(unsigned long long) ls.io_n, (unsigned long long) ls.io_h,
		(unsigned long long) ls.mm_n);
	/* what changed in memory since the step's start: runs of bytes */
	const char *names[2] = { "mem", "vmem" };
	const uint8_t *cur[2] = { (const uint8_t *) ls.pc->phys_mem, (const uint8_t *) ls.pc->vga_mem };
	const uint8_t *old[2] = { ls.snap_ram, ls.snap_vram };
	long len[2] = { ls.pc->phys_mem_size, ls.pc->vga_mem_size };
	for (int r = 0; r < 2; r++) {
		o += snprintf(line + o, sizeof(line) - o, " %s=", names[r]);
		int runs = 0, first = 1;
		for (long c = 0; c < len[r] && runs < 24; c += 4096) {
			long ce = c + 4096 < len[r] ? c + 4096 : len[r];
			if (!memcmp(cur[r] + c, old[r] + c, ce - c))
				continue;
			for (long i = c; i < ce && runs < 24; i++) {
				if (cur[r][i] == old[r][i])
					continue;
				if (r == 0 && i >= LS_HOLE_LO && i < LS_HOLE_HI)
					continue;
				long j = i;
				while (j + 1 < ce && cur[r][j + 1] != old[r][j + 1] && j + 1 - i < 16)
					j++;
				o += snprintf(line + o, sizeof(line) - o, "%s%lx:", first ? "" : ",", i);
				for (long q = i; q <= j; q++)
					o += snprintf(line + o, sizeof(line) - o, "%02x", old[r][q]);
				o += snprintf(line + o, sizeof(line) - o, ">");
				for (long q = i; q <= j; q++)
					o += snprintf(line + o, sizeof(line) - o, "%02x", cur[r][q]);
				first = 0;
				runs++;
				i = j;
			}
		}
		if (first)
			o += snprintf(line + o, sizeof(line) - o, "-");
	}
	/* the next instruction's bytes, last: reading them can touch the
	   page tables' accessed bits */
	uint8_t code[16];
	int k = lockstep_cpu_code(cpu, code, 16);
	o += snprintf(line + o, sizeof(line) - o, " code=");
	for (int i = 0; i < k; i++)
		o += snprintf(line + o, sizeof(line) - o, "%02x", code[i]);
	if (!k)
		o += snprintf(line + o, sizeof(line) - o, "-");
	o += snprintf(line + o, sizeof(line) - o, "\n");
	if (write(ls.child_fd, line, o) != o)
		_exit(2);
	_exit(0);
}

static void ls_split(CPUI386 *cpu, int n)
{
	char p[700];
	snprintf(p, sizeof(p), "%s/children.txt", ls.dir);
	ls.child_fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
	if (ls.child_fd < 0) {
		fprintf(stderr, "[lockstep] %s: %s\n", p, strerror(errno));
		exit(3);
	}
	ls_flush();
	fflush(stdout);
	fflush(stderr);
	ls.snap_ram = malloc(ls.pc->phys_mem_size);
	ls.snap_vram = malloc(ls.pc->vga_mem_size);
	memcpy(ls.snap_ram, ls.pc->phys_mem, ls.pc->phys_mem_size);
	memcpy(ls.snap_vram, ls.pc->vga_mem, ls.pc->vga_mem_size);
	int max = ls.split_max > 0 ? ls.split_max : n + 2;
	for (int m = 0; m <= max; m++) {
		pid_t pid = fork();
		if (pid == 0)
			ls_child(cpu, m, n);
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) {
			char b[200];
			int k = snprintf(b, sizeof(b), "C m=%d n=%d step=%llu dead=%s%d\n", m, n,
					 (unsigned long long) ls.steps,
					 pid < 0 ? "fork" : WIFSIGNALED(st) ? "signal" : "exit",
					 pid < 0 ? errno : WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
			if (write(ls.child_fd, b, k) != k)
				break;
		}
	}
	free(ls.snap_ram);
	free(ls.snap_vram);
	ls.snap_ram = ls.snap_vram = NULL;
	close(ls.child_fd);
	ls.child_fd = -1;
}

/* ==== the wraps ==== */
PC *__wrap_pc_new(SimpleFBDrawFunc *redraw, void *redraw_data, u8 *fb, PCConfig *conf)
{
	PC *pc = __real_pc_new(redraw, redraw_data, fb, conf);
	const char *dir = getenv("LS_OUT");
	if (!dir || !*dir)
		return pc;
	ls.on = 1;
	ls.pc = pc;
	ls.t0 = ls_wall();
	snprintf(ls.dir, sizeof(ls.dir), "%s", dir);
	mkdir(ls.dir, 0755);
#ifndef BPP
#define BPP 32
#endif
	ls.fb = fb;
	ls.fb_bytes = (long) conf->width * conf->height * (BPP / 8);
	ls.every = ls_env("LS_EVERY", 1000000);
	ls.next = 0;
	ls.dense_from = ls_env("LS_DENSE_FROM", UINT64_MAX);
	ls.dense_to = ls_env("LS_DENSE_TO", 0);
	ls.split_step = getenv("LS_SPLIT_STEP") ? (int64_t) ls_env("LS_SPLIT_STEP", 0) : -1;
	ls.split_max = (int) ls_env("LS_SPLIT_MAX", 0);
	ls.exit_cycle = ls_env("LS_EXIT_CYCLE", UINT64_MAX);
	ls.exit_step = getenv("LS_EXIT_STEP") ? (int64_t) ls_env("LS_EXIT_STEP", 0) : -1;
	const char *sb = getenv("LS_STOP_BYTE");    /* addr:value */
	if (sb && *sb) {
		ls.watch_addr = strtol(sb, NULL, 0);
		const char *c = strchr(sb, ':');
		ls.stop_val = c ? (int) strtol(c + 1, NULL, 0) : -1;
		ls.watch_val = -1;
	}
	ls.child_fd = -1;
	ls.rec = ls_open("records.txt");
	setvbuf(ls.rec, NULL, _IOLBF, 1 << 16);
	ls.irq = ls_open("irq.txt");
	if (ls.dense_from <= ls.dense_to)
		ls.io = ls_open("io.txt");
	fprintf(ls.rec, "H lockstep=1 every=%llu dense=%llu:%llu split=%lld exit=%llu:%lld "
		"mem=%ld vga_mem=%d fb=%dx%dx%d stop=%lx:%d\n",
		(unsigned long long) ls.every, (unsigned long long) ls.dense_from,
		(unsigned long long) ls.dense_to, (long long) ls.split_step,
		(unsigned long long) ls.exit_cycle, (long long) ls.exit_step, pc->phys_mem_size,
		pc->vga_mem_size, conf->width, conf->height, BPP, ls.watch_addr, ls.stop_val);
	atexit(ls_atexit);
	{
		static const int sigs[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
		for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
			ls_prev_handler[sigs[i]] = signal(sigs[i], ls_on_signal);
	}
	return pc;
}

static void ls_attach(CPUI386 *cpu)
{
	ls.cpu = cpu;
	ls.cb = lockstep_cpu_cb(cpu);
	ls_wrap_callbacks();
	ls.irq_h = ls.io_h = ls.mm_h = 0x13198a2e03707344ull;
	ls.sh = 0xa4093822299f31d0ull;
	struct ls_cpu s;
	ls_read(cpu, &s);
	ls.prev = s;
	ls_record('R', &s, 1);
	if (ls.every)
		ls.next = (s.cycle / ls.every + 1) * ls.every;
	ls.last_cycle = s.cycle;
}

void __wrap_cpui386_step(CPUI386 *cpu, int stepcount)
{
	if (!ls.on || cpu != ls.pc->cpu) {
		__real_cpui386_step(cpu, stepcount);
		return;
	}
	if (!ls.cpu)
		ls_attach(cpu);
	if (ls.split_step >= 0 && !ls.split_done && (int64_t) ls.steps == ls.split_step) {
		ls.split_done = 1;
		ls_split(cpu, stepcount);
	}
	__real_cpui386_step(cpu, stepcount);
	ls.steps++;

	struct ls_cpu s;
	ls_read(cpu, &s);
	/* the step hash takes in each state the CPU passes through at a step
	   boundary; a step that changed nothing (a halted CPU, most of an idle
	   Windows' steps) adds nothing, in both runs alike */
	if (memcmp(&s, &ls.prev, sizeof(s))) {
		ls.sh = ls_mix(ls.sh ^ ls_cpu_hash(&s));
		ls.prev = s;
	}
	int dense = s.cycle >= ls.dense_from && s.cycle <= ls.dense_to;
	int periodic = ls.every && s.cycle >= ls.next;
	if (dense || periodic) {
		/* in a halt the cycle stands still and only devices run: a dense
		   record then leaves out the memory hashes (both runs alike) */
		int full = periodic || s.cycle != ls.last_cycle;
		ls_record('R', &s, full);
		if (periodic)
			ls.next = (s.cycle / ls.every + 1) * ls.every;
	}
	ls.last_cycle = s.cycle;
	if (ls.watch_addr >= 0 && ls.watch_addr < ls.pc->phys_mem_size) {
		int v = (uint8_t) ls.pc->phys_mem[ls.watch_addr];
		if (v != ls.watch_val) {
			fprintf(ls.rec, "M step=%llu cyc=%llu addr=%lx val=%02x\n",
				(unsigned long long) ls.steps, (unsigned long long) s.cycle,
				ls.watch_addr, v);
			ls.watch_val = v;
			if (v == ls.stop_val) {
				ls.why = "stop-byte";
				exit(0);
			}
		}
	}
	if (s.cycle > ls.exit_cycle || (ls.exit_step >= 0 && (int64_t) ls.steps >= ls.exit_step)) {
		ls.why = "exit-point";
		exit(0);
	}
}
