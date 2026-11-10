/* host_glue.c: the card image's counters, and the paths the VFS stand-in
   registered */
#pragma once
#include <stddef.h>
#include "ff.h"

struct host_card_stats {
	unsigned reads, writes;
	/* sectors the test allows to be written: {start, count} */
	size_t allowed[4][2];
	int nallowed;
	unsigned outside;               /* sectors written outside them */
	size_t first_outside;
	/* a card that refuses (disk_image.c's test): read number fail_read_at
	   (write number fail_write_at) and the fail_n - 1 after it fail; 0:
	   none */
	unsigned fail_read_at, fail_write_at, fail_n;
	unsigned failed;                /* refused so far */
	/* reads that touch sectors watch_start to watch_start + watch_n - 1
	   (a volume's FAT) */
	size_t watch_start, watch_n;
	unsigned watch_reads;
	/* v5.3: host_dma_check(), ESP-IDF's buffer check as tests stand in
	   for it (a test puts it in the card's host.check_buffer_alignment):
	   a buffer is taken if both its ends are on 64-byte boundaries and it
	   overlaps none of the ranges refused here */
	struct {
		const void *start;
		size_t size;
	} dma_refused[4];
	int ndma_refused;
	/* sector reads and writes handed a buffer the check takes, and ones
	   handed a buffer it refuses (ESP-IDF's driver copies those through
	   internal RAM of its own); only with a check in the card */
	unsigned dma_direct, dma_copied;
};
extern struct host_card_stats host_card;

bool host_dma_check(int slot, const void *buf, size_t size);

/* the FatFs drive ("1:") and filesystem object registered for a path */
const char *host_vfs_drive(const char *base_path);
FATFS *host_vfs_fs(const char *base_path);
