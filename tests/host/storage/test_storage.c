/*
 * storage.c's card partitions (FIRMWARE-PLAN 3.4) against ESP-IDF's own
 * FatFs (src/ff.c, R0.16) and its SD card glue (diskio/diskio.c and
 * diskio_sdmmc.c), built for the host with the Korvo-1 build's FatFs
 * settings.  The card is an image file that run_storage.sh lays out: Plat's
 * card (FAT32 p1 at sector 8192, ext4 p2, FAT32 p3 labelled WIN9X at sector
 * 532480) and some others.  Only the card's sector reads and writes and the
 * VFS registration are stand-ins (host_glue.c).  storage.c's code is the
 * file's own text, cut out by run_storage.sh (STORAGE_PARTS_C).
 *
 *   test_storage SCENARIO IMAGE
 *
 * Exit status 0 when every check passes.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "storage.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "host_glue.h"

void *rawsd;                    /* storage.c's: the card */
#include STORAGE_PARTS_C

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

/* The part of esp_vfs_fat_sdmmc_mount() after the card is up, as ESP-IDF's
   vfs_fat_sdmmc.c does it (mount_prepare_mem(), esp_vfs_fat_mount_internal(),
   s_f_mount()): a free drive, the card on it, the VFS, f_mount() with the
   drive's partition left at "auto". */
static bool mount_first(sdmmc_card_t *card)
{
	BYTE pdrv = FF_DRV_NOT_USED;
	if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED)
		return false;
	ff_diskio_register_sdmmc(pdrv, card);
	ff_sdmmc_set_disk_status_check(pdrv, false);
	char drv[3] = { (char) ('0' + pdrv), ':', 0 };
	esp_vfs_fat_conf_t conf = { .base_path = "/sdcard", .fat_drive = drv, .max_files = 5 };
	FATFS *fs = NULL;
	if (esp_vfs_fat_register(&conf, &fs) != ESP_OK)
		return false;
	FRESULT r = f_mount(fs, drv, 1);
	if (r != FR_OK) {
		printf("f_mount of the first partition: %d\n", r);
		return false;
	}
	rawsd = card;
	return true;
}

/* a file through FatFs on the volume registered at base: its size, or -1 */
static long read_file(const char *base, const char *name, char *buf, size_t size)
{
	const char *drv = host_vfs_drive(base);
	if (!drv)
		return -1;
	char path[160];
	snprintf(path, sizeof(path), "%s/%s", drv, name);
	FIL f;
	memset(&f, 0, sizeof(f));
	if (f_open(&f, path, FA_READ) != FR_OK)
		return -1;
	UINT got = 0;
	FRESULT r = f_read(&f, buf, size - 1, &got);
	f_close(&f);            /* frees the file's buffer (FF_USE_DYN_BUFFER) */
	if (r != FR_OK)
		return -1;
	buf[got] = 0;
	return got;
}

/* the console's list, as esp_main.c's card_volumes() words it */
static void list(void)
{
	int n;
	const struct storage_volume *v = storage_volumes(&n);
	for (int i = 0; i < n; i++)
		printf("    partition %d: type %02X, %u MiB, %s%s%s%s%s%s\n", v[i].part, v[i].type,
		       (unsigned) v[i].mib, v[i].fat ? "FAT" : "not FAT",
		       v[i].mounted ? ", at " : "", v[i].mounted ? v[i].path : "",
		       v[i].label[0] ? ", label " : "", v[i].label,
		       v[i].why ? (v[i].why[0] ? "" : "") : "");
	for (int i = 0; i < n; i++)
		if (v[i].why)
			printf("    partition %d not mounted: %s\n", v[i].part, v[i].why);
	const char *w = storage_win_path();
	printf("    /mnt/win: %s\n", w ? w : "(none)");
}

struct want {
	int part;
	uint8_t type;
	bool fat, mounted;
	const char *path, *label;
	uint32_t mib;
	uint32_t start;         /* the volume's first sector, if mounted */
};

static void expect(const struct want *w, int nw, const char *win)
{
	int n;
	const struct storage_volume *v = storage_volumes(&n);
	CHECK(n == nw, "%d partitions listed, %d wanted", n, nw);
	for (int i = 0; i < n && i < nw; i++) {
		CHECK(v[i].part == w[i].part, "entry %d: partition %d, %d wanted", i, v[i].part,
		      w[i].part);
		CHECK(v[i].type == w[i].type, "partition %d: type %02X, %02X wanted", w[i].part,
		      v[i].type, w[i].type);
		CHECK(v[i].fat == w[i].fat, "partition %d: fat %d, %d wanted", w[i].part, v[i].fat,
		      w[i].fat);
		CHECK(v[i].mounted == w[i].mounted, "partition %d: mounted %d, %d wanted",
		      w[i].part, v[i].mounted, w[i].mounted);
		CHECK(v[i].mib == w[i].mib, "partition %d: %u MiB, %u wanted", w[i].part,
		      (unsigned) v[i].mib, (unsigned) w[i].mib);
		if (w[i].mounted) {
			CHECK(!strcmp(v[i].path, w[i].path), "partition %d at %s, %s wanted",
			      w[i].part, v[i].path, w[i].path);
			CHECK(!strcmp(v[i].label, w[i].label), "partition %d label \"%s\", \"%s\" "
			      "wanted", w[i].part, v[i].label, w[i].label);
			FATFS *fs = host_vfs_fs(w[i].path);
			CHECK(fs && fs->volbase == w[i].start, "%s: FatFs's volume starts at "
			      "sector %lu, %lu wanted", w[i].path,
			      fs ? (unsigned long) fs->volbase : 0, (unsigned long) w[i].start);
		} else {
			CHECK(!v[i].path[0], "partition %d has a path (%s) but is not mounted",
			      w[i].part, v[i].path);
		}
	}
	const char *got = storage_win_path();
	if (win)
		CHECK(got && !strcmp(got, win), "/mnt/win is %s, %s wanted", got ? got : "(none)",
		      win);
	else
		CHECK(!got, "/mnt/win is %s, none wanted", got);
}

/* a file seen on one volume and not on the other: the two drives read
   different partitions of the card */
static void sees(const char *base, const char *name, const char *text, bool there)
{
	char buf[256];
	long n = read_file(base, name, buf, sizeof(buf));
	if (there)
		CHECK(n >= 0 && !strcmp(buf, text), "%s/%s: %s", base, name,
		      n < 0 ? "not found" : buf);
	else
		CHECK(n < 0, "%s/%s found, where it is not", base, name);
}

/* the pattern run_storage.sh put in win95.img */
static uint8_t pattern(uint32_t i)
{
	return (uint8_t) (i * 7 + (i >> 9));
}

/* writes as the firmware makes them: the guest's disk image written in
   place and synced (ide.c: fwrite, fflush, fsync = f_write, f_sync), a new
   file on the WIN9X partition, and lastboot.txt on the first */
static void writes(const char *p3)
{
	char path[64];
	FIL f;
	UINT done;

	snprintf(path, sizeof(path), "%s/win95.img", host_vfs_drive(p3));
	memset(&f, 0, sizeof(f));
	CHECK(f_open(&f, path, FA_READ | FA_WRITE) == FR_OK, "open %s", path);
	uint8_t buf[4096];
	CHECK(f_lseek(&f, 1 << 20) == FR_OK, "seek");
	CHECK(f_read(&f, buf, sizeof(buf), &done) == FR_OK && done == sizeof(buf), "read");
	bool same = true;
	for (uint32_t i = 0; i < sizeof(buf); i++)
		if (buf[i] != pattern((1 << 20) + i))
			same = false;
	CHECK(same, "win95.img at 1 MiB is not what was put there");
	memset(buf, 0xA5, sizeof(buf));
	CHECK(f_lseek(&f, 1 << 20) == FR_OK, "seek");
	CHECK(f_write(&f, buf, sizeof(buf), &done) == FR_OK && done == sizeof(buf), "write");
	CHECK(f_sync(&f) == FR_OK, "sync");
	CHECK(f_close(&f) == FR_OK, "close");

	snprintf(path, sizeof(path), "%s/NEW.TXT", host_vfs_drive(p3));
	memset(&f, 0, sizeof(f));
	CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "create %s", path);
	const char *t = "written on the WIN9X partition by test_storage\n";
	CHECK(f_write(&f, t, strlen(t), &done) == FR_OK && done == strlen(t), "write");
	CHECK(f_close(&f) == FR_OK, "close");

	snprintf(path, sizeof(path), "%s/tiny386/lastboot.txt", host_vfs_drive("/sdcard"));
	memset(&f, 0, sizeof(f));
	CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK, "create %s", path);
	t = "win95.ini\n";
	CHECK(f_write(&f, t, strlen(t), &done) == FR_OK && done == strlen(t), "write");
	CHECK(f_close(&f) == FR_OK, "close");
}

static void unmount(const char *base)
{
	const char *drv = host_vfs_drive(base);
	if (drv)
		f_mount(NULL, drv, 0);
}

/* a drive taken by something else, for the "no logical drive left" case */
static DSTATUS dummy_status(BYTE pdrv) { (void) pdrv; return STA_NOINIT; }
static DRESULT dummy_rw(BYTE pdrv, BYTE *b, uint32_t s, UINT c) { return RES_ERROR; }
static DRESULT dummy_w(BYTE pdrv, const BYTE *b, uint32_t s, UINT c) { return RES_ERROR; }
static DRESULT dummy_ioctl(BYTE pdrv, BYTE cmd, void *b) { return RES_ERROR; }
static const ff_diskio_impl_t dummy = {
	.init = dummy_status, .status = dummy_status, .read = dummy_rw,
	.write = dummy_w, .ioctl = dummy_ioctl,
};

#define P1 { 1, 0x0C, true, true, "/sdcard", "KORVO_SD", 64, 8192 }
#define P2_LINUX { 2, 0x83, false, false, "", "", 192, 0 }

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_storage SCENARIO IMAGE\n");
		return 2;
	}
	const char *sc = argv[1];
	int fd = open(argv[2], O_RDWR);
	if (fd < 0) {
		perror(argv[2]);
		return 2;
	}
	struct stat st;
	fstat(fd, &st);
	sdmmc_card_t card = {
		.csd = { .capacity = (int) (st.st_size / 512), .sector_size = 512 },
		.fd = fd,
	};
	printf("%s: %s, %lld sectors\n", sc, argv[2], (long long) (st.st_size / 512));

	CHECK(mount_first(&card), "the first FAT partition did not mount at /sdcard");
	if (!rawsd)
		return 1;
	if (!strcmp(sc, "nodrive"))
		for (BYTE d = 1; d < FF_VOLUMES; d++)
			ff_diskio_register(d, &dummy);
	host_card.reads = 0;
	int n = storage_sd_mount_others();
	printf("  storage_sd_mount_others(): %d partitions, %u sector reads\n", n,
	       host_card.reads);
	list();

	if (!strcmp(sc, "plat") || !strcmp(sc, "sameserial")) {
		/* Plat's card: p1 FAT32 at 8192, ext4 p2, FAT32 WIN9X p3 at
		   532480; "sameserial": p3 with p1's volume serial number, as a
		   copy made sector by sector would have */
		struct want w[] = {
			P1, P2_LINUX,
			{ 3, 0x0C, true, true, "/sdcard3", "WIN9X", 512, 532480 },
		};
		expect(w, 3, "/sdcard3");
		sees("/sdcard", "tiny386/freedos.ini", "; p1 freedos.ini\n", true);
		sees("/sdcard3", "tiny386/freedos.ini", "", false);
		sees("/sdcard3", "P3.TXT", "p3 marker\n", true);
		sees("/sdcard", "P3.TXT", "", false);
		/* every sector written must be inside p1 or p3 */
		host_card.allowed[0][0] = 8192;
		host_card.allowed[0][1] = 131072;
		host_card.allowed[1][0] = 532480;
		host_card.allowed[1][1] = 1048576;
		host_card.nallowed = 2;
		host_card.writes = 0;
		writes("/sdcard3");
		unmount("/sdcard3");
		unmount("/sdcard");
		printf("  %u sector writes, %u outside p1 and p3\n", host_card.writes,
		       host_card.outside);
		CHECK(host_card.writes > 0, "nothing was written");
		CHECK(host_card.outside == 0, "%u sectors written outside p1 and p3, the "
		      "first %lu", host_card.outside, (unsigned long) host_card.first_outside);
	} else if (!strcmp(sc, "nolabel")) {
		/* p3 not labelled WIN9X: /mnt/win is still the other FAT one */
		struct want w[] = {
			P1, P2_LINUX,
			{ 3, 0x0C, true, true, "/sdcard3", "DATA", 512, 532480 },
		};
		expect(w, 3, "/sdcard3");
		sees("/sdcard3", "P3.TXT", "p3 marker\n", true);
	} else if (!strcmp(sc, "superfloppy")) {
		/* no partition table: the whole card is one FAT32 volume */
		struct want w[] = {
			{ 0, 0, true, true, "/sdcard", "KORVO_SD", 128, 0 },
		};
		expect(w, 1, NULL);
		sees("/sdcard", "tiny386/freedos.ini", "; p1 freedos.ini\n", true);
	} else if (!strcmp(sc, "four")) {
		/* four FAT partitions: every logical drive of
		   CONFIG_FATFS_VOLUME_COUNT 4 used; WIN9X is p3, not the first
		   other one (a FAT16 p2) */
		struct want w[] = {
			P1,
			{ 2, 0x0E, true, true, "/sdcard2", "P2", 32, 139264 },
			{ 3, 0x0C, true, true, "/sdcard3", "WIN9X", 512, 204800 },
			{ 4, 0x0C, true, true, "/sdcard4", "P4", 128, 1253376 },
		};
		expect(w, 4, "/sdcard3");
		sees("/sdcard2", "P2.TXT", "p2 marker\n", true);
		sees("/sdcard3", "P3.TXT", "p3 marker\n", true);
		sees("/sdcard4", "P4.TXT", "p4 marker\n", true);
		sees("/sdcard4", "P3.TXT", "", false);
		sees("/sdcard", "P2.TXT", "", false);
	} else if (!strcmp(sc, "nodrive")) {
		/* Plat's card with every other logical drive already taken: p3
		   is named, not mounted, and /mnt/win is not there */
		int nv;
		const struct storage_volume *v = storage_volumes(&nv);
		CHECK(nv == 3 && v[2].fat && !v[2].mounted && v[2].why &&
		      strstr(v[2].why, "no logical drive left"),
		      "p3 should be FAT, not mounted, for want of a logical drive");
		CHECK(!storage_win_path(), "/mnt/win should be missing");
		sees("/sdcard", "tiny386/freedos.ini", "; p1 freedos.ini\n", true);
	} else if (!strcmp(sc, "exfat")) {
		/* p3 exFAT (type 07): not FAT here, not mounted, no /mnt/win */
		struct want w[] = {
			P1, P2_LINUX,
			{ 3, 0x07, false, false, "", "", 512, 0 },
		};
		expect(w, 3, NULL);
	} else if (!strcmp(sc, "p1linux")) {
		/* the first partition is not FAT: FatFs's "auto" takes p2 for
		   /sdcard, and the serial number match agrees */
		struct want w[] = {
			{ 1, 0x83, false, false, "", "", 64, 0 },
			{ 2, 0x0C, true, true, "/sdcard", "KORVO_SD", 64, 139264 },
		};
		expect(w, 2, NULL);
		sees("/sdcard", "tiny386/freedos.ini", "; p1 freedos.ini\n", true);
	} else {
		fprintf(stderr, "no scenario %s\n", sc);
		return 2;
	}
	printf("  %s: %d checks, %d failed\n", sc, checks, fails);
	close(fd);
	return fails ? 1 : 0;
}
