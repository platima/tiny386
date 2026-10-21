/*
 * The card (and, on boards without one, the flash's FAT partition).
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Bit 0: the card is mounted at /sdcard; bit 1: the flash's storage
   partition at /spiflash (tried only without a card). */
int storage_init(void);
/* The card again (after a failure); true once it is mounted. */
bool storage_sd_mount(void);

/* A partition of the card's MBR. */
struct storage_volume {
	int part;               /* 1 to 4 in the MBR; 0: a card without one */
	uint8_t type;           /* the MBR's type byte */
	uint32_t mib;
	bool fat;               /* a FAT boot sector is there */
	bool mounted;
	char path[12];          /* "/sdcard", "/sdcard3"; "" if not mounted */
	char label[16];         /* the volume label, "WIN9X" */
	const char *why;        /* why a FAT one is not mounted */
};

/* After the card is mounted: every other FAT partition of its MBR at
   /sdcardN (N its MBR number).  Returns how many partitions there are. */
int storage_sd_mount_others(void);
const struct storage_volume *storage_volumes(int *n);
/* The partition the Linux route mounts at /mnt/win: labelled WIN9X, else
   the first FAT partition other than /sdcard.  NULL if there is none. */
const char *storage_win_path(void);

#endif /* STORAGE_H */
