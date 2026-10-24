/*
 * The disk images on the card, read and written through FatFs itself
 * rather than through C stdio and ESP-IDF's VFS (disk_image.h).
 *
 * Why (research\WIN95-PC-REPRO.md 7, WIN95-BAREMETAL-AUDIT.md 3.4 and 3.5):
 *
 * - Without fast seek, FatFs follows a file's cluster chain from its first
 *   cluster on every seek to an earlier cluster, one card read for each
 *   sector of the FAT it crosses: up to hundreds of reads for one guest
 *   sector with the image in one piece, thousands with it in pieces, and
 *   every such wait holds the emulator up.  ESP-IDF's VFS makes FatFs's
 *   cluster map (fast seek) only for a file opened read only.  FatFs itself
 *   allows it for a file open for writing too, as long as the file is not
 *   made longer, which nothing here does (ide.c refuses every sector past
 *   the end, and so does this file).  So each image is opened with
 *   f_open(), and its map made once.
 * - After one card error FatFs refuses every later read, write and seek of
 *   that open file (fp->err), until it is closed.  Through stdio it never
 *   was, so one CRC error or timeout cost the guest its disk until RESET.
 *   Here a refused access closes the image, opens it again (with its map)
 *   and is tried once more.
 * - The guest's sector buffer sits inside the IDE state, never at the
 *   64-byte alignment the SD host's DMA wants in PSRAM, so ESP-IDF's driver
 *   took and freed a bounce buffer for every sector.  Here one buffer in
 *   internal RAM, taken once, is used for every access.
 *
 * Plain C over FatFs's API, so that the host tests (tests/host/disk) and
 * the PC harness run this very file on ESP-IDF's own ff.c.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "esp_heap_caps.h"
#include "disk_image.h"

uint32_t get_uticks();
/* storage.c: a VFS path on a mounted card volume as FatFs's own ("1:/x") */
bool storage_fatfs_path(const char *path, char *out, size_t size);

int disk_image_fast_seek = 1;
void (*disk_image_say)(bool warn, const char *line);

#define DI_MAX 6                        /* four IDE drives and two to spare */
#define DI_BUF_SECTORS 8                /* the internal buffer: 4 KiB */
#define DI_MAP_FIRST 256                /* map words tried first: 127 pieces */
#define DI_MANY_PIECES 64               /* more than this: said in yellow */
#define DI_MAP_INTERNAL_MAX 4096        /* bytes: a bigger map goes to PSRAM */
#define DI_TICK_US 54925                /* the PIT's 18.2 Hz tick */
#if FF_USE_FASTSEEK                     /* CONFIG_FATFS_USE_FASTSEEK */
#define DI_FASTSEEK 1
#else
#define DI_FASTSEEK 0
#endif

struct disk_image {
	FIL fil;
	bool used, writable;
	uint64_t size;
	char fpath[200];                /* FatFs's path, "1:/win95.img" */
	char name[200];                 /* the VFS path, for the lines */
	DWORD *map;                     /* FatFs's cluster map, or NULL */
	UINT map_words;
	unsigned pieces;
	unsigned said;                  /* error lines said about this image */
};

static struct disk_image s_img[DI_MAX];
static uint8_t *s_buf;                  /* DI_BUF_SECTORS sectors, internal RAM */
static struct disk_image_stats s_stats;
static char s_error[96];

/* only the emulator's task opens and reads the images, so one line buffer
   will do, and it stays off that task's 4 KiB stack */
static void say(bool warn, const char *fmt, ...)
{
	static char line[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (disk_image_say)
		disk_image_say(warn, line);
	else
		fprintf(stderr, "%s\n", line);
}

static const char *fr_words(FRESULT r)
{
	switch (r) {
	case FR_OK: return "no error";
	case FR_DISK_ERR: return "the card refused a read or a write";
	case FR_INT_ERR: return "FatFs found the file system in a state it cannot use";
	case FR_NOT_READY: return "the card is not ready";
	case FR_NO_FILE: return "there is no such file";
	case FR_NO_PATH: return "there is no such folder";
	case FR_INVALID_NAME: return "the name is not one FAT can hold";
	case FR_DENIED: return "access denied";
	case FR_WRITE_PROTECTED: return "the card is write-protected";
	case FR_INVALID_DRIVE: return "no such drive";
	case FR_NOT_ENABLED: return "the volume is not mounted";
	case FR_NO_FILESYSTEM: return "no FAT file system on the volume";
	case FR_TIMEOUT: return "FatFs's lock timed out";
	case FR_NOT_ENOUGH_CORE: return "out of memory";
	case FR_TOO_MANY_OPEN_FILES: return "too many open files";
	default: return "a FatFs error";
	}
}

const char *disk_image_error(void)
{
	return s_error;
}

static void *map_alloc(size_t bytes)
{
	void *p = NULL;
	if (bytes <= DI_MAP_INTERNAL_MAX)
		p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (!p)
		p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!p && bytes > DI_MAP_INTERNAL_MAX)
		p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	return p;
}

/* FatFs's cluster map for an open image: d->map, or NULL if there is no
   room for it (FatFs then follows the FAT, as before) */
static FRESULT make_map(struct disk_image *d)
{
#if FF_USE_FASTSEEK
	if (!d->map) {
		UINT words = DI_MAP_FIRST;
		for (int tries = 0; tries < 2; tries++) {
			DWORD *map = map_alloc(words * sizeof(DWORD));
			if (!map)
				return FR_NOT_ENOUGH_CORE;
			map[0] = words;
			d->fil.cltbl = map;
			FRESULT r = f_lseek(&d->fil, CREATE_LINKMAP);
			if (r == FR_OK) {
				d->map = map;
				d->map_words = words;
				d->pieces = (map[0] - 2) / 2;
				return FR_OK;
			}
			d->fil.cltbl = NULL;
			UINT need = map[0];
			heap_caps_free(map);
			if (r != FR_NOT_ENOUGH_CORE)
				return r;
			d->pieces = (need - 2) / 2;
			words = need;
		}
		return FR_NOT_ENOUGH_CORE;
	}
	/* the image opened again: the same file, the same map */
	d->map[0] = d->map_words;
	d->fil.cltbl = d->map;
	FRESULT r = f_lseek(&d->fil, CREATE_LINKMAP);
	if (r != FR_OK)
		d->fil.cltbl = NULL;
	return r;
#else
	(void) d;
	return FR_NOT_ENABLED;
#endif
}

static FRESULT open_fil(struct disk_image *d)
{
	/* zeroed: with FF_USE_DYN_BUFFER f_open() takes a sector buffer of
	   its own only if fp->buf is NULL */
	memset(&d->fil, 0, sizeof(d->fil));
	return f_open(&d->fil, d->fpath, FA_READ | (d->writable ? FA_WRITE : 0));
}

/* after an error: the file closed (FatFs's error flag goes with it) and
   opened again, with its map */
static FRESULT reopen(struct disk_image *d)
{
	f_close(&d->fil);       /* frees the sector buffer; its result does not matter */
	FRESULT r = open_fil(d);
	if (r != FR_OK)
		return r;
	if (f_size(&d->fil) != d->size) {
		f_close(&d->fil);
		memset(&d->fil, 0, sizeof(d->fil));
		return FR_INT_ERR;
	}
	if (d->map)
		make_map(d);    /* without it, slower but right */
	return FR_OK;
}

int disk_image_open(const char *path, bool writable, struct disk_image **out,
		    uint64_t *size)
{
	char fpath[sizeof(s_img[0].fpath)];
	s_error[0] = 0;
	if (!storage_fatfs_path(path, fpath, sizeof(fpath)))
		return DISK_IMAGE_NOT_HERE;
	struct disk_image *d = NULL;
	for (int i = 0; i < DI_MAX && !d; i++)
		if (!s_img[i].used)
			d = &s_img[i];
	if (!d)
		return DISK_IMAGE_NOT_HERE;
	memset(d, 0, sizeof(*d));
	snprintf(d->fpath, sizeof(d->fpath), "%s", fpath);
	snprintf(d->name, sizeof(d->name), "%s", path);
	d->writable = writable;
	FRESULT r = open_fil(d);
	if (r != FR_OK) {
		snprintf(s_error, sizeof(s_error), "%s", fr_words(r));
		return DISK_IMAGE_FAILED;
	}
	d->size = f_size(&d->fil);
	d->used = true;
	if (!s_buf)     /* if there is no room, the guest's buffer goes as before */
		s_buf = heap_caps_aligned_alloc(64, DI_BUF_SECTORS * 512,
						MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	if (!DI_FASTSEEK) {
		say(false, "%s: not mapped (this build's FatFs has no fast seek): every "
		    "seek back reads the FAT from the image's start.", d->name);
	} else if (disk_image_fast_seek) {
		uint32_t t0 = get_uticks();
		r = make_map(d);
		unsigned ms = (unsigned) ((get_uticks() - t0) / 1000);
		if (r == FR_OK && d->pieces <= DI_MANY_PIECES)
			say(false, "%s: %u piece%s on the card, mapped in %u ms: a seek "
			    "reads no FAT.", d->name, d->pieces, d->pieces == 1 ? "" : "s", ms);
		else if (r == FR_OK)
			say(true, "%s is in %u pieces on the card (mapped in %u ms, so a "
			    "seek reads no FAT). Copied onto a freshly formatted partition "
			    "it would be in one, and start faster.", d->name, d->pieces, ms);
		else if (r == FR_NOT_ENOUGH_CORE)
			say(true, "%s is in %u pieces on the card, too many to map: every "
			    "seek back reads the FAT from the image's start. Copied onto a "
			    "freshly formatted partition it would be in one piece.",
			    d->name, d->pieces);
		else
			say(true, "%s could not be mapped (%s): every seek back reads "
			    "the FAT from the image's start.", d->name, fr_words(r));
	} else {
		say(false, "%s: not mapped (fast_seek = 0): every seek back reads the "
		    "FAT from the image's start.", d->name);
	}
	*out = d;
	*size = d->size;
	return DISK_IMAGE_OPENED;
}

/* one try: n bytes at offset, through the internal buffer */
static FRESULT rw_once(struct disk_image *d, uint64_t off, uint8_t *buf, size_t n,
		       bool write)
{
	while (n > 0) {
		size_t len = n;
		uint8_t *p = buf;
		if (s_buf) {
			if (len > DI_BUF_SECTORS * 512)
				len = DI_BUF_SECTORS * 512;
			p = s_buf;
		}
		FRESULT r = FR_OK;
		if (f_tell(&d->fil) != (FSIZE_t) off)
			r = f_lseek(&d->fil, (FSIZE_t) off);
		if (r != FR_OK)
			return r;
		if (f_tell(&d->fil) != (FSIZE_t) off)
			return FR_INT_ERR;
		UINT done = 0;
		if (write) {
			if (p != buf)
				memcpy(p, buf, len);
			r = f_write(&d->fil, p, (UINT) len, &done);
		} else {
			r = f_read(&d->fil, p, (UINT) len, &done);
			if (r == FR_OK && done == len && p != buf)
				memcpy(buf, p, len);
		}
		if (r != FR_OK)
			return r;
		if (done != len)
			return FR_INT_ERR;
		off += len;
		buf += len;
		n -= len;
	}
	return FR_OK;
}

/* one access in the figures: its time, its sectors */
static void count_access(bool write, uint32_t sectors, uint32_t dt)
{
	if (dt > s_stats.longest_us)
		s_stats.longest_us = dt;
	if (dt > s_stats.recent_us)
		s_stats.recent_us = dt;
	if (dt > DI_TICK_US)
		s_stats.over_tick++;
	s_stats.busy_us += dt;
	if (write) {
		s_stats.writes++;
		s_stats.wsectors += sectors;
	} else {
		s_stats.reads++;
		s_stats.rsectors += sectors;
	}
}

void disk_image_count(bool write, uint32_t sectors, uint32_t us, bool error,
		      bool recovered)
{
	if (error)
		s_stats.errors++;
	if (recovered)
		s_stats.recovered++;
	count_access(write, sectors, us);
}

static int access_image(struct disk_image *d, uint64_t off, uint8_t *buf, size_t n,
			bool write)
{
	if (!d || !d->used || off > d->size || n > d->size - off)
		return -1;
	uint32_t t0 = get_uticks();
	int ret = 0;
	FRESULT r = rw_once(d, off, buf, n, write);
	if (r != FR_OK) {
		s_stats.errors++;
		FRESULT r2 = reopen(d);
		if (r2 == FR_OK)
			r2 = rw_once(d, off, buf, n, write);
		if (r2 == FR_OK)
			s_stats.recovered++;
		else
			ret = -1;
		/* the first few, then every hundredth */
		if (d->said < 4 || !(d->said % 100))
			say(true, "%s: a %s at byte %llu failed (%s); opened again, the "
			    "%s %s.", d->name, write ? "write" : "read",
			    (unsigned long long) off, fr_words(r), write ? "write" : "read",
			    ret ? "failed again: the guest is told" : "then worked");
		d->said++;
	}
	count_access(write, (uint32_t) (n / 512), get_uticks() - t0);
	return ret;
}

int disk_image_read(struct disk_image *d, uint64_t offset, void *buf, size_t n)
{
	return access_image(d, offset, buf, n, false);
}

int disk_image_write(struct disk_image *d, uint64_t offset, const void *buf, size_t n)
{
	if (!d || !d->writable)
		return -1;
	return access_image(d, offset, (uint8_t *) buf, n, true);
}

int disk_image_sync(struct disk_image *d)
{
	if (!d || !d->used)
		return -1;
	if (f_sync(&d->fil) == FR_OK)
		return 0;
	s_stats.errors++;
	if (reopen(d) == FR_OK) {
		/* the data went to the card with each write; only the directory
		   entry's time was still to go */
		s_stats.recovered++;
		return 0;
	}
	return -1;
}

void disk_image_close(struct disk_image *d)
{
	if (!d || !d->used)
		return;
	f_close(&d->fil);
	if (d->map)
		heap_caps_free(d->map);
	memset(d, 0, sizeof(*d));
}

void disk_image_get_stats(struct disk_image_stats *s, bool reset_recent)
{
	*s = s_stats;
	if (reset_recent)
		s_stats.recent_us = 0;
}
