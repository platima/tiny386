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
};
extern struct host_card_stats host_card;

/* the FatFs drive ("1:") and filesystem object registered for a path */
const char *host_vfs_drive(const char *base_path);
FATFS *host_vfs_fs(const char *base_path);
