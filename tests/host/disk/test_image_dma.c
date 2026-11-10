/*
 * v5.3: the card's DMA straight into and out of the guest's disk buffers
 * for a disk image on the card (esp/main/disk_image.c's rw_once(), as
 * sd_part.c's card_rw() since d582cf7), on a FAT volume that run_disk.sh
 * lays out, with ESP-IDF's own FatFs and SD glue and ide.c as the boards
 * build it (BUILD_ESP32, the gathering and the read ahead on).
 * host_glue.c's host_dma_check() stands in for the SD driver's buffer
 * check (storage_card_dma_ok() below asks it, as storage.c asks the
 * driver's): a buffer is taken if both its ends are on 64-byte boundaries
 * and it is in none of the ranges a test refuses.
 *
 *   test_image_dma VOLUME SECTORS SEED LOG COPY,MORE DMA [short]
 *
 * COPY is how many sectors the disk_image.c built in moves an f_read() or
 * f_write() through its own internal buffer, and MORE how many once it has
 * said that the driver refuses a bigger buffer (this one: 4,16; v5.2's,
 * 3e386ff: 16,16); DMA 1 if it hands a buffer the check takes to FatFs in
 * one call (v5.2's 0).  The same SEED makes the same operations whichever
 * disk_image.c is built in, and LOG gets what the guest saw of each, and
 * the image's hash at the end, so that run_disk.sh can hold this one's LOG
 * against v5.2's.  "short": once the image is open, no more internal RAM,
 * so that the 16 sectors cannot be taken (4,4).
 *
 * 1. disk_image_read() and disk_image_write(): runs of 1 to 130 sectors to
 *    and from a buffer the check takes, the same 4 bytes on, and an aligned
 *    one it refuses, at the image's start, inside it, across its end and
 *    past it; a card that refuses a command once now and then (the image
 *    opened again, tried again) or every time.
 * 2. IDE commands through the ports: READ and WRITE SECTORS, READ and WRITE
 *    MULTIPLE, 1 to 256 sectors, write commands left part way; then the
 *    same with the check refusing ide.c's buffer.
 * Every read is held against the test's copy of the image, every write
 * read back, the image read whole at the end, and every buffer the card's
 * reads and writes were handed is held against the check: not one went to
 * the SD driver for it to copy.  Exit status 0 when every check passes.
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
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "disk_image.h"
#include "host_glue.h"

static int checks, fails;
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

#define IMG "0:/IMAGE.IMG"

/* ==== what disk_image.c, sd_part.c and ide.c want from the firmware ==== */

uint32_t get_uticks(void)
{
	static uint32_t t;
	return t += 10;
}

bool storage_fatfs_path(const char *path, char *out, size_t size)
{
	if (strncmp(path, "0:/", 3))
		return false;
	snprintf(out, size, "%s", path);
	return true;
}

int storage_card_pdrv(void)
{
	return -1;
}

uint64_t storage_card_sectors(void)
{
	return 0;
}

/* as storage.c asks the SD driver's own check */
bool storage_card_dma_ok(const void *buf, size_t size)
{
	return host_dma_check(0, buf, size);
}

bool storage_part_mounted(int part)
{
	(void) part;
	return false;
}

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

/* ==== FatFs's calls from disk_image.c, counted (-Wl,--wrap) ==== */

static unsigned s_freads, s_fwrites;
FRESULT __real_f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT __real_f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT __wrap_f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
	s_freads++;
	return __real_f_read(fp, buff, btr, br);
}
FRESULT __wrap_f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
	s_fwrites++;
	return __real_f_write(fp, buff, btw, bw);
}

/* ==== the image, as the guest should see it ==== */

static uint8_t *s_want;                 /* the test's copy of the image */
static uint64_t s_size;                 /* bytes */
static unsigned s_copy, s_more;
static int s_dma;
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

/* the image read whole through FatFs, with a file of its own: the test's
   own reads, left out of the card's buffer counts */
static bool read_whole(uint8_t *out)
{
	static FIL f;
	memset(&f, 0, sizeof(f));
	unsigned direct = host_card.dma_direct, copied = host_card.dma_copied;
	unsigned reads = host_card.reads;
	bool ok = f_open(&f, IMG, FA_READ) == FR_OK;
	UINT got = 0;
	if (ok)
		ok = __real_f_read(&f, out, (UINT) s_size, &got) == FR_OK && got == s_size;
	f_close(&f);
	host_card.dma_direct = direct;
	host_card.dma_copied = copied;
	host_card.reads = reads;
	return ok;
}

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

/* ==== 1. disk_image_read() and disk_image_write() ==== */

#define P1_MAX 130

static void part1(int ops)
{
	static const unsigned lens[] = {
		1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 129, 130,
	};
	const unsigned nlens = sizeof(lens) / sizeof(lens[0]);
	struct disk_image *d = NULL;
	uint64_t size = 0;
	CHECK(disk_image_open(IMG, true, &d, &size) == DISK_IMAGE_OPENED && size == s_size,
	      "the image opened, %llu bytes", (unsigned long long) size);
	unsigned caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED;
	uint8_t *a = heap_caps_malloc(P1_MAX * 512 + 64, caps);
	uint8_t *c = heap_caps_malloc(P1_MAX * 512, caps);
	uint8_t *back = malloc(P1_MAX * 512);
	host_card.dma_refused[0].start = c;
	host_card.dma_refused[0].size = P1_MAX * 512;
	host_card.ndma_refused = 1;
	uint8_t *bufs[3] = { a, a + 4, c };
	static const char *const kinds[3] = { "taken", "4 bytes on", "refused" };
	unsigned n_ops[3][2] = { { 0 } }, n_calls[3][2] = { { 0 } };
	unsigned copied0 = host_card.dma_copied, retries = 0, failed = 0, refused = 0;
	uint64_t ns = s_size / 512;

	for (int i = 0; i < ops; i++) {
		unsigned kind = rnd() % 3, n = lens[rnd() % nlens], where = rnd() % 5;
		uint64_t s;
		switch (where) {
		case 0: s = rnd() % 8; break;
		case 1: s = ns - n - 2 + rnd() % (n + 5); break;     /* across the end */
		case 2: s = ns + rnd() % 40; break;                  /* past it */
		default: s = 8 + rnd() % (unsigned) (ns - n - 16); break;
		}
		bool write = rnd() % 100 < 45;
		int fail = i % 23 == 7 ? 1 : i % 61 == 13 ? 2 : 0;
		uint8_t *b = bufs[kind];
		bool inside = s + n <= ns;
		unsigned r0 = host_card.reads, w0 = host_card.writes, c0 = host_card.dma_copied;
		unsigned fr0 = s_freads, fw0 = s_fwrites;
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
			ret = disk_image_write(d, s * 512, b, (size_t) n * 512);
			h = fnv(b, (size_t) n * 512);
		} else {
			memset(b, 0xA5, (size_t) n * 512);
			ret = disk_image_read(d, s * 512, b, (size_t) n * 512);
			h = ret ? 0 : fnv(b, (size_t) n * 512);
		}
		host_card.fail_read_at = host_card.fail_write_at = 0;
		host_card.fail_n = 1;
		unsigned calls = write ? s_fwrites - fw0 : s_freads - fr0;
		(void) r0; (void) w0;
		fprintf(s_log, "1 %d %c %llu %u %u %d %016llx\n", i, write ? 'W' : 'R',
			(unsigned long long) s, n, kind, ret, (unsigned long long) h);
		CHECK(host_card.dma_copied == c0, "op %d: the driver was handed a buffer to copy", i);
		if (!inside) {
			CHECK(ret != 0 && calls == 0, "op %d: %u at %llu, past the end: %d, %u calls", i,
			      n, (unsigned long long) s, ret, calls);
			refused++;
			continue;
		}
		if (fail == 2) {
			CHECK(ret != 0, "op %d: a card refusing every time: %d", i, ret);
			failed++;
		} else if (!write) {
			CHECK(ret == 0 && !memcmp(b, s_want + s * 512, (size_t) n * 512), "op %d: read %u "
			      "at %llu (%s): %d, %s", i, n, (unsigned long long) s, kinds[kind], ret,
			      ret ? "failed" : "wrong data");
		} else {
			CHECK(ret == 0, "op %d: write %u at %llu (%s): %d", i, n, (unsigned long long) s,
			      kinds[kind], ret);
			if (ret == 0)
				memcpy(s_want + s * 512, b, (size_t) n * 512);
		}
		if (fail == 1 && ret == 0)
			retries++;
		if (write && ret == 0) {
			CHECK(disk_image_read(d, s * 512, back, (size_t) n * 512) == 0 &&
			      !memcmp(back, s_want + s * 512, (size_t) n * 512), "op %d: the write read "
			      "back", i);
		}
		if (fail == 2 && write) {
			/* nothing of it on the card: read back with the card working */
			CHECK(disk_image_read(d, s * 512, back, (size_t) n * 512) == 0 &&
			      !memcmp(back, s_want + s * 512, (size_t) n * 512), "op %d: a write refused "
			      "for good left the image as it was", i);
		}
		/* the FatFs calls: one for a buffer the check takes, else COPY
		   sectors a call through the internal buffer */
		if (!fail && ret == 0) {
			bool taken = kind == 0;
			unsigned chunk = s_said_copy ? s_more : s_copy;
			unsigned want = s_dma && taken ? 1 : (n + chunk - 1) / chunk;
			CHECK(calls == want, "op %d: %u sectors (%s) in %u FatFs calls, not %u", i, n,
			      kinds[kind], calls, want);
			n_ops[kind][write]++;
			n_calls[kind][write] += calls;
		}
	}
	for (unsigned k = 0; k < 3; k++)
		printf("%-10s reads: %4u, %5u FatFs calls; writes: %4u, %5u calls\n", kinds[k],
		       n_ops[k][0], n_calls[k][0], n_ops[k][1], n_calls[k][1]);
	printf("%d operations: %u refused past the end, %u got over a refusing card, %u failed on "
	       "one\n", ops, refused, retries, failed);
	CHECK(host_card.dma_copied == copied0, "no buffer handed to the driver to copy");
	host_card.ndma_refused = 0;
	disk_image_close(d);
	heap_caps_free(a);
	heap_caps_free(c);
	free(back);
}

/* ==== 2. IDE commands through the ports ==== */

static void ide_irq(void *pic, int irq, int level)
{
	(void) pic; (void) irq; (void) level;
}

static uint32_t ata(IDEIFState *ifs, uint8_t cmd, uint32_t lba, unsigned count, uint8_t *buf,
		    bool write, unsigned stop, unsigned *done)
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
		if ((st & 0x01) || !(st & 0x08) || *done >= total || *done >= stop)
			break;
		uint8_t *p = buf + (size_t) *done * 512;
		if (write)
			ide_data_write_string(ifs, p, 4, 128);
		else
			ide_data_read_string(ifs, p, 4, 128);
		(*done)++;
	}
	return st;
}

static void part2(IDEIFState *ifs, int ops, const char *what)
{
	static const unsigned counts[] = {
		1, 1, 2, 3, 4, 4, 5, 8, 16, 31, 63, 64, 65, 66, 100, 127, 128, 129, 130, 200, 255, 0,
	};
	const unsigned ncounts = sizeof(counts) / sizeof(counts[0]);
	uint8_t *buf = malloc(256 * 512);
	unsigned c0 = host_card.dma_copied, left = 0, errs = 0;
	uint64_t ns = s_size / 512;
	/* a write left part way: its gathered blocks reach the image at the
	   drive's next command */
	bool pending = false;
	uint64_t p_lba = 0;
	unsigned p_done = 0;
	static uint8_t p_data[256 * 512];

	for (int i = 0; i < ops; i++) {
		unsigned count = counts[rnd() % ncounts], total = count ? count : 256;
		uint64_t lba = rnd() % 5 == 0 ? ns - total / 2 : rnd() % (unsigned) (ns - total);
		bool write = rnd() % 100 < 45, multiple = rnd() % 2;
		uint8_t cmd = write ? (multiple ? 0xC5 : 0x30) : (multiple ? 0xC4 : 0x20);
		bool inside = lba + total <= ns;
		unsigned stop = 256;
		if (write && !multiple && inside && total > 2 && rnd() % 6 == 0)
			stop = 1 + rnd() % (total - 1);
		if (write)
			fill(buf, (size_t) total * 512, s_rng ^ (uint64_t) i << 20);
		else
			memset(buf, 0xA5, (size_t) total * 512);
		unsigned done;
		uint32_t st = ata(ifs, cmd, (uint32_t) lba, count, buf, write, stop, &done);
		/* the command began with the last one's gathered blocks written */
		if (pending) {
			memcpy(s_want + p_lba * 512, p_data, (size_t) p_done * 512);
			pending = false;
		}
		uint32_t err = ide_ioport_read(ifs, 1);
		fprintf(s_log, "2 %s %d %02x %llu %u %u %u %02x %02x %016llx\n", what, i, cmd,
			(unsigned long long) lba, total, stop, done, st, err,
			(unsigned long long) fnv(buf, (size_t) done * 512));
		if (st & 0x01)
			errs++;
		if (!write) {
			CHECK(!memcmp(buf, s_want + lba * 512, (size_t) done * 512), "%s %d: READ %02x of "
			      "%u at %llu: wrong data", what, i, cmd, total, (unsigned long long) lba);
			CHECK(inside ? !(st & 0x01) && done == total : (st & 0x01) && done < total,
			      "%s %d: READ %02x of %u at %llu: status %02x after %u", what, i, cmd, total,
			      (unsigned long long) lba, st, done);
			continue;
		}
		if (!inside) {
			/* across the end: an error, and what the image now holds is
			   known again only by reading it */
			CHECK(st & 0x01, "%s %d: WRITE across the end: status %02x", what, i, st);
			ide_flush();
			CHECK(read_whole(s_want), "%s %d: the image read whole", what, i);
			continue;
		}
		if (stop < total) {
			pending = true;
			p_lba = lba;
			p_done = done;
			memcpy(p_data, buf, (size_t) done * 512);
			left++;
			continue;
		}
		CHECK(!(st & 0x01) && done == total, "%s %d: WRITE %02x of %u at %llu: status %02x, %u "
		      "moved", what, i, cmd, total, (unsigned long long) lba, st, done);
		memcpy(s_want + lba * 512, buf, (size_t) total * 512);
	}
	/* the last one's gathered blocks, by the front end's flush */
	ide_write_pending(ifs);
	if (pending)
		memcpy(s_want + p_lba * 512, p_data, (size_t) p_done * 512);
	ide_flush();
	printf("%s: %d commands, %u with an error, %u writes left part way\n", what, ops, errs, left);
	CHECK(host_card.dma_copied == c0, "%s: no buffer handed to the driver to copy", what);
	free(buf);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 7) {
		fprintf(stderr, "usage: test_image_dma VOLUME SECTORS SEED LOG COPY,MORE DMA [short]\n");
		return 2;
	}
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror(argv[1]);
		return 2;
	}
	static sdmmc_card_t card;
	card.csd.capacity = atoi(argv[2]);
	card.csd.sector_size = 512;
	card.fd = fd;
	card.host.check_buffer_alignment = host_dma_check;
	s_rng = strtoull(argv[3], NULL, 0) | 1;
	s_log = fopen(argv[4], "w");
	if (sscanf(argv[5], "%u,%u", &s_copy, &s_more) != 2 || !s_copy || !s_more || !s_log)
		return 2;
	s_dma = atoi(argv[6]);
	bool short_ram = argc > 7 && !strcmp(argv[7], "short");
	disk_image_say = said;
	ff_diskio_register_sdmmc(0, &card);
	ff_sdmmc_set_disk_status_check(0, false);
	static FATFS fs;
	if (f_mount(&fs, "0:", 1) != FR_OK) {
		printf("the volume did not mount\n");
		return 2;
	}
	FILINFO fi;
	CHECK(f_stat(IMG, &fi) == FR_OK, "IMAGE.IMG on the volume");
	s_size = fi.fsize;
	s_want = malloc(s_size);
	CHECK(s_want && read_whole(s_want), "IMAGE.IMG read, %llu bytes", (unsigned long long) s_size);
	printf("IMAGE.IMG: %llu sectors; COPY %u, MORE %u, DMA %d%s\n",
	       (unsigned long long) (s_size / 512), s_copy, s_more, s_dma, short_ram ? ", short" : "");

	printf("== 1. disk_image_read() and disk_image_write()\n");
	host_heap_no_internal = 0;
	if (short_ram) {
		/* the internal buffer taken at the open, then no more room */
		struct disk_image *d0 = NULL;
		uint64_t z;
		disk_image_open(IMG, false, &d0, &z);
		disk_image_close(d0);
		host_heap_no_internal = 1;
	}
	part1(900);

	printf("== 2. IDE commands through the ports\n");
	IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
	CHECK(ide_attach(ifs, 0, IMG) == 0, "the image attached as drive 0");
	ide_ioport_write(ifs, 6, 0xA0);
	IDEState *d = ifs->drives[0];
	ide_ioport_write(ifs, 2, 4);
	ide_ioport_write(ifs, 7, 0xC6);
	part2(ifs, 400, "ide");
	CHECK(d->wbuf && host_dma_check(0, d->wbuf, 128 * 512), "ide.c's buffer taken by the check");
	host_card.dma_refused[0].start = d->wbuf;
	host_card.dma_refused[0].size = 128 * 512;
	host_card.ndma_refused = 1;
	part2(ifs, 200, "refused");
	host_card.ndma_refused = 0;

	printf("== the image at the end\n");
	uint8_t *whole = malloc(s_size);
	CHECK(read_whole(whole) && !memcmp(whole, s_want, s_size), "the image read whole is the "
	      "test's copy, every byte");
	fprintf(s_log, "end %016llx\n", (unsigned long long) fnv(whole, s_size));
	CHECK(host_card.dma_copied == 0, "every buffer the card's reads and writes were handed, "
	      "the check took (%u taken, %u it would copy)", host_card.dma_direct,
	      host_card.dma_copied);
	if (s_dma)
		CHECK(s_said_dma == 1 && s_said_copy == 1, "disk_image.c said each way once (DMA %u, "
		      "copy %u)", s_said_dma, s_said_copy);
	else
		CHECK(s_said_dma == 0 && s_said_copy == 0, "this disk_image.c says neither");
	printf("card commands, the test's own whole-image reads left out: %u reads, %u writes\n",
	       host_card.reads, host_card.writes);
	fclose(s_log);
	close(fd);
	printf("test_image_dma: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
