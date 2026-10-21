/*
 * Host test of esp/main/speed.c's counting (FIRMWARE-PLAN 3.5), built with
 * SPEED_HOST: a pretend guest runs BENCH.COM's marks with known
 * instruction counts and times, some of it halted.
 *
 *   thousand instructions a second each second, halted time apart;
 *   the marks: each piece from its mark to the next, halted time left
 *     out, 0xAF ends the table once, a second run starts a new one, a
 *     byte outside 0xA1 to 0xAF is not a mark; only a run that begins
 *     with 0xA1 counts (a lone 0xAF, or 0xA5 then 0xAF, is no BENCH), and
 *     a run cut short and begun again gives the second run's table;
 *   the lines, character for character against the Linux route's own
 *     printf formats (tiny386-korvo.patch, print_marks() and
 *     print_counts()), printed here with the host's printf.
 */
#include <stdio.h>
#include <string.h>

#include "speed.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static struct speed s;
static uint32_t now = 4294000000u;      /* wraps 0.97 s in */
static uint32_t cycle = 4294900000u;    /* wraps too */
static uint8_t byte_4f0;
static int seconds_seen, done_seen;
static uint32_t last_kips, last_halt;

/* ms milliseconds, a step a millisecond, per_ms instructions in each (0:
   halted) */
static void run(int ms, int per_ms)
{
	for (int i = 0; i < ms; i++) {
		now += 1000;
		cycle += per_ms;
		int ev = speed_step(&s, now, cycle, byte_4f0);
		if (ev & SPEED_SECOND) {
			seconds_seen++;
			last_kips = s.kips;
			last_halt = s.halt_pct;
		}
		if (ev & SPEED_MARKS_DONE)
			done_seen++;
	}
}

/* the Linux route's lines, its own formats */
static void linux_mark_line(const struct speed_mark *k, char *buf, size_t size)
{
	const char *name = speed_mark_name(k->mark);
	double sec = k->us / 1e6;
	if (k->mark == 0xa4) {
		snprintf(buf, size, "tiny386: %02x %-34s  32768000 bytes        in %7.2f s:"
			 " %5.0f thousand a second", k->mark, name, sec,
			 sec > 0 ? 32768.0 / sec : 0.0);
		return;
	}
	snprintf(buf, size, "tiny386: %02x %-34s %9llu instructions in %7.2f s:"
		 " %5.0f thousand a second", k->mark, name ? name : "",
		 (unsigned long long) k->insns, sec, sec > 0 ? k->insns / 1e3 / sec : 0.0);
}

int main(void)
{
	char mine[200], theirs[200];

	speed_init(&s, now, cycle);
	printf("each second:\n");
	run(3000, 400);
	CHECK(seconds_seen == 3 && last_kips == 400 && last_halt == 0,
	      "3 s at 400 a ms: %d seconds, %u kips, %u %% halted", seconds_seen,
	      last_kips, last_halt);
	printf("  400 instructions a millisecond: %u thousand a second, %u %% halted\n",
	       last_kips, last_halt);
	run(750, 400);
	run(250, 0);
	CHECK(seconds_seen == 4 && last_kips == 400 && last_halt == 25,
	      "a quarter halted: %u kips, %u %%", last_kips, last_halt);
	printf("  a quarter of the second halted: %u thousand a second of running, "
	       "%u %% halted\n", last_kips, last_halt);

	printf("BENCH.COM's marks (milliseconds of running each, 400 a ms, with "
	       "halted time between):\n");
	byte_4f0 = 0x55;                /* not a mark */
	run(100, 400);
	static const struct { uint8_t mark; int ms; } piece[] = {
		{0xA1, 5243}, {0xA2, 1500}, {0xA3, 2000}, {0xA4, 1234},
		{0xA5, 900}, {0xA6, 3000}, {0xA7, 2500}, {0xA8, 1700}, {0xA9, 4100},
	};
	for (unsigned i = 0; i < 9; i++) {
		byte_4f0 = piece[i].mark;
		run(piece[i].ms, 400);
		run(37, 0);             /* halted: not the piece's */
	}
	byte_4f0 = 0xAF;
	run(10, 400);
	CHECK(done_seen == 1 && s.nmarks == 9, "done %d times, %d marks", done_seen,
	      s.nmarks);
	for (int i = 0; i < s.nmarks; i++) {
		const struct speed_mark *k = &s.marks[i];
		CHECK(k->mark == piece[i].mark && k->insns == 400ull * piece[i].ms &&
		      k->us == 1000ull * piece[i].ms, "mark %02x: %llu insns in %llu us",
		      k->mark, (unsigned long long) k->insns, (unsigned long long) k->us);
		speed_mark_line(&s, i, mine, sizeof(mine));
		linux_mark_line(k, theirs, sizeof(theirs));
		CHECK(!strcmp(mine, theirs), "line differs from the Linux route's:\n  "
		      "mine:   %s\n  theirs: %s", mine, theirs);
		printf("  %s\n", mine);
	}
	run(2000, 400);
	CHECK(done_seen == 1, "0xAF counted again while it stays");

	/* the total, against print_counts()'s format */
	speed_total_line(s.insns, s.run_us, "", mine, sizeof(mine));
	double run_s = s.run_us / 1e6;
	snprintf(theirs, sizeof(theirs), "tiny386: %llu instructions in %.1f s of running:"
		 " %.3f million a second", (unsigned long long) s.insns, run_s,
		 run_s > 0 ? s.insns / 1e6 / run_s : 0.0);
	CHECK(!strcmp(mine, theirs), "total differs:\n  mine:   %s\n  theirs: %s", mine,
	      theirs);
	printf("  %s\n", mine);
	speed_total_line(358000, 1000000, " since the PC started", mine, sizeof(mine));
	CHECK(!strcmp(mine, "tiny386: 358000 instructions in 1.0 s of running since the "
		      "PC started: 0.358 million a second"), "total with words: %s", mine);

	/* BENCH again: a new table */
	byte_4f0 = 0xA1;
	run(1000, 400);
	CHECK(s.nmarks == 0 && !s.marks_done, "second run: %d marks kept", s.nmarks);
	byte_4f0 = 0xA2;
	run(10, 400);
	byte_4f0 = 0xAF;
	run(10, 400);
	CHECK(done_seen == 2 && s.nmarks == 2 && s.marks[0].mark == 0xA1 &&
	      s.marks[0].insns == 400000, "second run: done %d, %d marks", done_seen,
	      s.nmarks);
	printf("  BENCH run again: a new table (%d pieces)\n", s.nmarks);

	/* review round 2 (emulator correctness 2): the byte is watched all
	   the time, and other programs may write it */
	printf("marks from a program that is not BENCH:\n");
	{
		/* as the reviewer's rev2_speed.c: a fresh count, the byte 0, then
		   a lone 0xAF; and a stray 0xA5 followed by 0xAF */
		struct speed s2;
		uint32_t t2 = 1000000, c2 = 0;
		int ev2 = 0;
		speed_init(&s2, t2, c2);
		for (int i = 0; i < 100; i++)
			ev2 |= speed_step(&s2, t2 += 1000, c2 += 400, 0);
		ev2 |= speed_step(&s2, t2 += 1000, c2 += 400, 0xAF);
		CHECK(!(ev2 & SPEED_MARKS_DONE) && s2.nmarks == 0,
		      "a lone 0xAF counted as the end of BENCH (%d marks)", s2.nmarks);
		speed_init(&s2, t2, c2);
		ev2 = speed_step(&s2, t2 += 1000, c2 += 400, 0xA5);
		ev2 |= speed_step(&s2, t2 += 1000, c2 += 400, 0xAF);
		CHECK(!(ev2 & SPEED_MARKS_DONE) && s2.nmarks == 0,
		      "0xA5 then 0xAF counted as BENCH (%d marks)", s2.nmarks);
		printf("  a lone 0xAF, and 0xA5 then 0xAF: no table, nothing shown\n");
	}
	/* BENCH stopped part way (A1 A2 A3), then run again from the start:
	   the table is the second run's only */
	static const uint8_t cut[] = { 0xA1, 0xA2, 0xA3, 0xA1, 0xA2, 0xA3, 0xAF };
	for (unsigned i = 0; i < sizeof(cut); i++) {
		byte_4f0 = cut[i];
		run(100 + 10 * i, 400);
	}
	CHECK(done_seen == 3 && s.nmarks == 3 && s.marks[0].mark == 0xA1 &&
	      s.marks[0].insns == 400ull * 130 && s.marks[2].mark == 0xA3 &&
	      s.marks[2].insns == 400ull * 150,
	      "a run cut short, then a whole one: done %d, %d marks, first %02x %llu",
	      done_seen, s.nmarks, s.marks[0].mark, (unsigned long long) s.marks[0].insns);
	printf("  BENCH stopped part way, then run again: the second run's %d pieces\n", s.nmarks);

	printf("speed: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
