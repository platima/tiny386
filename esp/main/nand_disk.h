/*
 * The ESP-Mosaico's SPI NAND (nand_disk.c): the guest's disk (nand_vol.h)
 * through ESP-IDF's spi_nand_flash and its Dhara map, and the chip raw for
 * USB disk mode's backup and restore.
 */
#ifndef NAND_DISK_H
#define NAND_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_blockdev.h"

enum nand_state {
	NAND_ABSENT,    /* the chip did not answer, or the map would not start */
	NAND_OURS,      /* formatted for tiny386: the guest's disk */
	NAND_OTHER,     /* something else on it (the factory files), or empty */
};

/* The SPI bus and the map, once; what is on the chip.  why says what went
   wrong for NAND_ABSENT. */
enum nand_state nand_disk_start(char *why, size_t size);
/* the guest's disk in bytes (NAND_OURS), and the map's pages */
uint64_t nand_disk_bytes(void);
uint32_t nand_disk_map_pages(void);
/* the chip's bad blocks as the raw scan found them (nand_raw_bad_blocks()) */
int nand_raw_bad_blocks(void);

/* Every block erased (bad ones left as they are), then a new volume with
   the signature: everything on the chip is gone.  0, or -1. */
int nand_disk_format(char *why, size_t size);

/* USB disk mode (usb_disk.c): the guest's disk in 512-byte sectors, and
   the whole chip raw, 2,176 bytes a page (2,048 and the spare area), every
   page one after another: 142,606,336 bytes.  Written, the raw one takes a
   dump made from it back, a block at a time. */
esp_blockdev_handle_t nand_disk_blockdev(void);
/* The guest's disk's last writes out to the chip, if it is open */
void nand_disk_sync(void);
esp_blockdev_handle_t nand_raw_blockdev(bool writable);
/* raw: blocks written back so far, and of them skipped as bad on this
   chip; pages that had an ECC error when read */
void nand_raw_counts(uint32_t *blocks, uint32_t *skipped, uint32_t *ecc_errors);

#endif /* NAND_DISK_H */
