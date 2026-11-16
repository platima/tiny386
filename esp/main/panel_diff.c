/*
 * panel_diff.c: see panel_diff.h.
 */
#include <string.h>

#include "panel_diff.h"

void panel_diff_init(struct panel_diff *pd, int w, int h, uint32_t *hash,
		     uint8_t *known)
{
	pd->w = w;
	pd->h = h;
	pd->blocks = w / PD_BLOCK;
	pd->hash = hash;
	pd->known = known;
	panel_diff_forget(pd);
}

void panel_diff_forget(struct panel_diff *pd)
{
	memset(pd->known, 0, (size_t) pd->h);
}

void panel_diff_forget_rows(struct panel_diff *pd, int r0, int n)
{
	if (r0 < 0) {
		n += r0;
		r0 = 0;
	}
	if (r0 + n > pd->h)
		n = pd->h - r0;
	if (n > 0)
		memset(pd->known + r0, 0, (size_t) n);
}

/* FNV-1a over the block's 32-bit words (PD_BLOCK / 2 of them), in two
   lanes with seeds of their own, the even words and the odd, so that each
   multiply need not wait for the one before (14% less time than one lane
   on the PC).  Each step is one to one for a given word, so a change to
   one word always changes the hash; the seeds keep words that trade lanes
   from cancelling out. */
static uint32_t block_hash(const uint16_t *p)
{
	const uint32_t *w = (const uint32_t *) p;
	uint32_t a = 2166136261u, b = 0x050C5D1Fu;
	for (int i = 0; i < PD_BLOCK / 2; i += 2) {
		a ^= w[i];
		b ^= w[i + 1];
		a *= 16777619u;
		b *= 16777619u;
	}
	return a ^ ((b << 15) | (b >> 17));
}

bool panel_diff_rows(struct panel_diff *pd, const uint16_t *buf, int stride,
		     int r0, int n, bool force, int *ya, int *yb, int *x0, int *x1)
{
	int b0 = pd->blocks, b1 = 0;    /* blocks b0 to b1 - 1 changed */
	int first = -1, last = -1;      /* rows of the piece that changed */
	for (int r = 0; r < n; r++) {
		int row = r0 + r;
		uint32_t *hs = pd->hash + (size_t) row * pd->blocks;
		bool all = force || !pd->known[row];
		const uint16_t *p = buf + (size_t) r * stride;
		bool changed = false;
		for (int b = 0; b < pd->blocks; b++, p += PD_BLOCK) {
			uint32_t h = block_hash(p);
			if (all || h != hs[b]) {
				hs[b] = h;
				changed = true;
				if (b < b0)
					b0 = b;
				if (b + 1 > b1)
					b1 = b + 1;
			}
		}
		pd->known[row] = 1;
		if (changed) {
			if (first < 0)
				first = r;
			last = r;
		}
	}
	if (first < 0)
		return false;
	*ya = first;
	*yb = last + 1;
	*x0 = b0 * PD_BLOCK;
	*x1 = b1 * PD_BLOCK;
	return true;
}

size_t panel_diff_pack(uint16_t *buf, int w, int ya, int yb, int x0, int x1)
{
	int cw = x1 - x0;
	/* each row's place in the packed rectangle is at or before where it
	   is now, so going down the rows overwrites nothing still to move */
	for (int r = ya; r < yb; r++)
		memmove(buf + (size_t) (r - ya) * cw, buf + (size_t) r * w + x0,
			(size_t) cw * sizeof(uint16_t));
	return (size_t) (yb - ya) * cw;
}

size_t panel_diff_copy_swapped(uint16_t *dst, const uint16_t *src, int stride,
			       int ya, int yb, int x0, int x1)
{
	int words = (x1 - x0) / 2;
	uint32_t *d = (uint32_t *) dst;
	/* two pixels a word */
	for (int r = ya; r < yb; r++) {
		const uint32_t *s = (const uint32_t *) (src + (size_t) r * stride + x0);
		for (int i = 0; i < words; i++) {
			uint32_t v = s[i];
			*d++ = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
		}
	}
	return (size_t) (yb - ya) * (size_t) (x1 - x0);
}
