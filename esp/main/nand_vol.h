/*
 * The guest's disk on the ESP-Mosaico's NAND (nand_vol.c): 512-byte
 * sectors on the 2 KiB pages of a wear-levelled store (ESP-IDF's
 * spi_nand_flash with its Dhara map, nand_disk.c), plain C over the four
 * page calls below, so that the host tests run it against pages in memory
 * (tests/host/nand_vol).
 *
 * - The last page holds a signature: "TINY386 NAND", a version, and the
 *   disk's size in pages, set when the volume is formatted.  Without it the
 *   volume is someone else's (the factory files) and nand_vol_open() says
 *   so; nothing is written until nand_vol_format().
 * - A page that would be all zeros is trimmed (given back to the map), and
 *   a page never written reads as zeros, so the free space in a disk image
 *   costs the NAND nothing until the guest writes there.
 * - One page is kept: sectors written into part of a page wait there, and
 *   reach the NAND when another page is wanted, or at nand_vol_sync()
 *   (ide.c's flush, every 2 s while the guest writes).
 */
#ifndef NAND_VOL_H
#define NAND_VOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NAND_VOL_PAGE 2048
#define NAND_VOL_SPP (NAND_VOL_PAGE / 512)      /* sectors a page */

struct nand_pages {
	void *ctx;
	uint32_t pages;         /* logical pages the store has */
	/* 0, or -1 if the store failed; a page never written (or trimmed)
	   reads as zeros */
	int (*read)(void *ctx, uint32_t page, uint8_t *buf);
	int (*write)(void *ctx, uint32_t page, const uint8_t *buf);
	int (*trim)(void *ctx, uint32_t page);
	int (*sync)(void *ctx);
};

struct nand_vol_stats {
	uint32_t page_reads, page_writes, trims, rmw, errors;
};

struct nand_vol {
	const struct nand_pages *p;
	uint32_t disk_pages;    /* the guest's disk, pages */
	uint8_t *page;          /* the one page kept (NAND_VOL_PAGE bytes) */
	uint32_t page_no;       /* which, or UINT32_MAX */
	bool dirty;
	struct nand_vol_stats st;
};

#define NAND_VOL_OK 0
#define NAND_VOL_NOT_OURS 1     /* no signature: not formatted for tiny386 */
#define NAND_VOL_ERROR (-1)

/* buf: NAND_VOL_PAGE bytes the volume keeps its page in.  NAND_VOL_OK and
   v->disk_pages set, NAND_VOL_NOT_OURS, or NAND_VOL_ERROR (the store
   failed, or is too small). */
int nand_vol_open(struct nand_vol *v, const struct nand_pages *p, uint8_t *buf);
/* Writes the signature (the store should be empty: erased); the disk is
   every page but the signature's.  0 or -1. */
int nand_vol_format(struct nand_vol *v, const struct nand_pages *p, uint8_t *buf);

uint64_t nand_vol_sectors(const struct nand_vol *v);
/* n sectors at lba: 0, or -1 (outside the disk, or the store failed) */
int nand_vol_read(struct nand_vol *v, uint64_t lba, void *buf, unsigned n);
int nand_vol_write(struct nand_vol *v, uint64_t lba, const void *buf, unsigned n);
/* the kept page to the store, then the store's own sync */
int nand_vol_sync(struct nand_vol *v);

#endif /* NAND_VOL_H */
