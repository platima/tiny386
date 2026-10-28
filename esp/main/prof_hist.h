/*
 * The profiler's counts (v5.1, s31/PROFILE.md): what core 0 samples of
 * core 1 a few thousand times a second while the PC runs, counted in
 * tables, and written out as PROFILE.TXT.
 *
 * Each sample is core 1's program counter (the host PC: which of the
 * firmware's instructions core 1 was at, stalls included) and the guest's
 * state at the same moment (prof_guest below).  Four tables count them:
 *
 *   host    each host PC, with what the guest's CPU was doing (area)
 *   coarse  the host PCs that found no room in host, by 64-byte line
 *   guest   the guest's code page, CS, mode, privilege level and area
 *   ops     the first bytes of the instruction tiny386's own interpreter
 *           was doing (an instruction the fast interpreter handed it, or
 *           any with fast_cpu = 0): prefixes, opcode, ModRM
 *
 * and running totals that never lose a sample (by area, mode, host region,
 * and area against region).  A table never grows: when it is full (3/4 of
 * its slots), a new key is counted as lost, and the totals still have it.
 *
 * Plain C with no ESP-IDF: tests/host/profile builds it with PROF_HOST, and
 * so does the PC's stand-in sampler.  On the board prof_sample() runs in
 * core 0's timer interrupt (PROF_IRAM: internal RAM, unless the build
 * defines PROFILE_IN_FLASH) and prof_write() on core 1 between two steps of
 * the PC, with the sampling paused.
 */
#ifndef PROF_HIST_H
#define PROF_HIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PROF_FORMAT 1

/* what the guest's CPU was doing (prof_guest.area) */
#define PROF_AREA_FAST  0       /* the fast interpreter's own work, or the loop around it */
#define PROF_AREA_EXEC1 1       /* an instruction the fast interpreter handed to cpu_exec1() */
#define PROF_AREA_ALONE 2       /* tiny386's own interpreter alone (fast_cpu = 0) */
#define PROF_AREA_HALT  3       /* the guest halted (HLT), waiting for an interrupt */
#define PROF_AREAS 4

/* the guest's mode */
#define PROF_MODE_REAL 0
#define PROF_MODE_V86  1        /* virtual-8086 mode */
#define PROF_MODE_PM16 2        /* protected mode, 16-bit code */
#define PROF_MODE_PM32 3        /* protected mode, 32-bit code */
#define PROF_MODES 4

/* where the host PC was (prof_set_range()) */
#define PROF_REGION_IRAM  0     /* code in internal RAM */
#define PROF_REGION_FLASH 1     /* code in flash, through core 1's instruction cache */
#define PROF_REGION_ROM   2     /* the chip's ROM (memcpy(), memset(), ...) */
#define PROF_REGION_OTHER 3     /* none of these (the check: should stay near 0) */
#define PROF_REGIONS 4

/* the page of a sample with none (prof_guest.code_page) */
#define PROF_NO_PAGE 0xffffffffu

/* One sample of the guest, as core 0 read it from core 1's CPU state. */
struct prof_guest {
	uint32_t cs_sel;        /* the CS selector */
	uint32_t cs_base;
	/* EXEC1 and ALONE: the instruction tiny386 is doing (cpu->ip); FAST
	   and HALT: where the fast interpreter's step began, or the next
	   instruction (cpu->next_ip): the fast interpreter keeps EIP to
	   itself until the step ends or it hands an instruction over */
	uint32_t eip;
	/* the linear page number (address >> 12) of the code being run:
	   tiny386's fetch cache, which the fast interpreter fills as it moves
	   from page to page; PROF_NO_PAGE when it holds none */
	uint32_t code_page;
	uint8_t mode, cpl, area;
	uint8_t nop;            /* bytes of op[] read: 0 when none could be */
	uint8_t op[8];          /* EXEC1 and ALONE: the instruction's first bytes */
};

/* a table's slot: host (pc, area), coarse (pc & ~63, area), guest (key,
   CS), ops (key, 0); count 0 is an empty slot */
struct prof_slot {
	uint32_t key, aux, count;
};

struct prof_table {
	struct prof_slot *s;
	uint32_t n;             /* slots, a power of 2 */
	uint32_t used;
	uint32_t max;           /* used never goes above this: n * 3 / 4 */
	uint32_t lost;          /* samples that found no slot */
};

/* the slots of each table: powers of 2 */
struct prof_sizes {
	uint32_t host, coarse, guest, ops;
};

/* the cache counters (ESP-IDF's esp_cache_cnt units: per core, the
   instruction and data buses into the flash and PSRAM cache) */
#define PROF_CACHE_UNITS 4
#define PROF_CACHE_COUNTERS 5   /* hit, miss, conflict, next level read, next level write */

struct prof {
	struct prof_table host, coarse, guest, ops;
	uint32_t samples;
	uint32_t area[PROF_AREAS];
	uint32_t mode[PROF_MODES];
	uint32_t region[PROF_REGIONS];
	uint32_t area_region[PROF_AREAS][PROF_REGIONS];
	uint32_t mode_area[PROF_MODES][PROF_AREAS];
	uint32_t op_unread;     /* EXEC1 and ALONE samples whose bytes could not be read */
	uint32_t page_guess;    /* FAST samples with no code page: the step's start used */
	uint32_t range_lo[3], range_hi[3];      /* PROF_REGION_IRAM to ROM: [lo, hi) */
	/* the cache counters, added up (prof_cache_add()) */
	uint64_t cache[PROF_CACHE_UNITS][PROF_CACHE_COUNTERS];
	uint32_t cache_have[PROF_CACHE_UNITS];  /* bit c: counter c exists */
	const char *cache_name[PROF_CACHE_UNITS];
	int8_t cache_core[PROF_CACHE_UNITS];
	char cache_kind[PROF_CACHE_UNITS];      /* 'i' instruction, 'd' data */
	uint32_t cache_reads;
	/* the sampler's own cost, where it measures it (core 0's cycles) */
	uint64_t cost_n, cost_cycles;
	uint32_t cost_max;
};

/* What a file says about where it came from (prof_write()). */
struct prof_meta {
	const char *firmware;   /* the app's version, as esp_app_get_description() has it */
	const char *board;
	const char *idf;
	const char *elf_sha256; /* the ELF the firmware was built as: its SHA-256, 64 hex digits */
	const char *sampler;    /* how the samples were taken: "core0-pc-record", "standin-sigprof" */
	const char *ini;
	const char *why;        /* "timer", "key", "end" */
	int fast_cpu;
	uint32_t rate_hz;       /* samples a second asked for */
	uint32_t file_no;       /* this run's files so far, this one too */
	uint32_t run;           /* this run's own word, drawn at random when it began */
	uint32_t cpu_mhz;
	uint64_t uptime_us;     /* since power-on */
	uint64_t sampling_us;   /* since sampling started */
	uint64_t paused_us;     /* of that, not sampled (the writes) */
	uint32_t last_write_us; /* the write before this one, from pause to resume; 0 for none */
	uint64_t guest_insns;   /* guest instructions since the PC started */
	uint64_t run_us, halt_us;       /* the speed screen's: running, and halted */
};

size_t prof_mem_bytes(const struct prof_sizes *sz);
/* The tables in mem (prof_mem_bytes() of it, 4-byte aligned), all empty. */
void prof_init(struct prof *p, void *mem, const struct prof_sizes *sz);
/* A host region: PCs in [lo, hi) count as region r (IRAM, FLASH, ROM). */
void prof_set_range(struct prof *p, int r, uint32_t lo, uint32_t hi);
/* One sample: core 1's PC and the guest at the same moment. */
void prof_sample(struct prof *p, uint32_t pc, const struct prof_guest *g);
/* the key the ops table counts the instruction of g under (prof_sample()):
   bits 0-7 ModRM, 8-15 the opcode byte, 16 after 0Fh, 17 has a ModRM, 18
   66h, 19 67h, 20 F3h, 21 F2h, 22 F0h, 23 a segment prefix, 24-25 the
   mode, 26 ALONE (else EXEC1), 27 the bytes ran out first */
uint32_t prof_op_key(const struct prof_guest *g);
/* Counter values read since the last call, added to the totals: unit u
   (0 to PROF_CACHE_UNITS - 1), counter c, have bit c set where it exists. */
void prof_cache_describe(struct prof *p, int u, const char *name, int core, char kind,
			 uint32_t have);
void prof_cache_add(struct prof *p, int u, const uint32_t v[PROF_CACHE_COUNTERS]);
/* the sampler's cost: cycles one sample took */
void prof_cost_add(struct prof *p, uint32_t cycles);

/* PROFILE.TXT (format 1, s31/PROFILE.md 4) to f, through buf (size bytes,
   at least 256).  Returns the bytes written, or -1 if f refused any. */
long prof_write(const struct prof *p, const struct prof_meta *m, FILE *f, char *buf,
		size_t size);
/* the same text to f and to f2 as well (NULL: none), made once */
long prof_write2(const struct prof *p, const struct prof_meta *m, FILE *f, FILE *f2,
		 char *buf, size_t size);

#endif /* PROF_HIST_H */
