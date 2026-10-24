/*
 * v4.4: a partition of the card as the guest's whole hard disk, read and
 * written raw ("partition passthrough", hda = sd:3 in an ini).
 *
 * The guest gets a disk of its own: its LBA 0 a master boot record made
 * here, listing that one partition at the sector where it starts on the
 * card, and every other sector reading as zeros and refusing writes.  The
 * partition's own sectors are the card's, at the same numbers.  So the
 * volume in it stays a normal FAT16 or FAT32 volume of the card, with its
 * hidden-sectors field right for both: a PC or the board's Linux mounts,
 * checks and copies it while tiny386 is not running, and Windows 95's boot
 * sector, which finds its own partition entry by that field, boots from it.
 * Nothing outside the partition can be written through it, the card's
 * partition table included.
 *
 * Plain C over FatFs's disk functions (disk_read() and disk_write() of the
 * card's physical drive), so that the host tests and the PC harness run
 * this very file against a card image.
 */
#ifndef SD_PART_H
#define SD_PART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The guest disk's geometry: tiny386's IDE disks are 16 heads, 63 sectors
   a track (ide.c), and SeaBIOS as tiny386 builds it uses no translation */
#define SD_PART_HEADS 16
#define SD_PART_SPT 63
/* The highest sector the guest's IDE disk can reach: 28-bit LBA */
#define SD_PART_LBA28 (1u << 28)
/* A FAT16 volume is at most 4 GiB (64 KiB clusters), 2 GiB for Windows 95 */
#define SD_PART_FAT16_MAX (8u << 20)
/* Smaller than this is no disk worth having */
#define SD_PART_MIN 2048u

/* "sd:3" is 3; 0 if name is not "sd:" at all; -1 if it is "sd:" and not a
   partition number from 1 to 4 */
int sd_part_name(const char *name);

/* What the card's sectors 0 and the partition's first say about it */
struct sd_part_plan {
	int part;                       /* 1 to 4, as on the card */
	uint8_t type;                   /* its MBR type byte */
	uint32_t start, count;          /* where it is on the card, sectors */
	uint64_t disk_sectors;          /* the guest's disk: to the partition's
					   end, rounded up to a whole cylinder */
	bool fat;                       /* a FAT boot sector at its start */
	bool fat32;                     /* ... of FAT32 */
	uint32_t fs_sectors;            /* the volume's own size (BPB) */
	uint32_t hidden;                /* its hidden-sectors field */
	char label[12];                 /* its label, "" if none */
	char note[200];                 /* a warning that does not stop it */
};

/* Checks partition part of the card's MBR (mbr, 512 bytes) for the guest;
   vbr is the partition's first sector (NULL if it could not be read).
   0 and *plan filled, or -1 and why says why not in plain words.  Pure:
   the host tests call it with made-up sectors. */
int sd_part_plan(const uint8_t *mbr, const uint8_t *vbr, uint64_t card_sectors,
		 int part, struct sd_part_plan *plan, char *why, size_t size);

/* The guest's LBA 0 for a plan: the boot code (sd_part_mbr.s), the card's
   disk signature, the one entry, active, in slot 1, and 55AAh */
void sd_part_make_mbr(const struct sd_part_plan *plan, const uint8_t *card_mbr,
		      uint8_t *out);

/* An entry's CHS bytes for a sector, at the guest's 16 heads and 63
   sectors; 1023/15/63 past what CHS can reach */
void sd_part_chs(uint32_t lba, uint8_t out[3]);

/* What sd_part_open() would find, without opening it (esp_main.c's check
   of the ini, for its console lines): 0, or -1 and why. */
int sd_part_check(int part, struct sd_part_plan *plan, char *why, size_t size);

struct sd_part;

/* Opens partition part of the card for the guest: reads the card's MBR and
   the partition's first sector, checks them (sd_part_plan()), and refuses
   if tiny386's own FatFs has the partition mounted.  0 and *out, or -1 and
   why. */
int sd_part_open(int part, struct sd_part **out, char *why, size_t size);
const struct sd_part_plan *sd_part_get_plan(const struct sd_part *p);
/* the guest disk's size in sectors */
uint64_t sd_part_sectors(const struct sd_part *p);

#define SD_PART_CARD_ERROR (-1)         /* the card refused it, even tried again */
#define SD_PART_REFUSED (-2)            /* outside the partition: the guest's mistake */

/* n sectors at the guest's sector lba: 0, or one of the two above */
int sd_part_read(struct sd_part *p, uint64_t lba, void *buf, unsigned n);
int sd_part_write(struct sd_part *p, uint64_t lba, const void *buf, unsigned n);
/* the card's own cache, if it has one: FatFs's CTRL_SYNC */
int sd_part_sync(struct sd_part *p);
void sd_part_close(struct sd_part *p);

#endif /* SD_PART_H */
