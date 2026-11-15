/*
 * The ESP-Mosaico's NAND disk layer (esp/main/nand_vol.c) against pages in
 * memory: the signature, sectors on pages through the kept page, zero
 * pages trimmed, the disk's bounds, what a sync keeps over a reopen (a
 * power loss after it), and a store that fails.
 *
 *   bash tests/host/nand/run_nand.sh [OUT]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "nand_vol.h"

#define PAGES 61                        /* the store's logical pages */
#define DISK_PAGES (PAGES - 1)
#define DISK_SECTORS (DISK_PAGES * NAND_VOL_SPP)

static uint8_t store[PAGES][NAND_VOL_PAGE];
static uint8_t mapped[PAGES];
static int writes, trims, syncs, fail_read_at = -1, fail_write_at = -1, ops;
static int sig_writes;

static int st_read(void *c, uint32_t p, uint8_t *b)
{
	(void) c;
	if (p >= PAGES || ops++ == fail_read_at)
		return -1;
	if (mapped[p])
		memcpy(b, store[p], NAND_VOL_PAGE);
	else
		memset(b, 0, NAND_VOL_PAGE);
	return 0;
}

static int st_write(void *c, uint32_t p, const uint8_t *b)
{
	(void) c;
	if (p >= PAGES || ops++ == fail_write_at)
		return -1;
	if (p == PAGES - 1)
		sig_writes++;
	memcpy(store[p], b, NAND_VOL_PAGE);
	mapped[p] = 1;
	writes++;
	return 0;
}

static int st_trim(void *c, uint32_t p)
{
	(void) c;
	if (p >= PAGES)
		return -1;
	mapped[p] = 0;
	trims++;
	return 0;
}

static int st_sync(void *c)
{
	(void) c;
	syncs++;
	return 0;
}

static const struct nand_pages P = {
	.pages = PAGES, .read = st_read, .write = st_write, .trim = st_trim, .sync = st_sync,
};

static uint8_t model[DISK_SECTORS * 512];
static uint8_t synced[DISK_SECTORS * 512];      /* the model at the last sync */
static uint8_t buf[64 * 512], page[NAND_VOL_PAGE];
static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static void fill(uint8_t *b, unsigned n, unsigned seed, int kind)
{
	for (unsigned i = 0; i < n; i++)
		b[i] = kind == 0 ? 0 : kind == 1 ? 0xFF : (uint8_t) (seed * 131 + i * 7 + (i >> 9));
}

int main(void)
{
	struct nand_vol v;
	srand(386);

	/* 1. an empty store is not ours; nothing is written by looking */
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_NOT_OURS, "empty store taken as ours");
	CHECK(writes == 0 && trims == 0, "open wrote to the store");
	/* a store with someone else's last page is not ours either */
	memset(store[PAGES - 1], 0x5A, NAND_VOL_PAGE);
	mapped[PAGES - 1] = 1;
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_NOT_OURS, "foreign page taken as ours");
	mapped[PAGES - 1] = 0;

	/* 2. formatted: the signature, the disk's size */
	CHECK(nand_vol_format(&v, &P, page) == 0, "format failed");
	CHECK(nand_vol_sectors(&v) == DISK_SECTORS, "disk %llu sectors, not %d",
	      (unsigned long long) nand_vol_sectors(&v), DISK_SECTORS);
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_OK, "formatted store not ours");
	CHECK(nand_vol_sectors(&v) == DISK_SECTORS, "reopened size wrong");
	/* a damaged signature (the inverted copy) is refused */
	store[PAGES - 1][24] ^= 1;
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_NOT_OURS, "damaged signature taken");
	store[PAGES - 1][24] ^= 1;
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_OK, "signature not back");
	int sig0 = sig_writes;

	/* 3. a never written disk reads as zeros */
	CHECK(nand_vol_read(&v, 0, buf, 64) == 0, "read failed");
	for (int i = 0; i < 64 * 512; i++)
		if (buf[i]) {
			CHECK(0, "fresh disk byte %d is %02x", i, buf[i]);
			break;
		}

	/* 4. bounds */
	CHECK(nand_vol_read(&v, DISK_SECTORS, buf, 1) != 0, "read past the end allowed");
	CHECK(nand_vol_write(&v, DISK_SECTORS - 1, buf, 2) != 0, "write past the end allowed");
	CHECK(nand_vol_read(&v, (uint64_t) -1, buf, 2) != 0, "wrapping read allowed");
	CHECK(nand_vol_read(&v, DISK_SECTORS - 1, buf, 1) == 0, "last sector unreadable");
	CHECK(nand_vol_read(&v, 5, buf, 0) == 0, "empty read refused");

	/* 5. random reads and writes against the model, with syncs, and
	   reopens after a sync (the kept page gone, as after a power loss) */
	for (int it = 0; it < 20000; it++) {
		unsigned n = 1 + rand() % 64;
		if (rand() % 4 == 0)
			n = 1 + rand() % 4;
		unsigned lba = rand() % (DISK_SECTORS - n + 1);
		int what = rand() % 10;
		if (what < 5) {
			int kind = rand() % 6 == 0 ? 0 : rand() % 8 == 0 ? 1 : 2;
			fill(buf, n * 512, it, kind);
			CHECK(nand_vol_write(&v, lba, buf, n) == 0, "write %u+%u failed", lba, n);
			memcpy(model + lba * 512, buf, n * 512);
		} else if (what < 9) {
			CHECK(nand_vol_read(&v, lba, buf, n) == 0, "read %u+%u failed", lba, n);
			if (memcmp(buf, model + lba * 512, n * 512)) {
				CHECK(0, "read %u+%u differs from the model (step %d)", lba, n, it);
				break;
			}
		} else if (rand() % 3) {
			CHECK(nand_vol_sync(&v) == 0, "sync failed");
			memcpy(synced, model, sizeof(model));
		} else {
			/* power lost: whatever the kept page held, unsynced, may be
			   gone; after a sync nothing is */
			CHECK(nand_vol_sync(&v) == 0, "sync failed");
			memcpy(synced, model, sizeof(model));
			CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_OK, "reopen failed");
			for (unsigned s = 0; s < DISK_SECTORS; s += 64) {
				unsigned k = DISK_SECTORS - s < 64 ? DISK_SECTORS - s : 64;
				CHECK(nand_vol_read(&v, s, buf, k) == 0, "read after reopen failed");
				if (memcmp(buf, synced + s * 512, k * 512)) {
					CHECK(0, "after reopen sectors %u+64 differ (step %d)", s, it);
					it = 1 << 30;
					break;
				}
			}
		}
	}
	CHECK(sig_writes == sig0, "the signature page was written %d times by the guest",
	      sig_writes - sig0);
	CHECK(nand_vol_open(&v, &P, page) == NAND_VOL_OK, "signature lost");

	/* 6. zero pages are trimmed and unmapped; a whole page of FFh is not */
	CHECK(nand_vol_sync(&v) == 0, "sync failed");
	memset(buf, 0, 4 * 512);
	int t0 = trims;
	CHECK(nand_vol_write(&v, 8, buf, 4) == 0, "zero write failed");
	CHECK(trims == t0 + 1 && !mapped[2], "a whole zero page was not trimmed");
	memset(buf, 0xFF, 4 * 512);
	CHECK(nand_vol_write(&v, 12, buf, 4) == 0, "FF write failed");
	CHECK(mapped[3], "a whole FFh page was trimmed");
	/* zeros into part of a page that becomes all zero: trimmed at sync */
	memset(buf, 0, 512);
	for (int s = 12; s < 16; s++)
		CHECK(nand_vol_write(&v, s, buf, 1) == 0, "zero sector write failed");
	t0 = trims;
	CHECK(nand_vol_sync(&v) == 0, "sync failed");
	CHECK(trims == t0 + 1 && !mapped[3], "a page made zero by sectors was not trimmed");
	CHECK(nand_vol_read(&v, 12, buf, 4) == 0, "read failed");
	for (int i = 0; i < 4 * 512; i++)
		if (buf[i]) {
			CHECK(0, "trimmed page reads %02x", buf[i]);
			break;
		}

	/* 7. sectors written one by one into a page cost one page write */
	int w0 = writes;
	fill(buf, 512, 7, 2);
	for (int s = 20; s < 24; s++)
		CHECK(nand_vol_write(&v, s, buf, 1) == 0, "sector write failed");
	CHECK(writes == w0, "a part page was written before it was needed");
	CHECK(nand_vol_sync(&v) == 0, "sync failed");
	CHECK(writes == w0 + 1, "four sectors of a page made %d page writes", writes - w0);

	/* 8. a store that fails: an error, and the data as it was */
	CHECK(nand_vol_sync(&v) == 0, "sync failed");
	fill(buf, 512, 9, 2);
	ops = 0;
	fail_read_at = 0;
	CHECK(nand_vol_write(&v, 25, buf, 1) != 0, "a failed read of a part page passed");
	fail_read_at = -1;
	ops = 0;
	fail_write_at = 0;
	fill(buf, 4 * 512, 10, 2);
	CHECK(nand_vol_write(&v, 28, buf, 4) != 0, "a failed page write passed");
	fail_write_at = -1;
	CHECK(nand_vol_read(&v, 28, buf, 4) == 0, "read after a failed write failed");

	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("nand_vol: signature, bounds, 20000 random steps with syncs and reopens, "
	       "trims, kept page, store errors: all as expected (%d page writes, %d trims, "
	       "%d syncs)\n", writes, trims, syncs);
	return 0;
}
