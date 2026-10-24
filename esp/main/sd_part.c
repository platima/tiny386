/*
 * v4.4: a partition of the card as the guest's whole hard disk, read and
 * written raw (sd_part.h says what the guest sees, and why it is laid out
 * so).
 *
 * The checks, in sd_part_plan(), before the guest gets anything:
 * - the card has an MBR (not a GPT, not one volume without a table);
 * - the partition is there, and is FAT16 (type 04, 06 or 0E) or FAT32 (0B
 *   or 0C): Linux's ext4 root (type 83), an extended partition, NTFS or
 *   exFAT are refused, and so is a FAT16 partition over 4 GiB;
 * - it lies inside the card, after sector 0, below 128 GiB (28-bit LBA,
 *   the most the guest's IDE disk can reach), and overlaps no other entry
 *   of the table, so that a write through it can land in no other
 *   partition;
 * - a FAT volume in it is no bigger than the partition.
 * Then, in sd_part_open(), the partition must not be mounted by tiny386's
 * own FatFs (esp_main.c lets /sdcardN go first, and never /sdcard).
 *
 * Writes: only inside the partition, every sector of the request, or none
 * of it.  Reads: LBA 0 is the record made here, the partition's sectors the
 * card's, everything else zeros.  A card error is tried once more, as
 * disk_image.c does, and counted in disk_image.c's figures (the speed
 * screen's Disk line, the information port).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "esp_heap_caps.h"
#include "disk_image.h"
#include "storage.h"
#include "sd_part.h"
#include "sd_part_mbr.h"

uint32_t get_uticks();

#define SDP_CYL (SD_PART_HEADS * SD_PART_SPT)   /* sectors a cylinder */
#define SDP_BUF_SECTORS 4                       /* ide.c's MAX_MULT_SECTORS */
#define SDP_MAX 4                               /* four IDE drives */

struct sd_part {
	bool used;
	int pdrv;                       /* FatFs's physical drive of the card */
	struct sd_part_plan plan;
	uint8_t mbr[512];               /* what the guest reads at LBA 0 */
	unsigned said;                  /* lines said about refused writes */
};

/* each taken when a partition is opened, from PSRAM, so that none of this
   costs internal RAM until hda = sd:N is used (and then only s_buf) */
static struct sd_part *s_parts[SDP_MAX];
static uint8_t *s_buf;                  /* internal RAM the SD host reaches by DMA */

static void *ps_alloc(size_t n)
{
	void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!p)
		p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
	if (p)
		memset(p, 0, n);
	return p;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t) (p[0] | p[1] << 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
	p[2] = (uint8_t) (v >> 16);
	p[3] = (uint8_t) (v >> 24);
}

int sd_part_name(const char *name)
{
	if (strncmp(name, "sd:", 3))
		return 0;
	if (name[3] >= '1' && name[3] <= '4' && !name[4])
		return name[3] - '0';
	return -1;
}

static const char *type_words(uint8_t t)
{
	switch (t) {
	case 0x01: return "a FAT12 partition";
	case 0x05: case 0x0F: case 0x85: return "an extended partition";
	case 0x07: return "an NTFS or exFAT partition";
	case 0x82: return "a Linux swap partition";
	case 0x83: return "a Linux partition (Linux's root is one)";
	case 0x8E: return "a Linux LVM partition";
	case 0xEF: return "an EFI system partition";
	default: return NULL;
	}
}

static bool fat16_type(uint8_t t)
{
	return t == 0x04 || t == 0x06 || t == 0x0E;
}

static bool fat32_type(uint8_t t)
{
	return t == 0x0B || t == 0x0C;
}

/* a FAT boot sector, as storage.c and FatFs's check_fs() look for one */
static bool fat_vbr(const uint8_t *s)
{
	if (s[510] != 0x55 || s[511] != 0xAA)
		return false;
	if (s[0] != 0xEB && s[0] != 0xE9 && s[0] != 0xE8)
		return false;
	return !memcmp(s + 0x52, "FAT32", 5) || !memcmp(s + 0x36, "FAT", 3);
}

static int refuse(char *why, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(why, size, fmt, ap);
	va_end(ap);
	return -1;
}

int sd_part_plan(const uint8_t *mbr, const uint8_t *vbr, uint64_t card_sectors,
		 int part, struct sd_part_plan *plan, char *why, size_t size)
{
	memset(plan, 0, sizeof(*plan));
	if (part < 1 || part > 4)
		return refuse(why, size, "partition %d: give a partition of the card's table, "
			      "1 to 4 (sd:3)", part);
	if (mbr[510] != 0x55 || mbr[511] != 0xAA)
		return refuse(why, size, "the card has no partition table (no MBR in its "
			      "first sector)");
	if (fat_vbr(mbr))
		return refuse(why, size, "the card is one FAT volume with no partition table, "
			      "so it has no partition %d", part);
	for (int i = 0; i < 4; i++)
		if (mbr[446 + 16 * i + 4] == 0xEE)
			return refuse(why, size, "the card has a GPT partition table, which tiny386 "
				      "does not read: lay the card out with an MBR (s31/newcard)");

	const uint8_t *e = mbr + 446 + 16 * (part - 1);
	uint8_t type = e[4];
	uint32_t start = le32(e + 8), count = le32(e + 12);
	if (!type || !count)
		return refuse(why, size, "the card has no partition %d", part);
	if (!fat16_type(type) && !fat32_type(type)) {
		const char *w = type_words(type);
		if (w)
			return refuse(why, size, "card partition %d is %s (type %02X): only a FAT16 "
				      "partition (type 04, 06 or 0E) or a FAT32 one (0B or 0C) can be "
				      "the guest's disk", part, w, type);
		return refuse(why, size, "card partition %d is of type %02X: only a FAT16 "
			      "partition (type 04, 06 or 0E) or a FAT32 one (0B or 0C) can be the "
			      "guest's disk", part, type);
	}
	uint64_t end = (uint64_t) start + count;
	if (start == 0)
		return refuse(why, size, "card partition %d starts at sector 0, over the "
			      "partition table itself", part);
	if (end > card_sectors)
		return refuse(why, size, "card partition %d runs to sector %llu, past the "
			      "card's end (%llu sectors): the table is wrong", part,
			      (unsigned long long) end, (unsigned long long) card_sectors);
	if (end > SD_PART_LBA28)
		return refuse(why, size, "card partition %d ends %llu MiB into the card, past "
			      "the 128 GiB the guest's IDE disk can reach (28-bit LBA): move it "
			      "nearer the card's start", part, (unsigned long long) (end >> 11));
	if (count < SD_PART_MIN)
		return refuse(why, size, "card partition %d is %u sectors, too small to be a "
			      "disk (1 MiB at least)", part, (unsigned) count);
	if (fat16_type(type) && count > SD_PART_FAT16_MAX)
		return refuse(why, size, "card partition %d is a FAT16 partition of %u MiB: "
			      "FAT16 holds 4 GiB at most (2 GiB for Windows 95); make it FAT32 "
			      "(type 0C) or smaller", part, (unsigned) (count >> 11));
	for (int i = 0; i < 4; i++) {
		const uint8_t *o = mbr + 446 + 16 * i;
		uint32_t os = le32(o + 8), on = le32(o + 12);
		if (i == part - 1 || !o[4] || !on)
			continue;
		if (start < (uint64_t) os + on && os < end)
			return refuse(why, size, "card partition %d (sectors %u to %llu) overlaps "
				      "partition %d (%u to %llu): a write to one could land in the "
				      "other, so neither is given to the guest", part, (unsigned) start,
				      (unsigned long long) end - 1, i + 1, (unsigned) os,
				      (unsigned long long) os + on - 1);
	}

	plan->part = part;
	plan->type = type;
	plan->start = start;
	plan->count = count;
	plan->disk_sectors = (end + SDP_CYL - 1) / SDP_CYL * SDP_CYL;
	if (plan->disk_sectors < 2 * SDP_CYL)
		plan->disk_sectors = 2 * SDP_CYL;

	if (!vbr) {
		snprintf(plan->note, sizeof(plan->note), "its first sector could not be "
			 "read; the guest will see what is there");
		return 0;
	}
	if (!fat_vbr(vbr)) {
		snprintf(plan->note, sizeof(plan->note), "it holds no FAT file system yet: "
			 "the guest sees an empty disk (FORMAT C: in the guest)");
		return 0;
	}
	plan->fat = true;
	plan->fat32 = !memcmp(vbr + 0x52, "FAT32", 5);
	uint16_t bps = le16(vbr + 11);
	uint32_t total = le16(vbr + 19);
	if (!total)
		total = le32(vbr + 32);
	plan->fs_sectors = total;
	plan->hidden = le32(vbr + 28);
	const uint8_t *lab = vbr + (plan->fat32 ? 0x47 : 0x2B);
	if (vbr[plan->fat32 ? 0x42 : 0x26] == 0x29) {
		memcpy(plan->label, lab, 11);
		plan->label[11] = 0;
		for (int n = 10; n >= 0 && plan->label[n] == ' '; n--)
			plan->label[n] = 0;
		if (!strcmp(plan->label, "NO NAME"))
			plan->label[0] = 0;
	}
	if (bps != 512)
		return refuse(why, size, "the FAT volume in card partition %d has %u-byte "
			      "sectors; the guest's disk has 512", part, (unsigned) bps);
	if (total > count)
		return refuse(why, size, "the FAT volume in card partition %d is %u sectors, "
			      "bigger than its partition (%u): its end would be past the "
			      "partition's", part, (unsigned) total, (unsigned) count);
	if (plan->hidden != start)
		snprintf(plan->note, sizeof(plan->note), "its boot sector's hidden-sectors "
			 "field is %u, not %u where it starts: Windows 95's boot sector finds "
			 "itself by it, so the guest may not start from this disk",
			 (unsigned) plan->hidden, (unsigned) start);
	else if (plan->fat32 != fat32_type(type))
		snprintf(plan->note, sizeof(plan->note), "its type (%02X) says FAT%s, its "
			 "boot sector FAT%s", type, plan->fat32 ? "16" : "32",
			 plan->fat32 ? "32" : "16");
	return 0;
}

void sd_part_chs(uint32_t lba, uint8_t out[3])
{
	uint32_t c = lba / SDP_CYL;
	uint32_t h = lba / SD_PART_SPT % SD_PART_HEADS;
	uint32_t s = lba % SD_PART_SPT + 1;
	if (c > 1023) {
		c = 1023;
		h = SD_PART_HEADS - 1;
		s = SD_PART_SPT;
	}
	out[0] = (uint8_t) h;
	out[1] = (uint8_t) (s | (c >> 8) << 6);
	out[2] = (uint8_t) c;
}

void sd_part_make_mbr(const struct sd_part_plan *plan, const uint8_t *card_mbr,
		      uint8_t *out)
{
	memset(out, 0, 512);
	memcpy(out, sd_part_mbr_code, sizeof(sd_part_mbr_code));
	memcpy(out + 440, card_mbr + 440, 4);   /* the card's disk signature */
	uint8_t *e = out + 446;
	e[0] = 0x80;
	sd_part_chs(plan->start, e + 1);
	e[4] = plan->type;
	sd_part_chs(plan->start + plan->count - 1, e + 5);
	put32(e + 8, plan->start);
	put32(e + 12, plan->count);
	out[510] = 0x55;
	out[511] = 0xAA;
}

const struct sd_part_plan *sd_part_get_plan(const struct sd_part *p)
{
	return &p->plan;
}

uint64_t sd_part_sectors(const struct sd_part *p)
{
	return p->plan.disk_sectors;
}

static bool buf_ready(void)
{
	if (!s_buf)
		s_buf = heap_caps_aligned_alloc(64, SDP_BUF_SECTORS * 512,
						MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	return s_buf != NULL;
}

/* the card's MBR into mbr (and the partition's first sector into vbr, 512
   bytes each) and partition part's plan from them, as sd_part_open() and
   sd_part_check() want them; the card's drive in *pdrv */
static int load_plan(int part, int *pdrv, uint8_t *mbr, uint8_t *vbr,
		     struct sd_part_plan *plan, char *why, size_t size)
{
	*pdrv = storage_card_pdrv();
	if (*pdrv < 0)
		return refuse(why, size, "there is no card to take partition %d from", part);
	if (storage_part_mounted(part))
		return refuse(why, size, "card partition %d is mounted by tiny386's own FatFs, "
			      "so the guest cannot have it whole as well", part);
	if (!buf_ready())
		return refuse(why, size, "no internal RAM for a 2 KiB sector buffer");
	BYTE d = (BYTE) *pdrv;
	if (disk_read(d, s_buf, 0, 1) != RES_OK && disk_read(d, s_buf, 0, 1) != RES_OK)
		return refuse(why, size, "the card's first sector could not be read");
	memcpy(mbr, s_buf, 512);
	bool have_vbr = false;
	uint64_t card = storage_card_sectors();
	if (part >= 1 && part <= 4) {
		uint32_t start = le32(mbr + 446 + 16 * (part - 1) + 8);
		if (start && start < card) {
			have_vbr = disk_read(d, s_buf, start, 1) == RES_OK ||
				disk_read(d, s_buf, start, 1) == RES_OK;
			if (have_vbr)
				memcpy(vbr, s_buf, 512);
		}
	}
	return sd_part_plan(mbr, have_vbr ? vbr : NULL, card, part, plan, why, size);
}

int sd_part_check(int part, struct sd_part_plan *plan, char *why, size_t size)
{
	uint8_t *tmp = ps_alloc(1024);
	if (!tmp)
		return refuse(why, size, "no memory for two sectors");
	int pdrv;
	int r = load_plan(part, &pdrv, tmp, tmp + 512, plan, why, size);
	heap_caps_free(tmp);
	return r;
}

int sd_part_open(int part, struct sd_part **out, char *why, size_t size)
{
	int slot = -1;
	for (int i = 0; i < SDP_MAX && slot < 0; i++)
		if (!s_parts[i])
			slot = i;
	if (slot < 0)
		return refuse(why, size, "four card partitions are open already");
	struct sd_part *p = ps_alloc(sizeof(*p));
	uint8_t *tmp = ps_alloc(1024);
	int r = -1;
	if (!p || !tmp)
		refuse(why, size, "no memory for the partition's state");
	else if (!load_plan(part, &p->pdrv, tmp, tmp + 512, &p->plan, why, size))
		r = 0;
	if (r == 0) {
		sd_part_make_mbr(&p->plan, tmp, p->mbr);
		p->used = true;
		s_parts[slot] = p;
		*out = p;
	} else if (p) {
		heap_caps_free(p);
	}
	if (tmp)
		heap_caps_free(tmp);
	return r;
}

/* one run of whole sectors of the card, through the internal buffer, tried
   twice; 0, or -1 if the card refused it both times */
static int card_rw(struct sd_part *p, uint32_t sector, uint8_t *buf, unsigned n,
		   bool write, bool *retried)
{
	uint32_t t0 = get_uticks();
	*retried = false;
	int ret = 0;
	unsigned left = n;
	uint32_t s = sector;
	uint8_t *b = buf;
	while (left > 0) {
		unsigned k = left < SDP_BUF_SECTORS ? left : SDP_BUF_SECTORS;
		DRESULT r;
		if (write) {
			memcpy(s_buf, b, k * 512);
			r = disk_write((BYTE) p->pdrv, s_buf, s, k);
			if (r != RES_OK) {
				*retried = true;
				r = disk_write((BYTE) p->pdrv, s_buf, s, k);
			}
		} else {
			r = disk_read((BYTE) p->pdrv, s_buf, s, k);
			if (r != RES_OK) {
				*retried = true;
				r = disk_read((BYTE) p->pdrv, s_buf, s, k);
			}
			if (r == RES_OK)
				memcpy(b, s_buf, k * 512);
		}
		if (r != RES_OK) {
			ret = -1;
			break;
		}
		left -= k;
		s += k;
		b += k * 512;
	}
	disk_image_count(write, n, get_uticks() - t0, *retried, *retried && !ret);
	return ret;
}

int sd_part_read(struct sd_part *p, uint64_t lba, void *buf, unsigned n)
{
	uint8_t *b = buf;
	if (!p || !p->used || lba + n > p->plan.disk_sectors)
		return SD_PART_REFUSED;
	uint64_t ps = p->plan.start, pe = ps + p->plan.count;
	while (n > 0) {
		if (lba >= ps && lba < pe) {
			unsigned k = (uint64_t) n < pe - lba ? n : (unsigned) (pe - lba);
			bool retried;
			if (card_rw(p, (uint32_t) lba, b, k, false, &retried))
				return SD_PART_CARD_ERROR;
			lba += k;
			b += k * 512;
			n -= k;
			continue;
		}
		if (lba == 0)
			memcpy(b, p->mbr, 512);
		else
			memset(b, 0, 512);
		lba++;
		b += 512;
		n--;
	}
	return 0;
}

int sd_part_write(struct sd_part *p, uint64_t lba, const void *buf, unsigned n)
{
	if (!p || !p->used)
		return SD_PART_REFUSED;
	uint64_t ps = p->plan.start, pe = ps + p->plan.count;
	if (lba < ps || lba + n > pe || !n) {
		/* the first few, then every hundredth */
		if (p->said < 4 || !(p->said % 100)) {
			char line[200];
			snprintf(line, sizeof(line), "sd:%d: the guest wrote %u sector%s at %llu, "
				 "outside card partition %d (%llu to %llu); refused, as every "
				 "write outside it is.", p->plan.part, n, n == 1 ? "" : "s",
				 (unsigned long long) lba, p->plan.part, (unsigned long long) ps,
				 (unsigned long long) pe - 1);
			if (disk_image_say)
				disk_image_say(true, line);
			else
				fprintf(stderr, "%s\n", line);
		}
		p->said++;
		return SD_PART_REFUSED;
	}
	bool retried;
	if (card_rw(p, (uint32_t) lba, (uint8_t *) buf, n, true, &retried))
		return SD_PART_CARD_ERROR;
	return 0;
}

int sd_part_sync(struct sd_part *p)
{
	if (!p || !p->used)
		return -1;
	return disk_ioctl((BYTE) p->pdrv, CTRL_SYNC, NULL) == RES_OK ? 0 : -1;
}

void sd_part_close(struct sd_part *p)
{
	for (int i = 0; p && i < SDP_MAX; i++)
		if (s_parts[i] == p) {
			s_parts[i] = NULL;
			heap_caps_free(p);
		}
}
