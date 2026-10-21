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

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_ide_fatfs IMAGE SECTORS\n");
		return 2;
	}
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
		fclose(((BlockDeviceFile *) d->bs->opaque)->f);
		free(d->bs->opaque);
		free(d->bs);
		free(d);
		free(ifs);
	}

	/* closed, the volume mounted again: the same size and space, and the
	   writes inside the image there */
	fclose(bf->f);
	free(bf);
	free(bs);
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
