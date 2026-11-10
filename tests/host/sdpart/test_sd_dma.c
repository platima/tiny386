/*
 * v5.3: the card's DMA straight into and out of the guest's disk buffers,
 * a run in one card command (esp/main/sd_part.c's card_rw(), storage.c's
 * storage_card_dma_ok()), on the card image run_sdpart.sh lays out as
 * s31/newcard makes one, with ESP-IDF's own FatFs and SD glue, storage.c's
 * partition code and ide.c as the boards build it (BUILD_ESP32, the
 * gathering and the read ahead on).  host_glue.c's host_dma_check() stands
 * in for the SD driver's buffer check: a buffer is taken if both its ends
 * are on 64-byte boundaries and it is in none of the ranges a test refuses.
 *
 *   test_sd_dma IMAGE SECTORS P3 P3N SEED LOG COPY,MORE DMA [short]
 *
 * COPY is how many sectors the sd_part.c built in moves a card command
 * through its own internal buffer, and MORE how many once it has said that
 * the driver refuses a bigger buffer (this one: 4,16, v5.2's 16 sectors
 * taken then; v5.2's: 16,16); DMA 1 if it gives a buffer the check takes
 * to the card as one command (v5.2's 0).  The same SEED makes the same
 * operations whichever sd_part.c is built in, and LOG gets what the guest
 * saw of each, so that run_sdpart.sh can hold this sd_part.c's LOG and
 * card image against v5.2's.  "short": once the partition is open, no more
 * internal RAM, so that sd_part.c cannot take v5.2's 16 sectors when the
 * driver refuses a big buffer, and goes on 4 sectors a command (4,4).
 *
 * 1. sd_part_read() and sd_part_write(): runs of 1 to 130 sectors to and
 *    from a buffer the check takes, the same 4 bytes on (not aligned), and
 *    an aligned one the check refuses, at the guest's LBA 0, across p3's
 *    start, inside, across p3's end and across the disk's end; a card that
 *    refuses a command once now and then (tried again) or every time.
 * 2. IDE commands through the ports: READ and WRITE SECTORS, READ and WRITE
 *    MULTIPLE (blocks of 4), 1 to 256 sectors, so that the read ahead (64
 *    sectors) and the gathering (128) fill, are used up and fill again, and
 *    write commands left part way (written at the next command); then the
 *    same with the check refusing ide.c's buffer.
 * Every read is held against the card image (or the record made for LBA 0,
 * or zeros), every write read back from it, and every buffer the card's
 * reads and writes were handed against the check: not one went to the SD
 * driver for it to copy.  Exit status 0 when every check passes.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "storage.h"
#include "disk_image.h"
#include "sd_part.h"
#include "host_glue.h"

extern void *rawsd;             /* storage.c's: the card */

static int checks, fails;
/* quiet when it passes: there are thousands */
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		if (fails++ < 40) { \
			printf("  FAIL line %d: ", __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

uint32_t get_uticks(void)
{
	static uint32_t t;
	return t += 10;
}

/* ==== ide.c, as the boards build it ==== */

#include "pci.h"
void *pcmalloc(long size) { return calloc(1, size); }
void tiny386_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("  tiny386_fatal: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(3);
}
PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
			       uint16_t vendor_id, uint16_t device_id,
			       uint8_t revision_id, uint16_t class_id)
{
	(void) b; (void) name; (void) devfn; (void) vendor_id; (void) device_id;
	(void) revision_id; (void) class_id;
	return NULL;
}
void pci_device_set_config8(PCIDevice *d, uint8_t addr, uint8_t val)
{
	(void) d; (void) addr; (void) val;
}

#define BUILD_ESP32
#define IDE_ENABLE_WRITE_GATHER
#define IDE_ENABLE_READ_AHEAD
#include IDE_C

/* ==== the card, the guest's disk and what the guest should see ==== */

static sdmmc_card_t s_card;
static int s_fd;
static uint64_t s_p3, s_pe, s_ds;       /* p3's first sector, one past its last, the disk's size */
static uint8_t s_mbr[512];              /* the record the guest reads at LBA 0 */
static unsigned s_copy, s_more;         /* COPY,MORE */
static int s_dma;                       /* DMA */
static FILE *s_log;
static uint64_t s_rng;

static uint32_t rnd(void)
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 7;
	s_rng ^= s_rng << 17;
	return (uint32_t) (s_rng >> 16);
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

/* n bytes that only this key makes */
static void fill(uint8_t *p, size_t n, uint64_t key)
{
	uint64_t x = key * 0x9E3779B97F4A7C15ull + 1;
	for (size_t i = 0; i < n; i++) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		p[i] = (uint8_t) (x >> 24);
	}
}

static void card_get(uint64_t s, uint8_t *out, unsigned n)
{
	if (pread(s_fd, out, (size_t) n * 512, (off_t) s * 512) != (ssize_t) n * 512)
		memset(out, 0xEE, (size_t) n * 512);
}

/* what the guest reads at LBA s */
static void expect_sector(uint64_t s, uint8_t *out)
{
	if (s == 0)
		memcpy(out, s_mbr, 512);
	else if (s >= s_p3 && s < s_pe)
		card_get(s, out, 1);
	else
		memset(out, 0, 512);
}

/* the sectors of [lba, lba + n) that the guest reads wrong; 0 if none */
static unsigned wrong_sectors(uint64_t lba, unsigned n, const uint8_t *b)
{
	uint8_t want[512];
	unsigned bad = 0;
	for (unsigned k = 0; k < n; k++) {
		expect_sector(lba + k, want);
		if (memcmp(b + (size_t) k * 512, want, 512))
			bad++;
	}
	return bad;
}

/* the lines sd_part.c says, counted by what they say */
static unsigned s_said_dma, s_said_copy;
static void said(bool warn, const char *line)
{
	if (strstr(line, "DMA reads and writes the guest's disk buffers"))
		s_said_dma++;
	else if (strstr(line, "cannot take a buffer of"))
		s_said_copy++;
	else
		return;
	printf("said (%s): %s\n", warn ? "yellow" : "white", line);
}

static bool mount_first(void)
{
	BYTE pdrv = FF_DRV_NOT_USED;
	if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED)
		return false;
	ff_diskio_register_sdmmc(pdrv, &s_card);
	ff_sdmmc_set_disk_status_check(pdrv, false);
	char drv[3] = { (char) ('0' + pdrv), ':', 0 };
	esp_vfs_fat_conf_t conf = { .base_path = "/sdcard", .fat_drive = drv, .max_files = 5 };
	FATFS *fs = NULL;
	if (esp_vfs_fat_register(&conf, &fs) != ESP_OK)
		return false;
	if (f_mount(fs, drv, 1) != FR_OK)
		return false;
	rawsd = &s_card;
	return true;
}

/* ==== 1. sd_part_read() and sd_part_write() ==== */

#define P1_MAX 130

static void part1(struct sd_part *sp, int ops)
{
	static const unsigned lens[] = {
		1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 129, 130,
	};
	const unsigned nlens = sizeof(lens) / sizeof(lens[0]);
	unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED;
	uint8_t *a = heap_caps_malloc(P1_MAX * 512 + 64, caps);
	uint8_t *c = heap_caps_malloc(P1_MAX * 512, caps);
	uint8_t *before = malloc(P1_MAX * 512), *after = malloc(P1_MAX * 512);
	host_card.dma_refused[0].start = c;
	host_card.dma_refused[0].size = P1_MAX * 512;
	host_card.ndma_refused = 1;
	uint8_t *bufs[3] = { a, a + 4, c };
	static const char *const kinds[3] = { "taken", "4 bytes on", "refused" };
	CHECK(host_dma_check(0, a, 512) && !host_dma_check(0, a + 4, 512) &&
	      !host_dma_check(0, c, 512), "the check takes a, refuses a + 4 and c");
	unsigned n_ops[3][2] = { { 0 } }, n_cmds[3][2] = { { 0 } }, n_secs[3][2] = { { 0 } };
	unsigned cards = 0, refused = 0, retries = 0, failed = 0;
	unsigned copied0 = host_card.dma_copied;

	for (int i = 0; i < ops; i++) {
		unsigned kind = rnd() % 3, n = lens[rnd() % nlens], where = rnd() % 6;
		uint64_t lba;
		switch (where) {
		case 0:
			lba = rnd() % 8;
			break;
		case 1:
			lba = s_p3 - n - 2 + rnd() % (n + 5);
			break;
		case 4:
			lba = s_pe - n - 2 + rnd() % (n + 5);
			break;
		case 5:
			lba = s_ds - n - 2 + rnd() % (n + 5);
			break;
		default:
			lba = s_p3 + rnd() % (unsigned) (s_pe - s_p3 - n);
			break;
		}
		bool write = rnd() % 100 < 45;
		/* now and then a card that refuses the run's first command once,
		   or refuses every one */
		int fail = i % 23 == 7 ? 1 : i % 61 == 13 ? 2 : 0;
		uint8_t *b = bufs[kind];
		uint64_t lo = lba > s_p3 ? lba : s_p3;
		uint64_t hi = lba + n < s_pe ? lba + n : s_pe;
		unsigned k = hi > lo ? (unsigned) (hi - lo) : 0;    /* the card's part */
		bool inside = lba >= s_p3 && lba + n <= s_pe;
		bool past = lba > s_ds || n > s_ds - lba;
		if (write && inside)
			card_get(lba, before, n);
		unsigned r0 = host_card.reads, w0 = host_card.writes, c0 = host_card.dma_copied;
		if (fail) {
			host_card.fail_n = fail == 1 ? 1 : 1000000;
			if (write)
				host_card.fail_write_at = host_card.writes + 1;
			else
				host_card.fail_read_at = host_card.reads + 1;
		}
		int ret;
		uint64_t h;
		if (write) {
			fill(b, (size_t) n * 512, s_rng ^ (uint64_t) i);
			ret = sd_part_write(sp, lba, b, n);
			h = fnv(b, (size_t) n * 512);
		} else {
			memset(b, 0xA5, (size_t) n * 512);
			ret = sd_part_read(sp, lba, b, n);
			h = ret ? 0 : fnv(b, (size_t) n * 512);
		}
		host_card.fail_read_at = host_card.fail_write_at = 0;
		host_card.fail_n = 1;
		unsigned cmds = write ? host_card.writes - w0 : host_card.reads - r0;
		fprintf(s_log, "1 %d %c %llu %u %u %d %016llx\n", i, write ? 'W' : 'R',
			(unsigned long long) lba, n, kind, ret, (unsigned long long) h);
		CHECK(host_card.dma_copied == c0, "op %d: the driver was handed a buffer to copy", i);

		if (!write) {
			if (past)
				CHECK(ret == SD_PART_REFUSED, "op %d: a read past the disk's end: %d", i, ret);
			else if (fail == 2 && k)
				CHECK(ret == SD_PART_CARD_ERROR, "op %d: a card refusing for good: %d", i,
				      ret);
			else
				CHECK(ret == 0 && !wrong_sectors(lba, n, b), "op %d: read %u at %llu "
				      "(%s): %d, %u sectors wrong", i, n, (unsigned long long) lba,
				      kinds[kind], ret, ret ? n : wrong_sectors(lba, n, b));
		} else if (!inside) {
			CHECK(ret == SD_PART_REFUSED && cmds == 0, "op %d: a write of %u at %llu, not "
			      "all in p3: %d, %u card commands", i, n, (unsigned long long) lba, ret,
			      cmds);
		} else {
			card_get(lba, after, n);
			if (fail == 2)
				CHECK(ret == SD_PART_CARD_ERROR && !memcmp(after, before, (size_t) n * 512),
				      "op %d: a write the card refuses for good: %d, the card %s", i, ret,
				      memcmp(after, before, (size_t) n * 512) ? "changed" : "as it was");
			else
				CHECK(ret == 0 && !memcmp(after, b, (size_t) n * 512), "op %d: write %u "
				      "at %llu (%s): %d, %s on the card", i, n, (unsigned long long) lba,
				      kinds[kind], ret, memcmp(after, b, (size_t) n * 512) ? "not" : "all");
		}
		if (ret == SD_PART_REFUSED)
			refused++;
		if (fail == 1 && k && ret == 0)
			retries++;
		if (fail == 2 && k)
			failed++;
		/* the card's part: one command for a buffer the check takes, else
		   COPY sectors a command through the internal buffer */
		if (!fail && k && ret == 0) {
			bool taken = kind == 0;
			unsigned chunk = s_said_copy ? s_more : s_copy;
			unsigned want = s_dma && taken ? 1 : (k + chunk - 1) / chunk;
			CHECK(cmds == want, "op %d: %u sectors of the card (%s) in %u commands, not %u",
			      i, k, kinds[kind], cmds, want);
			n_ops[kind][write]++;
			n_cmds[kind][write] += cmds;
			n_secs[kind][write] += k;
			cards++;
		}
	}
	for (unsigned kd = 0; kd < 3; kd++)
		printf("%-10s reads: %4u runs, %6u sectors, %5u card commands; writes: %4u "
		       "runs, %6u sectors, %5u commands\n", kinds[kd], n_ops[kd][0], n_secs[kd][0],
		       n_cmds[kd][0], n_ops[kd][1], n_secs[kd][1], n_cmds[kd][1]);
	printf("%d operations: %u on the card, %u refused, %u got over a refusing card, "
	       "%u failed on one\n", ops, cards, refused, retries, failed);
	CHECK(host_card.dma_copied == copied0, "no buffer handed to the driver to copy");
	host_card.ndma_refused = 0;
	heap_caps_free(a);
	heap_caps_free(c);
	free(before);
	free(after);
}

/* ==== 2. IDE commands through the ports ==== */

static int s_irqs;
static void ide_irq(void *pic, int irq, int level)
{
	(void) pic; (void) irq;
	if (level)
		s_irqs++;
}

/* one command in LBA mode: count sectors (256 for 0) at lba, the data from
   or to buf, at most stop sectors of it; *done the sectors moved, *sth the
   statuses seen; the status at the end */
static uint32_t ata(IDEIFState *ifs, uint8_t cmd, uint32_t lba, unsigned count, uint8_t *buf,
		    bool write, unsigned stop, unsigned *done, uint64_t *sth)
{
	ide_ioport_write(ifs, 6, 0xE0 | ((lba >> 24) & 0x0F));
	ide_ioport_write(ifs, 2, count & 0xFF);
	ide_ioport_write(ifs, 3, lba & 0xFF);
	ide_ioport_write(ifs, 4, (lba >> 8) & 0xFF);
	ide_ioport_write(ifs, 5, (lba >> 16) & 0xFF);
	ide_ioport_write(ifs, 7, cmd);
	unsigned total = count ? count : 256;
	uint32_t st;
	*done = 0;
	for (;;) {
		st = ide_status_read(ifs);
		*sth = *sth * 31 + st;
		if ((st & 0x01) || !(st & 0x08) || *done >= total || *done >= stop)
			break;
		uint8_t *p = buf + (size_t) *done * 512;
		for (int w = 0; w < 256; w++) {
			if (write) {
				ide_data_writew(ifs, p[2 * w] | p[2 * w + 1] << 8);
			} else {
				uint32_t v = ide_data_readw(ifs);
				p[2 * w] = (uint8_t) v;
				p[2 * w + 1] = (uint8_t) (v >> 8);
			}
		}
		(*done)++;
	}
	return st;
}

static void part2(IDEIFState *ifs, IDEState *d, int ops, const char *what)
{
	static const unsigned counts[] = {
		1, 1, 2, 3, 4, 4, 5, 8, 16, 31, 63, 64, 65, 66, 100, 127, 128, 129, 130, 200, 255, 0,
	};
	const unsigned ncounts = sizeof(counts) / sizeof(counts[0]);
	uint8_t *buf = malloc(256 * 512), *before = malloc(256 * 512), *after = malloc(256 * 512);
	unsigned n_cmd[2] = { 0 }, n_sec[2] = { 0 }, n_err[2] = { 0 }, left = 0;
	unsigned r0 = host_card.reads, w0 = host_card.writes, c0 = host_card.dma_copied;

	for (int i = 0; i < ops; i++) {
		unsigned count = counts[rnd() % ncounts], total = count ? count : 256;
		unsigned where = rnd() % 8;
		uint64_t lba;
		if (where == 0)
			lba = s_p3 - total / 2;
		else if (where == 1)
			lba = s_pe - total / 2;
		else if (where == 2)
			lba = s_ds - total / 2;
		else
			lba = s_p3 + rnd() % (unsigned) (s_pe - s_p3 - total);
		bool write = rnd() % 100 < 45, multiple = rnd() % 2;
		uint8_t cmd = write ? (multiple ? 0xC5 : 0x30) : (multiple ? 0xC4 : 0x20);
		bool inside = lba >= s_p3 && lba + total <= s_pe;
		/* now and then a write left part way: the guest's next command
		   writes what was gathered */
		unsigned stop = 256;
		if (write && !multiple && inside && total > 2 && rnd() % 6 == 0)
			stop = 1 + rnd() % (total - 1);
		if (write) {
			card_get(lba, before, total);
			fill(buf, (size_t) total * 512, s_rng ^ (uint64_t) i << 20);
		} else {
			memset(buf, 0xA5, (size_t) total * 512);
		}
		unsigned done;
		uint64_t sth = 0;
		uint32_t st = ata(ifs, cmd, (uint32_t) lba, count, buf, write, stop, &done, &sth);
		uint32_t err = ide_ioport_read(ifs, 1);
		fprintf(s_log, "2 %s %d %02x %llu %u %u %u %02x %02x %016llx %016llx\n", what, i, cmd,
			(unsigned long long) lba, total, stop, done, st, err,
			(unsigned long long) sth, (unsigned long long) fnv(buf, (size_t) done * 512));
		n_cmd[write]++;
		n_sec[write] += done;
		if (st & 0x01)
			n_err[write]++;
		if (!write) {
			CHECK(!wrong_sectors(lba, done, buf), "%s %d: READ %02x of %u at %llu: %u of %u "
			      "sectors moved wrong", what, i, cmd, total, (unsigned long long) lba,
			      wrong_sectors(lba, done, buf), done);
			bool past = lba + total > s_ds;
			CHECK(past ? (st & 0x01) && done < total : !(st & 0x01) && done == total,
			      "%s %d: READ %02x of %u at %llu: status %02x after %u sectors", what, i,
			      cmd, total, (unsigned long long) lba, st, done);
			continue;
		}
		if (stop < total) {
			/* the next command: one sector read at lba, which has the
			   gathered sectors written first */
			unsigned d2;
			uint64_t s2 = 0;
			uint8_t one[512];
			ata(ifs, 0x20, (uint32_t) lba, 1, one, false, 1, &d2, &s2);
			card_get(lba, after, total);
			CHECK(done == stop && !memcmp(after, buf, (size_t) done * 512) &&
			      !memcmp(after + (size_t) done * 512, before + (size_t) done * 512,
				      (size_t) (total - done) * 512) && !memcmp(one, buf, 512),
			      "%s %d: WRITE %02x of %u at %llu left after %u: those on the card "
			      "at the next command, the rest as it was", what, i, cmd, total,
			      (unsigned long long) lba, done);
			left++;
			continue;
		}
		card_get(lba, after, total);
		if (inside) {
			CHECK(!(st & 0x01) && done == total && !memcmp(after, buf, (size_t) total * 512),
			      "%s %d: WRITE %02x of %u at %llu: status %02x, %u moved, %s on the card",
			      what, i, cmd, total, (unsigned long long) lba, st, done,
			      memcmp(after, buf, (size_t) total * 512) ? "not all" : "all");
		} else {
			/* across p3's ends: an error, and each sector of p3 either
			   written or as it was */
			unsigned odd = 0;
			for (unsigned s = 0; s < total; s++) {
				uint64_t x = lba + s;
				if (x < s_p3 || x >= s_pe)
					continue;
				const uint8_t *o = after + (size_t) s * 512;
				if (memcmp(o, buf + (size_t) s * 512, 512) && memcmp(o, before + (size_t) s * 512, 512))
					odd++;
			}
			CHECK((st & 0x01) && !odd, "%s %d: WRITE %02x of %u at %llu, across p3's end: "
			      "status %02x, %u sectors neither written nor as they were", what, i, cmd,
			      total, (unsigned long long) lba, st, odd);
		}
	}
	unsigned cr = host_card.reads - r0, cw = host_card.writes - w0;
	printf("%s: %d commands: %u reads (%u sectors, %u errors), %u writes (%u sectors, "
	       "%u errors, %u left part way); %u card reads and %u card writes\n", what, ops,
	       n_cmd[0], n_sec[0], n_err[0], n_cmd[1], n_sec[1], n_err[1], left, cr, cw);
	CHECK(host_card.dma_copied == c0, "%s: no buffer handed to the driver to copy", what);
	(void) d;
	free(buf);
	free(before);
	free(after);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 9) {
		fprintf(stderr, "usage: test_sd_dma IMAGE SECTORS P3 P3N SEED LOG COPY,MORE DMA\n");
		return 2;
	}
	s_fd = open(argv[1], O_RDWR);
	if (s_fd < 0) {
		perror(argv[1]);
		return 2;
	}
	s_card.csd.capacity = atoi(argv[2]);
	s_card.csd.sector_size = 512;
	s_card.fd = s_fd;
	s_card.host.check_buffer_alignment = host_dma_check;
	s_p3 = (uint64_t) atoll(argv[3]);
	s_pe = s_p3 + (uint64_t) atoll(argv[4]);
	s_rng = strtoull(argv[5], NULL, 0) | 1;
	s_log = fopen(argv[6], "w");
	if (sscanf(argv[7], "%u,%u", &s_copy, &s_more) != 2 || !s_copy || !s_more || !s_log)
		return 2;
	s_dma = atoi(argv[8]);
	bool short_ram = argc > 9 && !strcmp(argv[9], "short");
	disk_image_say = said;

	char why[200];
	CHECK(mount_first(), "p1 mounted at /sdcard");
	storage_sd_mount_others();
	CHECK(storage_part_release(3, why, sizeof(why)), "p3 let go");
	host_card.nallowed = 1;
	host_card.allowed[0][0] = s_p3;
	host_card.allowed[0][1] = s_pe - s_p3;
	host_card.outside = 0;
	IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
	ide_attach(ifs, 0, "sd:3");
	ide_ioport_write(ifs, 6, 0xA0);
	IDEState *d = ifs->drives[0];
	struct sd_part *sp = d->bs->opaque;
	s_ds = sd_part_sectors(sp);
	uint8_t card0[512];
	card_get(0, card0, 1);
	sd_part_make_mbr(sd_part_get_plan(sp), card0, s_mbr);
	printf("sd:3: p3 at %llu, %llu sectors; the guest's disk %llu sectors; COPY %u, MORE "
	       "%u, DMA %d\n", (unsigned long long) s_p3, (unsigned long long) (s_pe - s_p3),
	       (unsigned long long) s_ds, s_copy, s_more, s_dma);
	host_heap_no_internal = short_ram;

	printf("== 1. sd_part_read() and sd_part_write()\n");
	part1(sp, 900);

	printf("== 2. IDE commands through the ports\n");
	/* SET MULTIPLE MODE: blocks of 4 for READ and WRITE MULTIPLE */
	ide_ioport_write(ifs, 2, 4);
	ide_ioport_write(ifs, 7, 0xC6);
	CHECK(!(ide_status_read(ifs) & 0x01) && d->mult_sectors == 4, "multiple mode, 4 sectors");
	part2(ifs, d, 400, "ide");
	CHECK(d->wbuf && d->rbuf == d->wbuf && host_dma_check(0, d->wbuf, 128 * 512),
	      "ide.c's buffer: taken, %u bytes past a 64-byte boundary",
	      (unsigned) ((uintptr_t) d->wbuf % 64));
	/* the same with the check refusing ide.c's buffer: through sd_part.c's own */
	host_card.dma_refused[0].start = d->wbuf;
	host_card.dma_refused[0].size = 128 * 512;
	host_card.ndma_refused = 1;
	part2(ifs, d, 200, "refused");
	host_card.ndma_refused = 0;
	ide_flush();

	printf("== what the guest saw, and the card\n");
	CHECK(host_card.outside == 0, "nothing written on the card outside p3 (%u sectors, the "
	      "first %zu)", host_card.outside, host_card.first_outside);
	CHECK(host_card.dma_copied == 0, "every buffer the card's reads and writes were handed, "
	      "the check took (%u taken, %u it would copy)", host_card.dma_direct,
	      host_card.dma_copied);
	if (s_dma)
		CHECK(s_said_dma == 1 && s_said_copy == 1, "sd_part.c said each way once (DMA %u, "
		      "copy %u)", s_said_dma, s_said_copy);
	else
		CHECK(s_said_dma == 0 && s_said_copy == 0, "this sd_part.c says neither");
	fclose(s_log);
	close(s_fd);
	printf("test_sd_dma: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
