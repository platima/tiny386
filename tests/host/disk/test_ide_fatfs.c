/*
 * Host test of ide.c's disk image block device through ESP-IDF's own FatFs,
 * the way the board reaches a disk image on the card (boot safety review,
 * round 2, m1).  ESP-IDF's vfs_fat.c turns fopen(path, "r+b") into
 * FA_READ | FA_WRITE | FA_OPEN_EXISTING and passes every lseek() to
 * f_lseek() with no bound; f_lseek() on a file open for writing stretches
 * the file to the place asked for.  So on the board a guest that reads or
 * writes past the end of its image made the image grow on the card, up to
 * filling the partition.  ide.c on BUILD_ESP32 now refuses any sector past
 * the end.
 *
 * Here ide.c is compiled as the board compiles it (BUILD_ESP32), its FILE
 * calls going to a stand-in for vfs_fat.c built on glibc's fopencookie():
 * open, read, write, seek (straight to f_lseek(), as vfs_fat.c does),
 * fsync (f_sync()).  The FatFs is ESP-IDF's ff.c, diskio.c and
 * diskio_sdmmc.c, with tests/host/storage's stubs and sector glue.
 *
 *   test_ide_fatfs IMAGE SECTORS     a FAT volume holding FREEDOS.IMG
 *
 * Exit status 0 when every check passes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"

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

/* ==== vfs_fat.c, as far as ide.c needs it ==== */

#define HF_MAX 4
#define HF_FD0 100
struct hf {
	FIL fil;
	FILE *f;
	bool used;
	char path[64];
};
static struct hf s_hf[HF_MAX];

static ssize_t hf_read(void *c, char *buf, size_t n)
{
	struct hf *h = c;
	UINT got = 0;
	if (f_read(&h->fil, buf, n, &got) != FR_OK) {
		errno = EIO;
		return -1;
	}
	return got;
}

static ssize_t hf_write(void *c, const char *buf, size_t n)
{
	struct hf *h = c;
	UINT put = 0;
	if (f_write(&h->fil, buf, n, &put) != FR_OK || put != n) {
		errno = EIO;
		return put ? (ssize_t) put : -1;
	}
	return put;
}

/* vfs_fat_lseek(): the new place worked out, then f_lseek() with no bound */
static int hf_seek(void *c, off64_t *off, int whence)
{
	struct hf *h = c;
	int64_t pos;
	switch (whence) {
	case SEEK_SET: pos = *off; break;
	case SEEK_CUR: pos = (int64_t) f_tell(&h->fil) + *off; break;
	case SEEK_END: pos = (int64_t) f_size(&h->fil) + *off; break;
	default: errno = EINVAL; return -1;
	}
	if (pos < 0) {
		errno = EINVAL;
		return -1;
	}
	if (f_lseek(&h->fil, (FSIZE_t) pos) != FR_OK) {
		errno = EIO;
		return -1;
	}
	*off = (off64_t) f_tell(&h->fil);
	return 0;
}

static int hf_close(void *c)
{
	struct hf *h = c;
	FRESULT r = f_close(&h->fil);
	h->used = false;
	return r == FR_OK ? 0 : -1;
}

FILE *hf_fopen(const char *path, const char *mode)
{
	BYTE m = FA_READ | FA_OPEN_EXISTING;
	if (strchr(mode, '+'))
		m |= FA_WRITE;
	for (int i = 0; i < HF_MAX; i++) {
		if (s_hf[i].used)
			continue;
		/* as vfs_fat.c's calloc(): f_open() takes a NULL sector buffer
		   to mean "allocate one" (FF_USE_DYN_BUFFER) */
		memset(&s_hf[i].fil, 0, sizeof(s_hf[i].fil));
		if (f_open(&s_hf[i].fil, path, m) != FR_OK) {
			errno = ENOENT;
			return NULL;
		}
		cookie_io_functions_t io = { hf_read, hf_write, hf_seek, hf_close };
		snprintf(s_hf[i].path, sizeof(s_hf[i].path), "%s", path);
		s_hf[i].f = fopencookie(&s_hf[i], mode, io);
		s_hf[i].used = s_hf[i].f != NULL;
		return s_hf[i].f;
	}
	errno = EMFILE;
	return NULL;
}

int hf_fileno(FILE *f)
{
	for (int i = 0; i < HF_MAX; i++)
		if (s_hf[i].used && s_hf[i].f == f)
			return HF_FD0 + i;
	return -1;
}

/* vfs_fat_fsync(): f_sync() */
int hf_fsync(int fd)
{
	int i = fd - HF_FD0;
	if (i < 0 || i >= HF_MAX || !s_hf[i].used)
		return -1;
	return f_sync(&s_hf[i].fil) == FR_OK ? 0 : -1;
}

/* ==== what disk_image.c (v3.8) wants from the firmware ==== */

#include "disk_image.h"

/* a clock that moves on 10 us a look */
uint32_t get_uticks(void)
{
	static uint32_t t;
	return t += 10;
}

/* storage.c's map of VFS paths to FatFs's: the test names images by
   FatFs's own paths ("0:/FREEDOS.IMG").  "stdio": none is on a volume, so
   ide.c takes its C stdio path (through hf_fopen() here), as before
   v3.8. */
static bool s_stdio;
bool storage_fatfs_path(const char *path, char *out, size_t size)
{
	if (s_stdio || strncmp(path, "0:/", 3))
		return false;
	snprintf(out, size, "%s", path);
	return true;
}

/* ==== ide.c, as the board builds it ==== */

#include "pci.h"
void *pcmalloc(long size) { return calloc(1, size); }
void tiny386_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("tiny386_fatal: ");
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
#define fopen hf_fopen
#define fileno(f) hf_fileno(f)
#define fsync(fd) hf_fsync(fd)
#include IDE_C
#undef fopen
#undef fileno
#undef fsync

/* ==== the test ==== */

#include "host_glue.h"          /* host_card: the card's sector counts */

static int s_irqs;
static void ide_irq(void *pic, int irq, int level)
{
	(void) pic; (void) irq;
	if (level)
		s_irqs++;
}

static DWORD free_clusters(void)
{
	DWORD fre = 0;
	FATFS *fs;
	f_getfree("0:", &fre, &fs);
	return fre;
}

/* the image's size: in its directory entry, and as the open file object
   has it, where a stretch shows at once (the entry follows at the sync) */
static FSIZE_t image_size(void)
{
	FILINFO fi;
	if (f_stat("0:/FREEDOS.IMG", &fi) != FR_OK)
		return 0;
	for (int i = 0; i < HF_MAX; i++)
		if (s_hf[i].used && !strcmp(s_hf[i].path, "0:/FREEDOS.IMG") &&
		    f_size(&s_hf[i].fil) != fi.fsize)
			return f_size(&s_hf[i].fil);
	return fi.fsize;
}

/* a block device the test made, closed whichever way ide.c opened it */
static void close_bs(BlockDevice *bs)
{
	BlockDeviceFile *bf = bs->opaque;
#ifdef BF_IMG
	if (bf->img)
		disk_image_close(bf->img);
#endif
	if (bf->f)
		fclose(bf->f);
	free(bf);
	free(bs);
}

#ifdef BF_IMG
/* the byte at offset o of FRAG.IMG */
static uint8_t frag_byte(uint32_t o)
{
	return (uint8_t) (o * 7 + (o >> 9));
}

/* FRAG.IMG in pieces one cluster long: a cluster of it and one of PAD.BIN
   in turn, as FatFs allocates them */
static bool make_frag(int pieces, UINT csize)
{
	FIL a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	if (f_open(&a, "0:/FRAG.IMG", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK ||
	    f_open(&b, "0:/PAD.BIN", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return false;
	uint8_t *buf = malloc(csize);
	UINT done;
	bool ok = true;
	for (int i = 0; i < pieces && ok; i++) {
		for (UINT k = 0; k < csize; k++)
			buf[k] = frag_byte((uint32_t) i * csize + k);
		ok = f_write(&a, buf, csize, &done) == FR_OK && done == csize;
		memset(buf, 0xEE, csize);
		ok = ok && f_write(&b, buf, csize, &done) == FR_OK && done == csize;
	}
	free(buf);
	ok = f_close(&a) == FR_OK && ok;
	ok = f_close(&b) == FR_OK && ok;
	return ok;
}

/* every sector of FRAG.IMG read once, in an order that jumps back and
   forth; returns the FAT sectors read meanwhile */
static unsigned read_scrambled(BlockDevice *fb, int64_t nb, bool *data_ok, unsigned *card_reads)
{
	static uint8_t sec[512];
	unsigned w0 = host_card.watch_reads, r0 = host_card.reads;
	*data_ok = true;
	for (int64_t i = 0; i < nb; i++) {
		int64_t s = (i * 7919) % nb;            /* 7919 is prime, nb is not a multiple */
		if (fb->read_async(fb, (uint64_t) s, sec, 1, NULL, NULL) != 0) {
			*data_ok = false;
			continue;
		}
		for (int k = 0; k < 512; k++)
			if (sec[k] != frag_byte((uint32_t) (s * 512 + k)))
				*data_ok = false;
	}
	*card_reads = host_card.reads - r0;
	return host_card.watch_reads - w0;
}

/* v3.8: the image's clusters mapped once (FatFs's fast seek), so that a
   seek reads no FAT, whatever pieces the image is in */
static void fast_seek_checks(void)
{
	FATFS *fs;
	DWORD fre;
	f_getfree("0:", &fre, &fs);
	UINT csize = fs->csize * 512;
	const int pieces = 300;
	CHECK(make_frag(pieces, csize), "FRAG.IMG made in %d pieces of %u bytes", pieces, csize);
	host_card.watch_start = fs->fatbase;
	host_card.watch_n = (size_t) fs->fsize * fs->n_fats;

	BlockDevice *fb = block_device_init("0:/FRAG.IMG", BF_MODE_RO);
	int64_t nb = ((BlockDeviceFile *) fb->opaque)->nb_sectors;
	bool ok;
	unsigned reads;
	unsigned fat = read_scrambled(fb, nb, &ok, &reads);
	CHECK(ok, "mapped: all %lld sectors of FRAG.IMG read back as written", (long long) nb);
	CHECK(fat == 0, "mapped: %u FAT sector reads for %lld sectors read out of order (0 wanted)",
	      fat, (long long) nb);
	CHECK(reads == (unsigned) nb, "mapped: %u card reads for %lld sectors, one each",
	      reads, (long long) nb);
	close_bs(fb);

	/* without the map, as through ESP-IDF's VFS before v3.8: the test
	   sees the FAT walks */
	disk_image_fast_seek = 0;
	fb = block_device_init("0:/FRAG.IMG", BF_MODE_RO);
	fat = read_scrambled(fb, nb, &ok, &reads);
	CHECK(ok, "not mapped: the sectors read back as written");
	CHECK(fat > (unsigned) nb, "not mapped: %u FAT sector reads for %lld sectors (the walks "
	      "the map saves)", fat, (long long) nb);
	close_bs(fb);
	disk_image_fast_seek = 1;
	host_card.watch_n = 0;
	f_unlink("0:/FRAG.IMG");
	f_unlink("0:/PAD.BIN");
}

/* v3.8: a card error costs one access a second try, not the disk for the
   rest of the run */
static void recovery_checks(BlockDevice *bs, int64_t nb)
{
	static uint8_t want[512], got[512];
	struct disk_image_stats s0, s1;
	unsigned e0 = ide_error_count();
	int r;

	memset(want, 0xC3, 512);
	CHECK(bs->write_async(bs, 100, want, 1, NULL, NULL) == 0, "sector 100 written");

	disk_image_get_stats(&s0, false);
	host_card.fail_n = 1;
	host_card.fail_read_at = host_card.reads + 1;
	memset(got, 0, 512);
	r = bs->read_async(bs, 100, got, 1, NULL, NULL);
	host_card.fail_read_at = 0;
	disk_image_get_stats(&s1, false);
	CHECK(r == 0 && !memcmp(got, want, 512), "one read refused by the card: opened again, "
	      "the read done, the data right");
	CHECK(s1.errors == s0.errors + 1 && s1.recovered == s0.recovered + 1,
	      "  counted as one error got over (%u, %u)", s1.errors - s0.errors,
	      s1.recovered - s0.recovered);
	CHECK(ide_error_count() == e0, "  not one the guest was told of");
	bool later = true;
	for (int i = 0; i < 64; i++)
		later &= bs->read_async(bs, (uint64_t) ((i * 977) % nb), got, 1, NULL, NULL) == 0;
	CHECK(later, "  every read after it works (64 reads)");

	memset(want, 0x3C, 512);
	disk_image_get_stats(&s0, false);
	host_card.fail_write_at = host_card.writes + 1;
	r = bs->write_async(bs, 101, want, 1, NULL, NULL);
	host_card.fail_write_at = 0;
	disk_image_get_stats(&s1, false);
	memset(got, 0, 512);
	CHECK(r == 0 && bs->read_async(bs, 101, got, 1, NULL, NULL) == 0 &&
	      !memcmp(got, want, 512), "one write refused by the card: opened again, the write "
	      "done, the sector reads back");
	CHECK(s1.errors == s0.errors + 1 && s1.recovered == s0.recovered + 1,
	      "  counted as one error got over");

	/* a card that refuses everything for a while (pulled out): the guest
	   is told, and once the card answers again the disk works again */
	host_card.fail_n = 1000000;
	host_card.fail_read_at = host_card.reads + 1;
	r = bs->read_async(bs, 100, got, 1, NULL, NULL);
	CHECK(r != 0, "a card that refuses everything: the read fails");
	CHECK(ide_error_count() == e0 + 1, "  and the guest is told (%u)", ide_error_count() - e0);
	host_card.fail_read_at = 0;
	host_card.fail_n = 1;
	memset(got, 0, 512);
	memset(want, 0xC3, 512);
	r = bs->read_async(bs, 100, got, 1, NULL, NULL);
	CHECK(r == 0 && !memcmp(got, want, 512), "the card answering again: the next read works "
	      "and is right");
}
#endif

/* before v3.8 (ide.c through C stdio and the VFS): one refused read made
   FatFs refuse the open image from then on; returns whether it did */
static bool stdio_sticky(BlockDevice *bs)
{
	static uint8_t got[512];
	host_card.fail_n = 1;
	host_card.fail_read_at = host_card.reads + 1;
	int r1 = bs->read_async(bs, 100, got, 1, NULL, NULL);
	host_card.fail_read_at = 0;
	int r2 = bs->read_async(bs, 100, got, 1, NULL, NULL);
	int r3 = bs->read_async(bs, 2000, got, 1, NULL, NULL);
	printf("  stdio path: a refused read gives %d; the next two, the card answering "
	       "again, give %d and %d\n", r1, r2, r3);
	return r1 != 0 && r2 != 0 && r3 != 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_ide_fatfs IMAGE SECTORS [stdio]\n");
		return 2;
	}
	s_stdio = argc > 3 && !strcmp(argv[3], "stdio");
	setvbuf(stdout, NULL, _IONBF, 0);       /* every line, even if it crashes */
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror(argv[1]);
		return 2;
	}
	static sdmmc_card_t card;
	card.csd.capacity = atoi(argv[2]);
	card.csd.sector_size = 512;
	card.fd = fd;
	ff_diskio_register_sdmmc(0, &card);
	ff_sdmmc_set_disk_status_check(0, false);
	static FATFS fs;
	if (f_mount(&fs, "0:", 1) != FR_OK) {
		printf("the volume did not mount\n");
		return 2;
	}
	DWORD free0 = free_clusters();
	FSIZE_t size0 = image_size();
	printf("%s: FREEDOS.IMG %llu bytes, %lu free clusters\n", argv[1],
	       (unsigned long long) size0, (unsigned long) free0);

	BlockDevice *bs = block_device_init("0:/FREEDOS.IMG", BF_MODE_RW);
	BlockDeviceFile *bf = bs->opaque;
	int64_t nb = bf->nb_sectors;
	static uint8_t buf[512 * 4];
	printf("  the image has %lld sectors\n", (long long) nb);

	/* inside the image: as before */
	CHECK(bs->read_async(bs, nb - 1, buf, 1, NULL, NULL) == 0, "the last sector reads");
	memset(buf, 0x5A, 512);
	CHECK(bs->write_async(bs, nb - 1, buf, 1, NULL, NULL) == 0, "the last sector is written");
	CHECK(bs->write_async(bs, 0, buf, 2, NULL, NULL) == 0, "the first two sectors are written");
#ifdef BF_ACTIVITY
	/* the status LED's disk light (v3.8): one more on the count for every
	   read and every write the guest asks for, one refused for being past
	   the end included */
	{
		unsigned a0 = ide_activity;
		bool r1 = bs->read_async(bs, 1, buf, 1, NULL, NULL) == 0;
		bool w1 = bs->write_async(bs, 1, buf, 1, NULL, NULL) == 0;
		bool r2 = bs->read_async(bs, 0, buf, 2, NULL, NULL) == 0;
		bool past = bs->read_async(bs, (uint64_t) nb, buf, 1, NULL, NULL) != 0;
		CHECK(r1 && w1 && r2 && past && ide_activity == a0 + 4,
		      "the disk light's count: %u more for four accesses (a read, a write, a "
		      "read of two sectors, a read past the end)", ide_activity - a0);
	}
#endif

	/* past its end: refused, and nothing on the card changes */
	struct { uint64_t sector; int n; const char *what; } past[] = {
		{ (uint64_t) nb, 1, "the sector after the last" },
		{ (uint64_t) nb - 1, 2, "two sectors across the end" },
		{ (uint64_t) nb + 1000, 1, "1,000 sectors past the end" },
		{ 0xFFFFFE00ull / 512, 1, "at byte 0xFFFFFE00, FatFs's largest file" },
	};
	for (unsigned i = 0; i < sizeof(past) / sizeof(past[0]); i++) {
		CHECK(bs->read_async(bs, past[i].sector, buf, past[i].n, NULL, NULL) != 0,
		      "read, %s: refused", past[i].what);
		CHECK(image_size() == size0, "  the image is still %llu bytes (now %llu)",
		      (unsigned long long) size0, (unsigned long long) image_size());
		CHECK(bs->write_async(bs, past[i].sector, buf, past[i].n, NULL, NULL) != 0,
		      "write, %s: refused", past[i].what);
		CHECK(image_size() == size0, "  the image is still %llu bytes (now %llu)",
		      (unsigned long long) size0, (unsigned long long) image_size());
	}
	ide_flush();
	CHECK(ide_error_count() == 0, "no card errors counted (%u): a sector past the end is "
	      "the guest's mistake, not the card's", ide_error_count());
	CHECK(free_clusters() == free0, "free clusters %lu, as before (%lu)",
	      (unsigned long) free_clusters(), (unsigned long) free0);

	/* ATA FLUSH CACHE (E7h) and its 48-bit form (EAh), as Windows 98 and
	   later and Linux guests send at shutdown: done, not refused, and the
	   image's last writes go to the card (emulator correctness review,
	   round 2, note 7).  On a second image, FLUSH.IMG, attached as an IDE
	   disk the way pc.c does it. */
	{
		static const uint8_t cmds[2] = { 0xE7, 0xEA };
		IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
		ide_attach(ifs, 0, "0:/FLUSH.IMG");
		ide_ioport_write(ifs, 6, 0xA0);         /* drive 0 */
		IDEState *d = ifs->drives[0];
		for (int k = 0; k < 2; k++) {
			memset(buf, 0x30 + k, 512);
			CHECK(d->bs->write_async(d->bs, 3, buf, 1, NULL, NULL) == 0,
			      "FLUSH.IMG: a sector written through its disk");
			unsigned w0 = host_card.writes;
			s_irqs = 0;
			ide_ioport_write(ifs, 7, cmds[k]);
			uint32_t st = ide_status_read(ifs);
			CHECK((st & 0x41) == 0x40 && s_irqs > 0,
			      "command %02Xh: status %02X (ready, no error), %d interrupt%s",
			      cmds[k], st, s_irqs, s_irqs == 1 ? "" : "s");
			CHECK(host_card.writes > w0, "command %02Xh: what FatFs held back went to "
			      "the card (%u sector write%s)", cmds[k], host_card.writes - w0,
			      host_card.writes - w0 == 1 ? "" : "s");
		}
		CHECK(ide_error_count() == 0, "no card errors (%u)", ide_error_count());
		close_bs(d->bs);
		free(d);
		free(ifs);
	}

#ifdef BF_IMG
	if (!s_stdio) {
		CHECK(bf->img && !bf->f, "the image is open through disk_image.c, not stdio");
		recovery_checks(bs, nb);
		fast_seek_checks();
	} else {
		CHECK(!bf->img && bf->f, "the image is open through stdio");
#else
	{
#endif
		/* the test sees the fault that disk_image.c's second try removes */
		CHECK(stdio_sticky(bs), "stdio path, as before v3.8: after one refused read FatFs "
		      "refused the image from then on");
	}

	/* closed, the volume mounted again: the same size and space, and the
	   writes inside the image there */
	close_bs(bs);
	f_mount(NULL, "0:", 0);
	if (f_mount(&fs, "0:", 1) != FR_OK) {
		printf("the volume did not mount again\n");
		return 2;
	}
	CHECK(image_size() == size0 && free_clusters() == free0,
	      "mounted again: %llu bytes, %lu free clusters", (unsigned long long) image_size(),
	      (unsigned long) free_clusters());
	/* zeroed: with FF_USE_DYN_BUFFER, f_open() allocates the sector
	   buffer only if fp->buf is NULL, as vfs_fat.c's calloc() leaves it */
	FIL f;
	memset(&f, 0, sizeof(f));
	UINT got = 0;
	memset(buf, 0, 512);
	bool ok = f_open(&f, "0:/FREEDOS.IMG", FA_READ) == FR_OK &&
		f_lseek(&f, (FSIZE_t) (nb - 1) * 512) == FR_OK &&
		f_read(&f, buf, 512, &got) == FR_OK && got == 512;
	f_close(&f);
	int same = ok;
	for (int i = 0; ok && i < 512; i++)
		same &= buf[i] == 0x5A;
	CHECK(same, "the last sector holds what was written there");
	f_mount(NULL, "0:", 0);
	close(fd);
	printf("test_ide_fatfs: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
