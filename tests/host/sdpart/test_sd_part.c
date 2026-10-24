/*
 * v4.4: a card partition as the guest's whole disk (esp/main/sd_part.c,
 * hda = sd:3), against ESP-IDF's own FatFs and SD card glue on card images
 * that run_sdpart.sh lays out, with storage.c's partition code as the
 * board has it (cut out of the file by run_sdpart.sh) and ide.c built as
 * the board builds it (BUILD_ESP32).
 *
 *   test_sd_part unit                    sd_part_name(), sd_part_plan() and
 *                                        the CHS bytes on made-up sectors
 *   test_sd_part new IMAGE SECTORS       the new card: p1 FAT32, p3 FAT16
 *                                        right after it, p2 after p3
 *   test_sd_part old IMAGE SECTORS       Plat's card as it is: p1, p2 ext4,
 *                                        p3 FAT32 WIN9X (v4.5: refused)
 *   test_sd_part fat32 IMAGE SECTORS     the same card with p3 labelled
 *                                        DATA32: given whole and read
 *
 * "new" writes GUEST.TXT on p3 through the guest's own view of its disk
 * (FatFs reading the record sd_part.c made, as Windows 95 does), and
 * run_sdpart.sh then checks the card with mtools and fsck.fat.
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
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "storage.h"
#include "disk_image.h"
#include "sd_part.h"
#include "sd_part_mbr.h"
#include "host_glue.h"

extern void *rawsd;             /* storage.c's: the card */

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

/* a clock that moves on 10 us a look */
uint32_t get_uticks(void)
{
	static uint32_t t;
	return t += 10;
}

/* ==== ide.c, as the board builds it ==== */

#include "pci.h"
void *pcmalloc(long size) { return calloc(1, size); }
static char s_fatal[300];
static bool s_fatal_returns;    /* a test of a refusal: back to main() */
#include <setjmp.h>
static jmp_buf s_fatal_jmp;
void tiny386_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_fatal, sizeof(s_fatal), fmt, ap);
	va_end(ap);
	printf("  tiny386_fatal: %s\n", s_fatal);
	if (s_fatal_returns)
		longjmp(s_fatal_jmp, 1);
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
#include IDE_C

/* ==== made-up sectors for the unit checks ==== */

static void entry(uint8_t *mbr, int i, uint8_t type, uint32_t start, uint32_t count)
{
	uint8_t *e = mbr + 446 + 16 * i;
	memset(e, 0, 16);
	e[4] = type;
	for (int k = 0; k < 4; k++) {
		e[8 + k] = (uint8_t) (start >> (8 * k));
		e[12 + k] = (uint8_t) (count >> (8 * k));
	}
}

static void blank_mbr(uint8_t *mbr)
{
	memset(mbr, 0, 512);
	mbr[510] = 0x55;
	mbr[511] = 0xAA;
}

/* a FAT16 boot sector as Windows 95's FORMAT leaves one (BPB values only) */
static void fat16_vbr(uint8_t *v, uint32_t total, uint32_t hidden, const char *label)
{
	memset(v, 0, 512);
	v[0] = 0xEB; v[1] = 0x3C; v[2] = 0x90;
	memcpy(v + 3, "MSWIN4.1", 8);
	v[11] = 0; v[12] = 2;           /* 512 bytes a sector */
	v[13] = 16;
	v[14] = 1;
	v[16] = 2;
	v[17] = 0; v[18] = 2;
	if (total < 65536) {
		v[19] = (uint8_t) total; v[20] = (uint8_t) (total >> 8);
	} else {
		for (int k = 0; k < 4; k++)
			v[32 + k] = (uint8_t) (total >> (8 * k));
	}
	v[21] = 0xF8;
	v[24] = 63; v[26] = 16;
	for (int k = 0; k < 4; k++)
		v[28 + k] = (uint8_t) (hidden >> (8 * k));
	v[36] = 0x80;
	v[38] = 0x29;
	memset(v + 43, ' ', 11);
	if (label)
		memcpy(v + 43, label, strlen(label));
	memcpy(v + 54, "FAT16   ", 8);
	v[510] = 0x55;
	v[511] = 0xAA;
}

static bool refused(const uint8_t *mbr, const uint8_t *vbr, uint64_t card, int part,
		    const char *words, const char *what)
{
	struct sd_part_plan p;
	char why[200] = "";
	int r = sd_part_plan(mbr, vbr, card, part, &p, why, sizeof(why));
	bool ok = r != 0 && strstr(why, words);
	CHECK(ok, "%s: refused (%s)", what, why[0] ? why : "accepted");
	return ok;
}

/* ==== v4.5: the ini's side (esp_main.c's steps 1 to 3 and the WIN9X
   files) ==== */

static const char *s_present[4];
static const char *s_asked[8];
static int s_nasked;
static bool fake_exists(const char *path)
{
	if (s_nasked < 8)
		s_asked[s_nasked++] = strdup(path);
	for (int i = 0; i < 4; i++)
		if (s_present[i] && !strcmp(s_present[i], path))
			return true;
	return false;
}

static void ini_checks(void)
{
	char why[240];
	printf("== the ini's side of sd:N (esp_main.c's check_card_partition())\n");
	const char *none[] = { "sd:3", NULL, NULL, NULL, NULL, NULL, "/sdcard/tiny386/bios.bin",
			       "/sdcard/tiny386/vgabios.bin", NULL, NULL, NULL };
	int n = (int) (sizeof(none) / sizeof(none[0]));
	CHECK(sd_part_ini_check("sd:3", 0, false, none, n, why, sizeof(why)) == 3,
	      "hda = sd:3, the BIOS on /sdcard: partition 3");
	CHECK(sd_part_ini_check("sd:2", 3, false, none, n, why, sizeof(why)) == 2,
	      "hdd = sd:2: partition 2 (what is on it is sd_part_plan()'s)");
	CHECK(sd_part_ini_check("sd:0", 0, false, none, n, why, sizeof(why)) < 0 &&
	      strstr(why, "1 to 4"), "sd:0: refused (%s)", why);
	CHECK(sd_part_ini_check("sd:33", 0, false, none, n, why, sizeof(why)) < 0 &&
	      strstr(why, "1 to 4"), "sd:33: refused");
	CHECK(sd_part_ini_check("sd:p3", 0, false, none, n, why, sizeof(why)) < 0, "sd:p3: refused");
	CHECK(sd_part_ini_check("sd:3", 0, true, none, n, why, sizeof(why)) < 0 &&
	      strstr(why, "not a CD or a floppy"), "cda = sd:3: refused (%s)", why);
	CHECK(sd_part_ini_check("sd:3", 4, false, none, n, why, sizeof(why)) < 0 &&
	      strstr(why, "not a CD or a floppy"), "fda = sd:3: refused");
	CHECK(sd_part_ini_check("sd:3", 5, false, none, n, why, sizeof(why)) < 0, "fdb = sd:3: refused");
	const char *one[] = { "sd:3", "/sdcard3/data.img", NULL, NULL, NULL, NULL,
			      "/sdcard/tiny386/bios.bin", NULL, NULL, NULL, NULL };
	CHECK(sd_part_ini_check("sd:3", 0, false, one, n, why, sizeof(why)) < 0 &&
	      strstr(why, "/sdcard3/data.img is on it"), "hdb on /sdcard3: refused (%s)", why);
	const char *two[] = { "sd:3", NULL, NULL, NULL, "/sdcard3/boot.img", NULL,
			      "/sdcard3/bios.bin", NULL, NULL, NULL, NULL };
	CHECK(sd_part_ini_check("sd:3", 0, false, two, n, why, sizeof(why)) < 0 &&
	      strstr(why, "/sdcard3/boot.img and 1 more"), "fda and the BIOS on /sdcard3: refused "
	      "(%s)", why);
	const char *kern[] = { "sd:3", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			       "/sdcard3/vmlinux.bin", NULL, NULL };
	CHECK(sd_part_ini_check("sd:3", 0, false, kern, n, why, sizeof(why)) < 0 &&
	      strstr(why, "vmlinux.bin"), "kernel on /sdcard3: refused (v4.5; %s)", why);
	const char *init[] = { "sd:3", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			       "/sdcard3/initrd", "/sdcard3/linuxstart.bin" };
	CHECK(sd_part_ini_check("sd:3", 0, false, init, n, why, sizeof(why)) < 0 &&
	      strstr(why, "and 1 more"), "initrd and linuxstart on /sdcard3: refused");
	const char *near[] = { "sd:3", "/sdcard/x/sdcard3/a.img", "/sdcard3", "/sdcard31/b.img",
			       NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	CHECK(sd_part_ini_check("sd:3", 0, false, near, n, why, sizeof(why)) == 3,
	      "names that only look alike (/sdcard/x/sdcard3/..., /sdcard3, /sdcard31/...): not on it");

	s_present[0] = NULL;
	s_nasked = 0;
	CHECK(sd_part_route_files(3, fake_exists, why, sizeof(why)) == 0 && s_nasked == 2 &&
	      !strcmp(s_asked[0], "/sdcard3/win95.img") && !strcmp(s_asked[1], "/sdcard3/korvo-root.img"),
	      "the new card's p3: neither win95.img nor korvo-root.img there (asked %s, %s)",
	      s_nasked > 0 ? s_asked[0] : "", s_nasked > 1 ? s_asked[1] : "");
	s_present[0] = "/sdcard3/win95.img";
	CHECK(sd_part_route_files(3, fake_exists, why, sizeof(why)) < 0 && strstr(why, "win95.img") &&
	      strstr(why, "WIN9X"), "win95.img on /sdcard3: refused (%s)", why);
	s_present[0] = "/sdcard3/korvo-root.img";
	CHECK(sd_part_route_files(3, fake_exists, why, sizeof(why)) < 0 && strstr(why, "korvo-root"),
	      "korvo-root.img on /sdcard3: refused");
	s_present[0] = "/sdcard2/win95.img";
	CHECK(sd_part_route_files(3, fake_exists, why, sizeof(why)) == 0,
	      "win95.img on another partition: not this one's business");
	s_present[0] = NULL;
}

static void unit(void)
{
	static uint8_t mbr[512], vbr[512];
	struct sd_part_plan p;
	char why[200];
	const uint64_t card = 62333952;         /* a 32 GB card's sectors */

	printf("== sd_part_name()\n");
	CHECK(sd_part_name("sd:3") == 3 && sd_part_name("sd:1") == 1 && sd_part_name("sd:4") == 4,
	      "sd:1, sd:3, sd:4 are partitions 1, 3, 4");
	CHECK(sd_part_name("sd:0") < 0 && sd_part_name("sd:5") < 0 && sd_part_name("sd:") < 0 &&
	      sd_part_name("sd:33") < 0 && sd_part_name("sd:3 ") < 0 && sd_part_name("sd:p3") < 0,
	      "sd:0, sd:5, sd:, sd:33, \"sd:3 \", sd:p3 are malformed (-1)");
	CHECK(sd_part_name("/sdcard3/win95.img") == 0 && sd_part_name("win95.img") == 0 &&
	      sd_part_name("SD:3") == 0 && sd_part_name("/dev/mmcblk0") == 0,
	      "file names and SD:3 are not partitions (0)");

	printf("== the CHS bytes, at 16 heads and 63 sectors\n");
	uint8_t c[3];
	sd_part_chs(63, c);
	CHECK(c[0] == 1 && c[1] == 1 && c[2] == 0, "sector 63: head 1, sector 1, cylinder 0 "
	      "(%02x %02x %02x), as win95.img's own table has it", c[0], c[1], c[2]);
	sd_part_chs(818495, c);
	CHECK(c[0] == 15 && c[1] == 0xFF && c[2] == 0x2B, "sector 818495: cylinder 811, head "
	      "15, sector 63 (%02x %02x %02x)", c[0], c[1], c[2]);
	sd_part_chs(139264, c);
	CHECK(c[0] == 2 && c[1] == 35 && c[2] == 138, "sector 139264: cylinder 138, head 2, "
	      "sector 35 (%02x %02x %02x)", c[0], c[1], c[2]);
	sd_part_chs(1032191, c);
	CHECK(c[0] == 15 && c[1] == 0xFF && c[2] == 0xFF, "sector 1032191, the last CHS "
	      "reaches: 1023/15/63 (%02x %02x %02x)", c[0], c[1], c[2]);
	sd_part_chs(1032192, c);
	CHECK(c[0] == 15 && c[1] == 0xFF && c[2] == 0xFF, "sector 1032192, past CHS: "
	      "1023/15/63 (%02x %02x %02x)", c[0], c[1], c[2]);

	printf("== partitions the guest may have\n");
	blank_mbr(mbr);
	entry(mbr, 0, 0x0C, 8192, 131072);
	entry(mbr, 1, 0x83, 958464, 4194304);
	entry(mbr, 2, 0x0E, 139264, 818433);
	fat16_vbr(vbr, 818433, 139264, "WIN95");
	memset(why, 0, sizeof(why));
	int r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0, "the new card's p3, FAT16 type 0E after p1: accepted (%s)", why);
	CHECK(p.start == 139264 && p.count == 818433 && p.type == 0x0E && p.fat && !p.fat32 &&
	      p.fs_sectors == 818433 && p.hidden == 139264 && !strcmp(p.label, "WIN95") &&
	      !p.note[0], "  plan: start %u, %u sectors, type %02X, FAT16, label %s, no note (%s)",
	      (unsigned) p.start, (unsigned) p.count, p.type, p.label, p.note);
	CHECK(p.disk_sectors == 958608 && p.disk_sectors % 1008 == 0 &&
	      p.disk_sectors >= 139264 + 818433 && p.disk_sectors - (139264 + 818433) < 1008,
	      "  the guest's disk: %llu sectors (951 cylinders of 16 x 63), the partition's end "
	      "rounded up", (unsigned long long) p.disk_sectors);
	uint8_t g[512];
	sd_part_make_mbr(&p, mbr, g);
	CHECK(!memcmp(g, sd_part_mbr_code, sizeof(sd_part_mbr_code)) &&
	      g[446] == 0x80 && g[450] == 0x0E && g[454] == 0x00 && g[455] == 0x20 &&
	      g[456] == 0x02 && g[457] == 0x00 && g[510] == 0x55 && g[511] == 0xAA,
	      "  the guest's MBR: the boot code, slot 1 active, type 0E, start 139264, 55AA");
	bool rest = true;
	for (int k = 462; k < 510; k++)
		rest &= g[k] == 0;
	CHECK(rest, "  slots 2 to 4 empty: the guest sees this partition alone");

	fat16_vbr(vbr, 818433, 63, NULL);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && strstr(p.note, "hidden-sectors") && !p.label[0], "a volume whose hidden "
	      "sectors is still win95.img's 63: accepted, with a note (%s)", p.note);
	r = sd_part_plan(mbr, NULL, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && strstr(p.note, "could not be read"), "its first sector unreadable: "
	      "accepted, with a note");
	memset(vbr, 0, 512);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && !p.fat && strstr(p.note, "FORMAT"), "no FAT volume in it yet: "
	      "accepted, with a note (%s)", p.note);
	entry(mbr, 1, 0x83, 4333568, 4194304);
	entry(mbr, 2, 0x0C, 139264, 4194304);
	r = sd_part_plan(mbr, NULL, card, 3, &p, why, sizeof(why));
	CHECK(r == 0, "a FAT32 p3 of 2 GiB (type 0C): accepted");
	entry(mbr, 2, 0x06, 139264, 818433);
	r = sd_part_plan(mbr, NULL, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && p.type == 0x06, "a FAT16 p3 of type 06: accepted, its type kept");
	/* v4.5: the guest's disk is never more than 28-bit LBA can name
	   (IDENTIFY's words 60 and 61 hold at most 0FFFFFFFh) */
	entry(mbr, 2, 0x0E, SD_PART_LBA28 - 818433, 818433);
	entry(mbr, 1, 0x83, 958464, 1000000);
	r = sd_part_plan(mbr, NULL, 300000000, 3, &p, why, sizeof(why));
	CHECK(r == 0 && p.disk_sectors == SD_PART_LBA28, "a partition ending at sector 0FFFFFFFh, "
	      "the 28-bit limit: accepted, the guest's disk %llu sectors, no more (%s)",
	      (unsigned long long) p.disk_sectors, r ? why : "");
	entry(mbr, 2, 0x0E, SD_PART_LBA28 - 818433 - 10, 818433);
	r = sd_part_plan(mbr, NULL, 300000000, 3, &p, why, sizeof(why));
	CHECK(r == 0 && p.disk_sectors == SD_PART_LBA28,
	      "a partition ending 10 sectors short of it, in the last part cylinder: that cylinder "
	      "cut at the limit (%llu sectors; v4.4 gave 268436448)", (unsigned long long) p.disk_sectors);
	/* v4.5: the types Windows 95's boot sector reads by CHS, past the
	   504 MiB CHS reaches */
	blank_mbr(mbr);
	entry(mbr, 0, 0x0C, 8192, 1048576);
	entry(mbr, 2, 0x06, 1056768, 818433);
	fat16_vbr(vbr, 818433, 1056768, NULL);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && strstr(p.note, "CHS") && strstr(p.note, "make its type 0E"),
	      "a FAT16 p3 of type 06 past 504 MiB: accepted, with a note (%s)", p.note);
	entry(mbr, 2, 0x04, 1056768, 60000);
	fat16_vbr(vbr, 60000, 1056768, NULL);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && strstr(p.note, "make its type 0E"), "the same of type 04: the note");
	entry(mbr, 2, 0x0E, 1056768, 818433);
	fat16_vbr(vbr, 818433, 1056768, NULL);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && !p.note[0], "the same of type 0E: no note (%s)", p.note);
	entry(mbr, 0, 0x0C, 8192, 131072);
	entry(mbr, 2, 0x06, 1032192 - 818433, 818433);
	fat16_vbr(vbr, 818433, 1032192 - 818433, NULL);
	r = sd_part_plan(mbr, vbr, card, 3, &p, why, sizeof(why));
	CHECK(r == 0 && !p.note[0], "type 06 ending at sector 1032191, the last CHS reaches: no "
	      "note (%s)", p.note);
	/* a type-00 entry with no size is nothing */
	blank_mbr(mbr);
	entry(mbr, 0, 0x0C, 8192, 131072);
	entry(mbr, 2, 0x0E, 139264, 818433);
	entry(mbr, 3, 0x00, 200000, 0);
	r = sd_part_plan(mbr, NULL, card, 3, &p, why, sizeof(why));
	CHECK(r == 0, "an entry of type 00 with a start inside p3 but no size: passed over (%s)",
	      r ? why : "");

	printf("== partitions the guest may not have\n");
	blank_mbr(mbr);
	entry(mbr, 0, 0x0C, 8192, 131072);
	entry(mbr, 1, 0x83, 958464, 4194304);
	entry(mbr, 2, 0x0E, 139264, 818433);
	refused(mbr, NULL, card, 2, "Linux", "p2, Linux's root (type 83)");
	refused(mbr, NULL, card, 4, "no partition 4", "p4, an empty slot");
	refused(mbr, NULL, card, 0, "1 to 4", "partition 0");
	refused(mbr, NULL, card, 5, "1 to 4", "partition 5");
	entry(mbr, 3, 0x05, 5152768, 1000000);
	refused(mbr, NULL, card, 4, "extended", "an extended partition (05)");
	entry(mbr, 3, 0x07, 5152768, 1000000);
	refused(mbr, NULL, card, 4, "NTFS or exFAT", "an exFAT partition (07)");
	entry(mbr, 3, 0x01, 5152768, 20000);
	refused(mbr, NULL, card, 4, "FAT12", "a FAT12 partition (01)");
	entry(mbr, 3, 0xDA, 5152768, 20000);
	refused(mbr, NULL, card, 4, "type DA", "a partition of another type (DA)");
	entry(mbr, 3, 0, 0, 0);
	entry(mbr, 2, 0x0E, 139264, 900000);
	refused(mbr, NULL, card, 3, "overlaps partition 2", "p3 running into p2");
	entry(mbr, 2, 0x0E, 100000, 818433);
	refused(mbr, NULL, card, 3, "overlaps partition 1", "p3 starting inside p1");
	entry(mbr, 2, 0x0E, 8192, 131072);
	refused(mbr, NULL, card, 3, "overlaps partition 1", "p3 the same sectors as p1");
	entry(mbr, 2, 0x0E, 0, 818433);
	refused(mbr, NULL, card, 3, "sector 0", "p3 starting at sector 0, over the table");
	entry(mbr, 1, 0x83, 958464, 100);
	entry(mbr, 2, 0x0E, 62000000, 818433);
	refused(mbr, NULL, card, 3, "past the card's end", "p3 running past the card's end");
	entry(mbr, 2, 0x0C, SD_PART_LBA28 - 1000, 2048);
	refused(mbr, NULL, 300000000, 3, "128 GiB", "p3 ending past 128 GiB on a big card");
	entry(mbr, 2, 0x0E, SD_PART_LBA28 - 818432, 818433);
	refused(mbr, NULL, 300000000, 3, "128 GiB", "p3 ending at 2^28, one past what 28-bit LBA "
		"names (v4.4 took it)");
	entry(mbr, 1, 0x83, 958464, 4194304);
	entry(mbr, 2, 0x0E, 139264, 818433);
	entry(mbr, 3, 0x00, 200000, 100000);
	refused(mbr, NULL, card, 3, "overlaps partition 4", "an entry of type 00 with a size, "
		"inside p3 (v4.4 passed it over; Linux counts it)");
	entry(mbr, 3, 0, 0, 0);
	fat16_vbr(vbr, 818433, 139264, "WIN9X");
	refused(mbr, vbr, card, 3, "labelled WIN9X", "a FAT16 volume labelled WIN9X");
	memcpy(vbr + 43, "win9x      ", 11);
	refused(mbr, vbr, card, 3, "labelled WIN9X", "  in small letters");
	entry(mbr, 2, 0x0E, 139264, 1000);
	refused(mbr, NULL, card, 3, "too small", "p3 of 1000 sectors");
	entry(mbr, 2, 0x0E, 139264, SD_PART_FAT16_MAX + 2048);
	refused(mbr, NULL, card, 3, "FAT16 holds 4 GiB", "a FAT16 partition over 4 GiB");
	entry(mbr, 2, 0x0E, 139264, 818433);
	fat16_vbr(vbr, 818433 + 100, 139264, NULL);
	refused(mbr, vbr, card, 3, "bigger than its partition", "a FAT volume bigger than its "
		"partition");
	fat16_vbr(vbr, 818433, 139264, NULL);
	vbr[11] = 0; vbr[12] = 0x10;
	refused(mbr, vbr, card, 3, "4096-byte", "a FAT volume of 4096-byte sectors");
	entry(mbr, 0, 0xEE, 1, 0xFFFFFFFF);
	refused(mbr, NULL, card, 3, "GPT", "a card with a GPT (protective entry EE)");
	fat16_vbr(mbr, 4000000, 0, NULL);
	refused(mbr, NULL, card, 1, "no partition table", "a card that is one FAT volume, no table");
	memset(mbr, 0, 512);
	refused(mbr, NULL, card, 3, "no partition table", "a card with no 55AA in sector 0");

	ini_checks();
}

/* ==== the guest's own view of its disk: FatFs on the block device ==== */

static BlockDevice *s_guest;

static DSTATUS g_init(unsigned char pdrv) { (void) pdrv; return 0; }
static DSTATUS g_status(unsigned char pdrv) { (void) pdrv; return 0; }
static DRESULT g_read(unsigned char pdrv, unsigned char *buff, uint32_t sector, UINT count)
{
	(void) pdrv;
	return s_guest->read_async(s_guest, sector, buff, (int) count, NULL, NULL) ? RES_ERROR : RES_OK;
}
static DRESULT g_write(unsigned char pdrv, const unsigned char *buff, uint32_t sector, UINT count)
{
	(void) pdrv;
	return s_guest->write_async(s_guest, sector, buff, (int) count, NULL, NULL) ? RES_ERROR : RES_OK;
}
static DRESULT g_ioctl(unsigned char pdrv, unsigned char cmd, void *buff)
{
	(void) pdrv;
	switch (cmd) {
	case CTRL_SYNC: return RES_OK;
	case GET_SECTOR_COUNT: *(LBA_t *) buff = (LBA_t) s_guest->get_sector_count(s_guest); return RES_OK;
	case GET_SECTOR_SIZE: *(WORD *) buff = 512; return RES_OK;
	case GET_BLOCK_SIZE: *(DWORD *) buff = 1; return RES_OK;
	}
	return RES_PARERR;
}
static const ff_diskio_impl_t s_guest_impl = { g_init, g_status, g_read, g_write, g_ioctl };

/* the guest's disk mounted the way a DOS or Windows guest finds C:: its
   LBA 0's table, partition 1 */
static FATFS s_gfs;
static char s_gdrv[3];
static bool guest_mount(BlockDevice *bs)
{
	BYTE pd = FF_DRV_NOT_USED;
	if (ff_diskio_get_drive(&pd) != ESP_OK || pd == FF_DRV_NOT_USED)
		return false;
	s_guest = bs;
	ff_diskio_register(pd, &s_guest_impl);
	VolToPart[pd].pd = pd;
	VolToPart[pd].pt = 1;
	s_gdrv[0] = (char) ('0' + pd);
	s_gdrv[1] = ':';
	s_gdrv[2] = 0;
	FRESULT r = f_mount(&s_gfs, s_gdrv, 1);
	printf("  the guest's view mounted on FatFs drive %s: FatFs error %d, volume at sector "
	       "%lu\n", s_gdrv, r, (unsigned long) s_gfs.volbase);
	return r == FR_OK;
}

static void guest_unmount(void)
{
	f_mount(NULL, s_gdrv, 0);
	BYTE pd = (BYTE) (s_gdrv[0] - '0');
	ff_diskio_unregister(pd);
	VolToPart[pd].pt = 0;
}

static long guest_file(const char *name, char *buf, size_t size)
{
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", s_gdrv, name);
	FIL f;
	memset(&f, 0, sizeof(f));
	if (f_open(&f, path, FA_READ) != FR_OK)
		return -1;
	UINT got = 0;
	FRESULT r = f_read(&f, buf, (UINT) size - 1, &got);
	f_close(&f);
	if (r != FR_OK)
		return -1;
	buf[got] = 0;
	return got;
}

/* ==== the card ==== */

static sdmmc_card_t s_card;
static int s_fd;

/* esp_vfs_fat_sdmmc_mount()'s part after the card is up, as test_storage.c
   does it: the first FAT partition at /sdcard on a free drive */
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

static bool card_sector(uint32_t s, uint8_t *buf)
{
	return pread(s_fd, buf, 512, (off_t) s * 512) == 512;
}

static void list(void)
{
	int n;
	const struct storage_volume *v = storage_volumes(&n);
	for (int i = 0; i < n; i++)
		printf("    partition %d: type %02X, %u MiB, %s%s%s%s\n", v[i].part, v[i].type,
		       (unsigned) v[i].mib, v[i].fat ? "FAT" : "not FAT",
		       v[i].mounted ? ", at " : "", v[i].mounted ? v[i].path : "",
		       v[i].why ? ", " : "", v[i].why ? v[i].why : "");
}

static bool open_refused(int part, const char *words, const char *what)
{
	struct sd_part *p = NULL;
	char why[200] = "";
	int r = sd_part_open(part, &p, why, sizeof(why));
	bool ok = r != 0 && strstr(why, words);
	CHECK(ok, "%s: sd_part_open refuses (%s)", what, r ? why : "opened");
	return ok;
}

static int s_irqs;
static void ide_irq(void *pic, int irq, int level)
{
	(void) pic; (void) irq;
	if (level)
		s_irqs++;
}

/* READ SECTORS (20h) in LBA mode through the IDE ports, as the BIOS and
   Windows 95's driver send it: one sector into buf; the status after */
static uint32_t ata_read(IDEIFState *ifs, uint32_t lba, uint8_t *buf)
{
	ide_ioport_write(ifs, 6, 0xE0 | ((lba >> 24) & 0x0F));
	ide_ioport_write(ifs, 2, 1);
	ide_ioport_write(ifs, 3, lba & 0xFF);
	ide_ioport_write(ifs, 4, (lba >> 8) & 0xFF);
	ide_ioport_write(ifs, 5, (lba >> 16) & 0xFF);
	ide_ioport_write(ifs, 7, 0x20);
	uint32_t st = ide_status_read(ifs);
	if (st & 0x08)
		for (int k = 0; k < 256; k++) {
			uint32_t w = ide_data_readw(ifs);
			buf[2 * k] = (uint8_t) w;
			buf[2 * k + 1] = (uint8_t) (w >> 8);
		}
	return st;
}

/* WRITE SECTORS (30h) in LBA mode: one sector from buf; the status after */
static uint32_t ata_write(IDEIFState *ifs, uint32_t lba, const uint8_t *buf)
{
	ide_ioport_write(ifs, 6, 0xE0 | ((lba >> 24) & 0x0F));
	ide_ioport_write(ifs, 2, 1);
	ide_ioport_write(ifs, 3, lba & 0xFF);
	ide_ioport_write(ifs, 4, (lba >> 8) & 0xFF);
	ide_ioport_write(ifs, 5, (lba >> 16) & 0xFF);
	ide_ioport_write(ifs, 7, 0x30);
	for (int k = 0; k < 256; k++)
		ide_data_writew(ifs, buf[2 * k] | buf[2 * k + 1] << 8);
	return ide_status_read(ifs);
}

static bool all(const uint8_t *b, uint8_t v, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (b[i] != v)
			return false;
	return true;
}

static int scenario_new(uint32_t p1, uint32_t p3, uint32_t p3n, uint32_t p2)
{
	static uint8_t buf[4 * 512], want[512], sec[512];
	char why[200], text[256];

	printf("== the card's partitions, as the board mounts them\n");
	CHECK(mount_first(), "p1 mounted at /sdcard");
	storage_sd_mount_others();
	list();
	CHECK(storage_part_mounted(3) && storage_part_mounted(1) && !storage_part_mounted(2),
	      "p1 and p3 (FAT16) mounted by FatFs, p2 (ext4) not");
	CHECK(storage_card_pdrv() >= 0 && storage_card_sectors() == (uint64_t) s_card.csd.capacity,
	      "the card: FatFs drive %d, %llu sectors", storage_card_pdrv(),
	      (unsigned long long) storage_card_sectors());

	printf("== refused while FatFs has it, and p1 never let go\n");
	open_refused(3, "mounted by tiny386's own FatFs", "p3 while mounted at /sdcard3");
	CHECK(!storage_part_release(1, why, sizeof(why)) && strstr(why, "/sdcard"),
	      "p1 (/sdcard) is not let go: %s", why);
	CHECK(storage_part_mounted(1), "  p1 still mounted");
	open_refused(1, "mounted by tiny386's own FatFs", "p1");
	open_refused(2, "Linux", "p2, Linux's root");
	open_refused(4, "no partition 4", "p4, an empty slot");

	printf("== p3 let go, then given to the guest\n");
	CHECK(storage_part_release(3, why, sizeof(why)), "p3 let go");
	CHECK(!storage_part_mounted(3) && storage_part_mounted(1), "  p3 no longer mounted, p1 still");
	CHECK(storage_part_release(3, why, sizeof(why)), "  letting it go again: nothing to do");
	{
		FIL f;
		memset(&f, 0, sizeof(f));
		char path[48];
		snprintf(path, sizeof(path), "%s/tiny386/win95-sd.ini", host_vfs_drive("/sdcard"));
		CHECK(f_open(&f, path, FA_READ) == FR_OK, "  /sdcard still reads (%s)", path);
		f_close(&f);
	}
	struct sd_part_plan plan;
	CHECK(sd_part_check(3, &plan, why, sizeof(why)) == 0 && plan.start == p3 &&
	      plan.count == p3n && plan.fat && !plan.fat32 && plan.hidden == p3 && !plan.note[0],
	      "sd_part_check(3): FAT16, start %u, %u sectors, hidden %u, label \"%s\" (%s)",
	      (unsigned) plan.start, (unsigned) plan.count, (unsigned) plan.hidden, plan.label,
	      plan.note);

	host_card.nallowed = 1;
	host_card.allowed[0][0] = p3;
	host_card.allowed[0][1] = p3n;
	host_card.outside = 0;

	IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
	ide_attach(ifs, 0, "sd:3");
	ide_ioport_write(ifs, 6, 0xA0);
	IDEState *d = ifs->drives[0];
	BlockDevice *bs = d->bs;
	uint64_t want_n = ((uint64_t) p3 + p3n + 1007) / 1008 * 1008;
	CHECK(d->nb_sectors == (int64_t) want_n && d->heads == 16 && d->sectors == 63 &&
	      d->cylinders == (int) (want_n / 1008),
	      "the IDE disk: %lld sectors, %d cylinders, %d heads, %d sectors a track",
	      (long long) d->nb_sectors, d->cylinders, d->heads, d->sectors);

	/* IDENTIFY DEVICE (ECh): what SeaBIOS and Windows 95's driver read */
	ide_ioport_write(ifs, 7, 0xEC);
	uint16_t id[256];
	for (int k = 0; k < 256; k++)
		id[k] = (uint16_t) ide_data_readw(ifs);
	uint32_t lba_total = id[60] | (uint32_t) id[61] << 16;
	CHECK(lba_total == want_n && id[1] == want_n / 1008 && id[3] == 16 && id[6] == 63 &&
	      (id[49] & (1 << 9)), "IDENTIFY: %u LBA sectors, %u cylinders, %u heads, %u sectors, "
	      "LBA supported", (unsigned) lba_total, id[1], id[3], id[6]);

	printf("== what the guest reads\n");
	uint8_t mbr_card[512], expect[512];
	card_sector(0, mbr_card);
	sd_part_make_mbr(&plan, mbr_card, expect);
	uint32_t st = ata_read(ifs, 0, sec);
	CHECK(!(st & 1) && !memcmp(sec, expect, 512), "LBA 0 (READ SECTORS through the ports): "
	      "the record made for the guest, status %02X", st);
	CHECK(!memcmp(sec + 440, mbr_card + 440, 4), "  with the card's disk signature");
	CHECK(memcmp(sec, mbr_card, 512) != 0, "  not the card's own sector 0");
	card_sector(p3, want);
	st = ata_read(ifs, p3, sec);
	CHECK(!(st & 1) && !memcmp(sec, want, 512), "LBA %u: p3's boot sector, the card's own",
	      (unsigned) p3);
	card_sector(p1, want);
	CHECK(!all(want, 0, 512), "  (the card's sector %u, p1's boot sector, is not zeros)",
	      (unsigned) p1);
	st = ata_read(ifs, p1, sec);
	CHECK(!(st & 1) && all(sec, 0, 512), "LBA %u (p1's boot sector on the card): zeros",
	      (unsigned) p1);
	CHECK(bs->read_async(bs, p2, buf, 4, NULL, NULL) != 0 || all(buf, 0, 4 * 512),
	      "p2's sectors are past the guest's disk, or zeros");
	CHECK(bs->read_async(bs, (uint64_t) want_n, buf, 1, NULL, NULL) != 0,
	      "a read past the guest's disk: refused");
	CHECK(bs->read_async(bs, p3 - 2, buf, 4, NULL, NULL) == 0 && all(buf, 0, 1024) &&
	      card_sector(p3, want) && !memcmp(buf + 1024, want, 512),
	      "4 sectors across p3's start: two zeros, then the card's");
	CHECK(bs->read_async(bs, (uint64_t) p3 + p3n - 1, buf, 2, NULL, NULL) == 0 &&
	      all(buf + 512, 0, 512), "2 sectors across p3's end: the card's, then zeros");

	printf("== what the guest writes\n");
	unsigned e0 = ide_error_count();
	memset(sec, 0x5A, 512);
	st = ata_write(ifs, p3 + p3n - 1, sec);
	CHECK(!(st & 1) && card_sector(p3 + p3n - 1, want) && !memcmp(want, sec, 512),
	      "p3's last sector written (WRITE SECTORS through the ports): on the card at the "
	      "same number, status %02X", st);
	memset(buf, 0x77, sizeof(buf));
	CHECK(bs->write_async(bs, p3 + 5000, buf, 4, NULL, NULL) == 0, "4 sectors inside p3 written");
	bool same = true;
	for (int k = 0; k < 4; k++)
		same &= card_sector(p3 + 5000 + k, want) && all(want, 0x77, 512);
	CHECK(same, "  on the card");
	struct { uint64_t lba; int n; const char *what; } out[] = {
		{ 0, 1, "LBA 0, the guest's partition table" },
		{ p1, 1, "p1's boot sector" },
		{ p1 + 100, 4, "inside p1" },
		{ p3 - 1, 1, "the sector before p3" },
		{ p3 - 1, 2, "across p3's start" },
		{ (uint64_t) p3 + p3n - 1, 2, "across p3's end" },
		{ (uint64_t) p3 + p3n, 1, "the sector after p3" },
		{ p2, 1, "p2, Linux's root" },
		{ want_n - 1, 1, "the guest disk's last sector" },
	};
	memset(buf, 0xEE, sizeof(buf));
	unsigned w0 = host_card.writes;
	for (unsigned k = 0; k < sizeof(out) / sizeof(out[0]); k++)
		CHECK(bs->write_async(bs, out[k].lba, buf, out[k].n, NULL, NULL) != 0,
		      "write %s: refused", out[k].what);
	st = ata_write(ifs, 0, buf);
	CHECK(st & 1, "WRITE SECTORS to LBA 0 through the ports: the guest is told of an error "
	      "(status %02X)", st);
	CHECK(host_card.writes == w0, "  not one of them reached the card (%u writes)",
	      host_card.writes - w0);
	CHECK(ide_error_count() == e0, "  none counted as a card error (%u)", ide_error_count() - e0);

	printf("== a card that refuses once\n");
	struct disk_image_stats s0, s1;
	disk_image_get_stats(&s0, false);
	host_card.fail_n = 1;
	host_card.fail_read_at = host_card.reads + 1;
	card_sector(p3, want);
	CHECK(bs->read_async(bs, p3, sec, 1, NULL, NULL) == 0 && !memcmp(sec, want, 512),
	      "one read refused by the card: tried again, the data right");
	host_card.fail_read_at = 0;
	host_card.fail_write_at = host_card.writes + 1;
	memset(sec, 0x3C, 512);
	CHECK(bs->write_async(bs, p3 + 6000, sec, 1, NULL, NULL) == 0 &&
	      card_sector(p3 + 6000, want) && all(want, 0x3C, 512),
	      "one write refused by the card: tried again, on the card");
	host_card.fail_write_at = 0;
	disk_image_get_stats(&s1, false);
	CHECK(s1.errors - s0.errors == 2 && s1.recovered - s0.recovered == 2 &&
	      s1.rsectors - s0.rsectors == 1 && s1.wsectors - s0.wsectors == 1,
	      "  in disk_image.c's figures: 2 errors, 2 got over, 1 sector each way");
	host_card.fail_n = 1000000;
	host_card.fail_read_at = host_card.reads + 1;
	CHECK(bs->read_async(bs, p3, sec, 1, NULL, NULL) != 0 && ide_error_count() == e0 + 1,
	      "a card that refuses everything: the read fails, counted as a card error");
	host_card.fail_read_at = 0;
	host_card.fail_n = 1;
	CHECK(bs->read_async(bs, p3, sec, 1, NULL, NULL) == 0, "  and works again after");
	e0 = ide_error_count();

	printf("== the guest's own view: FatFs finds C: from the guest's LBA 0\n");
	CHECK(guest_mount(bs) && s_gfs.volbase == p3 && s_gfs.fs_type == FS_FAT16,
	      "partition 1 of the guest's table mounted: FAT16 at sector %lu",
	      (unsigned long) s_gfs.volbase);
	CHECK(guest_file("P3.TXT", text, sizeof(text)) > 0 && !strcmp(text, "on the windows volume\n"),
	      "P3.TXT, put on p3 by mtools, read through the guest's disk");
	{
		FIL f;
		memset(&f, 0, sizeof(f));
		char path[64];
		snprintf(path, sizeof(path), "%s/GUEST.TXT", s_gdrv);
		UINT done = 0;
		const char *t = "written by the guest through sd:3\n";
		CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK &&
		      f_write(&f, t, (UINT) strlen(t), &done) == FR_OK && done == strlen(t) &&
		      f_close(&f) == FR_OK, "GUEST.TXT written through the guest's disk");
		static uint8_t big[64 * 1024];
		for (unsigned k = 0; k < sizeof(big); k++)
			big[k] = (uint8_t) (k * 13 + 5);
		snprintf(path, sizeof(path), "%s/BIG.BIN", s_gdrv);
		memset(&f, 0, sizeof(f));
		CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK &&
		      f_write(&f, big, sizeof(big), &done) == FR_OK && done == sizeof(big) &&
		      f_close(&f) == FR_OK, "BIG.BIN (64 KiB) written through the guest's disk");
	}
	guest_unmount();
	ide_flush();
	CHECK(ide_error_count() == e0, "ide_flush(): no card error");
	CHECK(host_card.outside == 0, "every sector written on the card was inside p3 (%u outside, "
	      "the first %zu)", host_card.outside, host_card.first_outside);

	printf("== a second sd:3, and sd:3 as a CD (refused by esp_main.c before this)\n");
	s_fatal_returns = true;
	if (!setjmp(s_fatal_jmp)) {
		storage_sd_mount_others();      /* p3 mounted again, as at the next start */
		ide_attach(ifs, 1, "sd:3");
		CHECK(false, "sd:3 attached while FatFs has it mounted");
	} else {
		CHECK(strstr(s_fatal, "mounted by tiny386's own FatFs") != NULL,
		      "ide_attach(sd:3) with p3 mounted again: stops with \"%s\"", s_fatal);
	}
	s_fatal_returns = false;
	return 0;
}

static int scenario_old(bool win9x)
{
	char why[200], text[256];
	printf("== Plat's card as it is: p1 FAT32, p2 ext4, p3 FAT32 %s\n",
	       win9x ? "WIN9X" : "relabelled DATA32");
	CHECK(mount_first(), "p1 mounted at /sdcard");
	storage_sd_mount_others();
	list();
	open_refused(2, "Linux", "p2, Linux's root");
	open_refused(4, "no partition 4", "p4");
	CHECK(!storage_part_release(1, why, sizeof(why)), "p1 not let go: %s", why);
	CHECK(storage_part_release(3, why, sizeof(why)) && !storage_part_mounted(3), "p3 let go");
	if (win9x) {
		/* v4.5: the Linux route's WIN9X partition is not the guest's */
		struct sd_part_plan plan;
		CHECK(sd_part_check(3, &plan, why, sizeof(why)) != 0 && strstr(why, "WIN9X"),
		      "sd_part_check(3): refused (%s)", why);
		open_refused(3, "labelled WIN9X", "p3, WIN9X");
		unsigned w0 = host_card.writes;
		IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
		s_fatal_returns = true;
		if (!setjmp(s_fatal_jmp)) {
			ide_attach(ifs, 0, "sd:3");
			CHECK(false, "sd:3 attached on the WIN9X partition");
		} else {
			CHECK(strstr(s_fatal, "WIN9X") != NULL, "ide_attach(sd:3): stops with \"%s\"",
			      s_fatal);
		}
		s_fatal_returns = false;
		CHECK(host_card.writes == w0, "nothing written on the card (%u)", host_card.writes - w0);
		return 0;
	}
	host_card.nallowed = 1;
	host_card.allowed[0][0] = 532480;
	host_card.allowed[0][1] = 1048576;
	host_card.outside = 0;
	IDEIFState *ifs = ide_allocate(14, NULL, ide_irq);
	ide_attach(ifs, 0, "sd:3");
	BlockDevice *bs = ifs->drives[0]->bs;
	const struct sd_part_plan *p = sd_part_get_plan(bs->opaque);
	CHECK(p->fat32 && p->type == 0x0C && p->start == 532480 && !strcmp(p->label, "DATA32"),
	      "sd:3: FAT32 \"%s\", type %02X, from sector %u", p->label, p->type, (unsigned) p->start);
	CHECK(guest_mount(bs) && s_gfs.fs_type == FS_FAT32 && s_gfs.volbase == 532480,
	      "the guest's view: FAT32 at sector %lu", (unsigned long) s_gfs.volbase);
	CHECK(guest_file("P3.TXT", text, sizeof(text)) > 0 && !strcmp(text, "p3 marker\n"),
	      "P3.TXT read through the guest's disk");
	guest_unmount();
	CHECK(host_card.outside == 0, "nothing written outside p3 (%u)", host_card.outside);
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: test_sd_part unit | new IMAGE SECTORS P1 P3 P3N P2 | old IMAGE "
			"SECTORS | fat32 IMAGE SECTORS\n");
		return 2;
	}
	if (!strcmp(argv[1], "unit")) {
		unit();
	} else {
		if (argc < 4)
			return 2;
		s_fd = open(argv[2], O_RDWR);
		if (s_fd < 0) {
			perror(argv[2]);
			return 2;
		}
		s_card.csd.capacity = atoi(argv[3]);
		s_card.csd.sector_size = 512;
		s_card.fd = s_fd;
		if (!strcmp(argv[1], "new") && argc >= 8)
			scenario_new((uint32_t) atol(argv[4]), (uint32_t) atol(argv[5]),
				     (uint32_t) atol(argv[6]), (uint32_t) atol(argv[7]));
		else if (!strcmp(argv[1], "old"))
			scenario_old(true);
		else if (!strcmp(argv[1], "fat32"))
			scenario_old(false);
		else
			return 2;
		close(s_fd);
	}
	printf("test_sd_part %s: %d checks, %d failed\n", argv[1], checks, fails);
	return fails ? 1 : 0;
}
