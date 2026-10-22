/*
 * Host test of misc.c's floppy drive (the emulink device the BIOS's int 13h
 * uses) with the ESP-IDF port's checks (EMULINK_CHECKED, which BUILD_ESP32
 * turns on; here on its own, without the rest of BUILD_ESP32): boot safety
 * review, round 2, m1, and emulator correctness review, round 2, note 6.
 *
 *   a drive number of 2 or more is refused, not used as an index;
 *   a cylinder, head, sector and count past the image's end are refused,
 *     and the image file does not grow;
 *   reads and writes inside the image work as before;
 *   a write the medium refuses is an error for the guest, and counted
 *     (emulink_error_count(), which the console's card-error line adds in);
 *   each read and write that reaches the image moves the status LED's
 *     disk light count (emulink_activity, v3.8), and one refused before
 *     it does not.
 *
 *   test_floppy DIR      makes DIR/floppy.img (1.44 MB) and works on it
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "misc.h"

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

#define FLOPPY_BYTES (18 * 80 * 2 * 512)

static long file_size(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? (long) st.st_size : -1;
}

/* as the BIOS does it: the command, then drive, CHS and count */
static uint32_t start(EMULINK *e, uint32_t cmd, uint32_t drive, int c, int h, int s, int n)
{
	emulink_cmd_write(e, cmd);
	emulink_data_write(e, drive);
	emulink_data_write(e, ((uint32_t) c << 16) | ((uint32_t) h << 8) | (uint32_t) s);
	emulink_data_write(e, (uint32_t) n);
	return emulink_status_read(e);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[1024];
	static uint8_t buf[FLOPPY_BYTES];

	snprintf(path, sizeof(path), "%s/floppy.img", dir);
	FILE *f = fopen(path, "wb");
	for (long i = 0; i < FLOPPY_BYTES; i++)
		buf[i] = (uint8_t) (i / 512);
	fwrite(buf, 1, FLOPPY_BYTES, f);
	fclose(f);

	EMULINK *e = emulink_init();
	CHECK(emulink_attach_floppy(e, 0, path) == 0, "a 1.44 MB image attached as drive 0");

	/* drive 2: refused before fdd[2] is looked at */
	CHECK(start(e, 0x101, 2, 0, 0, 1, 1) == (uint32_t) -EIO, "read, drive 2: refused");
	CHECK(start(e, 0x102, 7, 0, 0, 1, 1) == (uint32_t) -EIO, "write, drive 7: refused");
	CHECK(emulink_data_write_string(e, buf, 2, 256) == 256 &&
	      emulink_status_read(e) == (uint32_t) -1, "data for a refused write: an error");

	/* past the end */
	CHECK(start(e, 0x101, 0, 80, 0, 1, 1) == (uint32_t) -EIO, "read, cylinder 80 of 80: refused");
	CHECK(start(e, 0x102, 0, 79, 1, 18, 2) == (uint32_t) -EIO,
	      "write, the last sector and one more: refused");
	CHECK(start(e, 0x102, 0, 200, 0, 1, 1) == (uint32_t) -EIO, "write, cylinder 200: refused");
	CHECK(emulink_data_write_string(e, buf, 2, 256) == 256 &&
	      emulink_status_read(e) == (uint32_t) -1, "its data: an error");
	CHECK(start(e, 0x101, 0, 0, 0, 0, 1) == (uint32_t) -EIO, "read, sector 0 (there is none): refused");
	CHECK(file_size(path) == FLOPPY_BYTES, "the image is still %d bytes (%ld)", FLOPPY_BYTES,
	      file_size(path));

	/* inside: as before */
	CHECK(start(e, 0x101, 0, 79, 1, 18, 1) == 0, "read, the last sector: accepted");
	memset(buf, 0, 512);
	CHECK(emulink_data_read_string(e, buf, 2, 256) == 256 && emulink_status_read(e) == 0 &&
	      buf[0] == (uint8_t) (2879 % 256), "its data: sector 2879 (%02X)", buf[0]);
	CHECK(start(e, 0x102, 0, 0, 0, 1, 2) == 0, "write, the first two sectors: accepted");
	memset(buf, 0xA5, 1024);
	unsigned w0 = emulink_write_count();
	CHECK(emulink_data_write_string(e, buf, 2, 512) == 512 && emulink_status_read(e) == 0 &&
	      emulink_write_count() == w0 + 1, "their data written");
	emulink_flush(e);
	CHECK(emulink_error_count() == 0, "no errors counted (%u)", emulink_error_count());
	f = fopen(path, "rb");
	size_t got = fread(buf, 1, 1024, f);
	fclose(f);
	CHECK(got == 1024 && buf[0] == 0xA5 && buf[1023] == 0xA5, "on the medium");
	CHECK(file_size(path) == FLOPPY_BYTES, "the image is still %d bytes (%ld)", FLOPPY_BYTES,
	      file_size(path));

	/* the status LED's disk light (v3.8): one more on the count for each
	   read and each write that reaches the image, none for one refused */
	unsigned a0 = emulink_activity;
	CHECK(start(e, 0x101, 0, 0, 0, 3, 1) == 0 && emulink_data_read_string(e, buf, 2, 256) == 256 &&
	      start(e, 0x102, 0, 0, 0, 3, 1) == 0 && emulink_data_write_string(e, buf, 2, 256) == 256 &&
	      emulink_activity == a0 + 2, "a read and a write: the disk light's count %u more (2 wanted)",
	      emulink_activity - a0);
	a0 = emulink_activity;
	CHECK(start(e, 0x101, 0, 80, 0, 1, 1) == (uint32_t) -EIO && emulink_activity == a0,
	      "a read refused before the image: the count unchanged (%u more)", emulink_activity - a0);

	/* A medium that refuses the write: /dev/full, which takes nothing.
	   emulink_attach_floppy() leaves a file whose size it does not know
	   open, in the first format (tiny386's own way): enough to write to. */
	if (access("/dev/full", W_OK) == 0) {
		CHECK(emulink_attach_floppy(e, 1, "/dev/full") != 0,
		      "/dev/full is no floppy size: the attach says so");
		unsigned e0 = emulink_error_count();
		CHECK(start(e, 0x102, 1, 0, 0, 1, 1) == 0, "a write to drive 1 (/dev/full) begins");
		CHECK(emulink_data_write_string(e, buf, 2, 256) == 256 &&
		      emulink_status_read(e) == (uint32_t) -1,
		      "its data, taken by the C library but refused at the flush: an error");
		CHECK(emulink_error_count() == e0 + 1, "and counted (%u, was %u)",
		      emulink_error_count(), e0);
	} else {
		printf("  (no /dev/full here: the refused write is not tried)\n");
	}

	printf("test_floppy: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
