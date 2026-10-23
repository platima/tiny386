/*
 * The disk images on the card, read and written through FatFs itself
 * (disk_image.c): each with its clusters mapped once, so that a seek reads
 * no FAT; one card error answered by opening the image again; the guest's
 * sectors through one buffer in internal RAM that the SD host can reach by
 * DMA.  ide.c uses it on the board for every image on a card volume that
 * storage.c mounted, and C stdio for any other (/spiflash).
 */
#ifndef DISK_IMAGE_H
#define DISK_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct disk_image;

#define DISK_IMAGE_OPENED 0
#define DISK_IMAGE_NOT_HERE 1   /* not on a card volume: the caller uses stdio */
#define DISK_IMAGE_FAILED (-1)  /* on a card volume, and it would not open */

/* Opens path (a VFS path, /sdcard3/win95.img) for reading, and for writing
   too if writable.  On DISK_IMAGE_OPENED *out and *size (bytes) are set;
   on DISK_IMAGE_FAILED disk_image_error() says why.  The image is never
   made longer. */
int disk_image_open(const char *path, bool writable, struct disk_image **out,
		    uint64_t *size);
const char *disk_image_error(void);

/* n bytes at offset; 0, or -1 if the card refused it even after the image
   was opened again (or the range is past the image's end, which is the
   caller's mistake and not counted).  ide.c reads and writes whole
   sectors at sector boundaries. */
int disk_image_read(struct disk_image *d, uint64_t offset, void *buf, size_t n);
int disk_image_write(struct disk_image *d, uint64_t offset, const void *buf, size_t n);
/* FatFs's f_sync(): the directory entry; the data is on the card already */
int disk_image_sync(struct disk_image *d);
void disk_image_close(struct disk_image *d);

/* All images together, since the start. */
struct disk_image_stats {
	uint32_t reads, writes;         /* accesses */
	uint32_t errors;                /* accesses the card refused at first */
	uint32_t recovered;             /* of those, done once the image was opened again */
	uint32_t longest_us;            /* the longest access */
	uint32_t recent_us;             /* the longest since the last reset_recent */
	uint32_t over_tick;             /* accesses longer than a timer tick, 55 ms */
	/* for the information port (perfport.h), wrapping */
	uint32_t rsectors, wsectors;    /* 512-byte sectors read and written */
	uint32_t busy_us;               /* all the accesses' time together */
};
void disk_image_get_stats(struct disk_image_stats *s, bool reset_recent);

/* 1 (the default): each image's clusters mapped when it is opened.  0:
   not, so that every seek back walks the FAT from the image's first
   cluster, as through ESP-IDF's VFS ([esp] fast_seek = 0). */
extern int disk_image_fast_seek;

/* Where the lines about the images go (the console on the board);
   NULL: stderr. */
extern void (*disk_image_say)(bool warn, const char *line);

#endif /* DISK_IMAGE_H */
