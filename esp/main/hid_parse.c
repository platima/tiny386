/*
 * HID report descriptors, as far as keyboards and mice need: see
 * hid_parse.h.  Item format: HID 1.11, section 6.2.2.
 */
#include <string.h>

#include "hid_parse.h"

#define MAX_USAGES 16
#define MAX_IDS 16
#define STACK_DEPTH 4

#define PAGE_DESKTOP 0x01
#define PAGE_KEYBOARD 0x07
#define PAGE_BUTTON 0x09

struct globals {
	uint16_t page;
	int32_t lmin;
	uint32_t size, count;
	uint8_t id;
};

struct parser {
	struct globals g, stack[STACK_DEPTH];
	int sp;
	uint32_t usages[MAX_USAGES];   /* page << 16 | usage */
	int nusages;
	uint32_t umin, umax;
	bool has_min, has_max;
	/* where the next input field of each report starts, in bits */
	uint8_t ids[MAX_IDS];
	uint16_t offsets[MAX_IDS];
	int nids;
	uint16_t scratch;               /* for report IDs past MAX_IDS */
	bool kbd_seen, mouse_seen;      /* an ID is bound to each */
};

static uint32_t item_unsigned(const uint8_t *p, int n)
{
	uint32_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

static int32_t item_signed(const uint8_t *p, int n)
{
	uint32_t v = item_unsigned(p, n);
	if (n == 1)
		return (int8_t) v;
	if (n == 2)
		return (int16_t) v;
	return (int32_t) v;
}

static uint16_t *offset_of(struct parser *ps, uint8_t id)
{
	for (int i = 0; i < ps->nids; i++)
		if (ps->ids[i] == id)
			return &ps->offsets[i];
	if (ps->nids == MAX_IDS)
		return &ps->scratch;
	ps->ids[ps->nids] = id;
	ps->offsets[ps->nids] = 0;
	return &ps->offsets[ps->nids++];
}

static bool kbd_ok(struct parser *ps, struct hid_layout *l)
{
	if (!ps->kbd_seen) {
		ps->kbd_seen = true;
		l->kbd_id = ps->g.id;
	}
	return l->kbd_id == ps->g.id;
}

static bool mouse_ok(struct parser *ps, struct hid_layout *l)
{
	if (!ps->mouse_seen) {
		ps->mouse_seen = true;
		l->mouse_id = ps->g.id;
	}
	return l->mouse_id == ps->g.id;
}

static void set_field(struct hid_field *f, uint32_t bit, uint32_t size,
		      uint32_t count, bool sign)
{
	f->bit = bit;
	f->size = size;
	f->count = count;
	f->sign = sign;
}

/* the usage of the k-th field of a variable item, 0 if none was given */
static uint32_t usage_of(const struct parser *ps, uint32_t k)
{
	if (ps->nusages)
		return ps->usages[k < (uint32_t) ps->nusages ? k : (uint32_t) ps->nusages - 1];
	if (ps->has_min) {
		uint32_t u = ps->umin + k;
		if (ps->has_max && u > ps->umax)
			u = ps->umax;
		return u;
	}
	return 0;
}

static void input_item(struct parser *ps, struct hid_layout *l, uint32_t flags)
{
	const struct globals *g = &ps->g;
	uint16_t *next = offset_of(ps, g->id);
	uint32_t start = *next;
	*next += g->size * g->count;

	if ((flags & 1) || !g->size || !g->count)
		return;                 /* constant: padding */
	bool variable = flags & 2, relative = flags & 4;

	if (!variable) {
		/* an array: of keyboard usages, a byte each, is the key array */
		uint32_t first = ps->has_min ? ps->umin : ps->nusages ? ps->usages[0] :
			(uint32_t) g->page << 16;
		if ((first >> 16) == PAGE_KEYBOARD && g->size == 8 && !l->keys.size &&
		    kbd_ok(ps, l)) {
			set_field(&l->keys, start, 8, g->count, false);
			l->kbd = true;
		}
		return;
	}

	/* a run of keyboard bits: the modifiers (E0 to E7), or a bitmap of
	   keys as an N-key rollover keyboard sends */
	uint32_t u0 = usage_of(ps, 0);
	if ((u0 >> 16) == PAGE_KEYBOARD && g->size == 1) {
		uint32_t id = u0 & 0xffff;
		if (id == 0xE0 && g->count >= 8 && !l->mods.size && kbd_ok(ps, l)) {
			set_field(&l->mods, start, 1, 8, false);
			l->kbd = true;
		} else if (id < 0xE0 && ps->has_min && !l->bitmap.size && kbd_ok(ps, l)) {
			set_field(&l->bitmap, start, 1, g->count, false);
			l->bitmap_first = id;
			l->kbd = true;
		}
		return;
	}
	for (uint32_t k = 0; k < g->count; k++) {
		uint32_t u = usage_of(ps, k);
		uint32_t bit = start + k * g->size;
		uint16_t page = u >> 16, id = u & 0xffff;
		if (page == PAGE_BUTTON && g->size == 1 && id == 1 &&
		    !l->buttons.size && mouse_ok(ps, l)) {
			uint32_t n = g->count - k;
			set_field(&l->buttons, bit, 1, n > 8 ? 8 : n, false);
		} else if (page == PAGE_DESKTOP && relative && g->size <= 32) {
			struct hid_field *f = id == 0x30 ? &l->x : id == 0x31 ? &l->y :
				id == 0x38 ? &l->wheel : NULL;
			if (f && !f->size && mouse_ok(ps, l))
				set_field(f, bit, g->size, 1, g->lmin < 0);
		}
	}
	if (l->x.size && l->y.size)
		l->mouse = true;
}

bool hid_parse(const uint8_t *d, size_t len, struct hid_layout *out)
{
	static struct parser ps;        /* one caller at a time: the USB task */
	memset(&ps, 0, sizeof(ps));
	memset(out, 0, sizeof(*out));

	size_t i = 0;
	while (i < len) {
		uint8_t p = d[i++];
		if (p == 0xFE) {
			/* a long item: its size, its tag, its data; none of use */
			if (i + 2 > len)
				break;
			i += 2 + d[i];
			continue;
		}
		int n = p & 3;
		if (n == 3)
			n = 4;
		if (i + n > len)
			break;
		const uint8_t *v = d + i;
		i += n;
		uint32_t u = item_unsigned(v, n);
		int type = (p >> 2) & 3, tag = p >> 4;

		if (type == 1) {                /* global */
			switch (tag) {
			case 0x0: ps.g.page = u; break;
			case 0x1: ps.g.lmin = item_signed(v, n); break;
			case 0x7: ps.g.size = u; break;
			case 0x8: ps.g.id = u; out->uses_ids = true; break;
			case 0x9: ps.g.count = u; break;
			case 0xA:
				if (ps.sp < STACK_DEPTH)
					ps.stack[ps.sp++] = ps.g;
				break;
			case 0xB:
				if (ps.sp > 0)
					ps.g = ps.stack[--ps.sp];
				break;
			default: break;
			}
		} else if (type == 2) {         /* local */
			/* four bytes carry their own page */
			uint32_t full = n == 4 ? u : (uint32_t) ps.g.page << 16 | u;
			switch (tag) {
			case 0x0:
				if (ps.nusages < MAX_USAGES)
					ps.usages[ps.nusages++] = full;
				break;
			case 0x1: ps.umin = full; ps.has_min = true; break;
			case 0x2: ps.umax = full; ps.has_max = true; break;
			default: break;
			}
		} else if (type == 0) {         /* main */
			if (tag == 0x8)
				input_item(&ps, out, u);
			/* every main item ends the local ones before it */
			ps.nusages = 0;
			ps.has_min = ps.has_max = false;
		}
	}
	/* a mouse is X and Y; buttons alone are not one */
	if (!out->mouse)
		memset(&out->buttons, 0, sizeof(out->buttons));
	return out->kbd || out->mouse;
}

/* ==== reports ==== */

static uint32_t bits_at(const uint8_t *r, size_t n, uint32_t bit, int size)
{
	uint32_t v = 0;
	for (int i = 0; i < size; i++) {
		uint32_t b = bit + i;
		if (b / 8 < n && (r[b / 8] & (1 << (b % 8))))
			v |= 1u << i;
	}
	return v;
}

static int32_t field(const uint8_t *r, size_t n, const struct hid_field *f, int k)
{
	uint32_t v = bits_at(r, n, f->bit + k * f->size, f->size);
	if (f->sign && f->size < 32 && (v & (1u << (f->size - 1))))
		v |= ~0u << f->size;
	return (int32_t) v;
}

/* the report's data, after its ID, if it is the one wanted */
static bool data_of(const struct hid_layout *l, uint8_t id, const uint8_t **r, size_t *n)
{
	if (!l->uses_ids)
		return true;
	if (*n < 1 || (*r)[0] != id)
		return false;
	(*r)++;
	(*n)--;
	return true;
}

static void add_key(uint8_t *boot, int *nk, uint8_t u)
{
	if (*nk < 6)
		boot[2 + *nk] = u;
	(*nk)++;
}

bool hid_report_keyboard(const struct hid_layout *l, const uint8_t *r, size_t n,
			 uint8_t boot[8])
{
	if (!l->kbd || !data_of(l, l->kbd_id, &r, &n))
		return false;
	memset(boot, 0, 8);
	int nk = 0;
	if (l->mods.size)
		boot[0] = bits_at(r, n, l->mods.bit, 8);
	for (int k = 0; k < l->keys.count; k++) {
		uint8_t u = field(r, n, &l->keys, k);
		if (u == 0x01) {
			nk = 7;         /* the keyboard says it lost count */
			break;
		}
		if (u > 3)
			add_key(boot, &nk, u);
	}
	for (int k = 0; k < l->bitmap.count; k++) {
		if (!bits_at(r, n, l->bitmap.bit + k, 1))
			continue;
		unsigned u = l->bitmap_first + k;
		if (u >= 0xE0 && u <= 0xE7)
			boot[0] |= 1 << (u - 0xE0);
		else if (u > 3 && u < 0x100)
			add_key(boot, &nk, u);
	}
	if (nk > 6)
		memset(boot + 2, 0x01, 6);      /* ErrorRollOver */
	return true;
}

bool hid_report_mouse(const struct hid_layout *l, const uint8_t *r, size_t n,
		      int *dx, int *dy, int *wheel, int *buttons)
{
	if (!l->mouse || !data_of(l, l->mouse_id, &r, &n))
		return false;
	*buttons = l->buttons.size ? (int) bits_at(r, n, l->buttons.bit, l->buttons.count) : 0;
	*dx = field(r, n, &l->x, 0);
	*dy = field(r, n, &l->y, 0);
	*wheel = l->wheel.size ? field(r, n, &l->wheel, 0) : 0;
	return true;
}

const char *hid_layout_what(const struct hid_layout *l)
{
	if (l->kbd && l->mouse)
		return "keyboard and mouse";
	if (l->kbd)
		return "keyboard";
	if (l->mouse)
		return "mouse";
	return "nothing to use";
}
