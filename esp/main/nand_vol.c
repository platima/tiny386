/*
 * The guest's disk on the ESP-Mosaico's NAND: see nand_vol.h.  Plain C.
 */
#include <string.h>
#include "nand_vol.h"

#define NO_PAGE UINT32_MAX

/* the signature page: magic, version, disk pages, the same again inverted */
static const char s_magic[16] = "TINY386 NAND\0\0\0";
#define SIG_VERSION 1

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8);
	p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
	return p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static bool all_zero(const uint8_t *b)
{
	const uint32_t *w = (const uint32_t *) (const void *) b;
	uint32_t acc = 0;
	for (int i = 0; i < NAND_VOL_PAGE / 4; i++)
		acc |= w[i];
	return acc == 0;
}

int nand_vol_open(struct nand_vol *v, const struct nand_pages *p, uint8_t *buf)
{
	memset(v, 0, sizeof(*v));
	v->p = p;
	v->page = buf;
	v->page_no = NO_PAGE;
	if (p->pages < 2)
		return NAND_VOL_ERROR;
	if (p->read(p->ctx, p->pages - 1, buf) != 0)
		return NAND_VOL_ERROR;
	if (memcmp(buf, s_magic, sizeof(s_magic)) != 0 || get32(buf + 16) != SIG_VERSION)
		return NAND_VOL_NOT_OURS;
	uint32_t pages = get32(buf + 20);
	if (get32(buf + 24) != ~pages || pages == 0 || pages > p->pages - 1)
		return NAND_VOL_NOT_OURS;
	v->disk_pages = pages;
	return NAND_VOL_OK;
}

int nand_vol_format(struct nand_vol *v, const struct nand_pages *p, uint8_t *buf)
{
	memset(v, 0, sizeof(*v));
	v->p = p;
	v->page = buf;
	v->page_no = NO_PAGE;
	if (p->pages < 2)
		return -1;
	memset(buf, 0, NAND_VOL_PAGE);
	memcpy(buf, s_magic, sizeof(s_magic));
	put32(buf + 16, SIG_VERSION);
	put32(buf + 20, p->pages - 1);
	put32(buf + 24, ~(p->pages - 1));
	if (p->write(p->ctx, p->pages - 1, buf) != 0 || p->sync(p->ctx) != 0)
		return -1;
	v->disk_pages = p->pages - 1;
	return 0;
}

uint64_t nand_vol_sectors(const struct nand_vol *v)
{
	return (uint64_t) v->disk_pages * NAND_VOL_SPP;
}

/* a whole page to the store: trimmed if it is all zeros */
static int put_page(struct nand_vol *v, uint32_t page, const uint8_t *b)
{
	int r;
	if (all_zero(b)) {
		r = v->p->trim(v->p->ctx, page);
		v->st.trims++;
	} else {
		r = v->p->write(v->p->ctx, page, b);
		v->st.page_writes++;
	}
	if (r)
		v->st.errors++;
	return r ? -1 : 0;
}

static int write_back(struct nand_vol *v)
{
	if (!v->dirty)
		return 0;
	if (put_page(v, v->page_no, v->page) != 0)
		return -1;
	v->dirty = false;
	return 0;
}

/* page into the kept one (written back first if it held another) */
static int keep(struct nand_vol *v, uint32_t page)
{
	if (v->page_no == page)
		return 0;
	if (write_back(v) != 0)
		return -1;
	v->page_no = NO_PAGE;
	v->st.page_reads++;
	if (v->p->read(v->p->ctx, page, v->page) != 0) {
		v->st.errors++;
		return -1;
	}
	v->page_no = page;
	return 0;
}

static bool in_disk(const struct nand_vol *v, uint64_t lba, unsigned n)
{
	uint64_t total = nand_vol_sectors(v);
	return lba <= total && n <= total - lba;
}

int nand_vol_read(struct nand_vol *v, uint64_t lba, void *buf, unsigned n)
{
	uint8_t *out = buf;
	if (!in_disk(v, lba, n))
		return -1;
	while (n) {
		uint32_t page = (uint32_t) (lba / NAND_VOL_SPP);
		unsigned off = (unsigned) (lba % NAND_VOL_SPP);
		unsigned k = NAND_VOL_SPP - off;
		if (k > n)
			k = n;
		if (off == 0 && k == NAND_VOL_SPP && v->page_no != page) {
			/* a whole page straight into the caller's buffer */
			v->st.page_reads++;
			if (v->p->read(v->p->ctx, page, out) != 0) {
				v->st.errors++;
				return -1;
			}
		} else {
			if (keep(v, page) != 0)
				return -1;
			memcpy(out, v->page + off * 512, k * 512);
		}
		out += k * 512;
		lba += k;
		n -= k;
	}
	return 0;
}

int nand_vol_write(struct nand_vol *v, uint64_t lba, const void *buf, unsigned n)
{
	const uint8_t *in = buf;
	if (!in_disk(v, lba, n))
		return -1;
	while (n) {
		uint32_t page = (uint32_t) (lba / NAND_VOL_SPP);
		unsigned off = (unsigned) (lba % NAND_VOL_SPP);
		unsigned k = NAND_VOL_SPP - off;
		if (k > n)
			k = n;
		if (off == 0 && k == NAND_VOL_SPP) {
			/* the whole page: the kept copy, if it is this page,
			   is out of date and goes */
			if (v->page_no == page) {
				v->page_no = NO_PAGE;
				v->dirty = false;
			}
			if (put_page(v, page, in) != 0)
				return -1;
		} else {
			if (keep(v, page) != 0)
				return -1;
			memcpy(v->page + off * 512, in, k * 512);
			v->dirty = true;
			v->st.rmw++;
		}
		in += k * 512;
		lba += k;
		n -= k;
	}
	return 0;
}

int nand_vol_sync(struct nand_vol *v)
{
	if (write_back(v) != 0)
		return -1;
	return v->p->sync(v->p->ctx) ? -1 : 0;
}
