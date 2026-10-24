/*
 * One setting of an ini file changed in its text: see ini_edit.h.
 */
#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "ini_edit.h"

/* a line of the text: [s, e) without its '\n', the next one at nx */
struct ln {
	size_t s, e, nx;
};

static int next_line(const char *t, size_t n, size_t at, struct ln *l)
{
	if (at >= n)
		return 0;
	size_t i = at;
	while (i < n && t[i] != '\n')
		i++;
	l->s = at;
	l->e = i;
	l->nx = i < n ? i + 1 : n;
	return 1;
}

/* what ini.c makes of a line */
enum { L_BLANK, L_COMMENT, L_CONT, L_SECTION, L_KEY, L_BAD };

static int sp(char c)
{
	return isspace((unsigned char) c);
}

static size_t lskip(const char *t, size_t i, size_t e)
{
	while (i < e && sp(t[i]))
		i++;
	return i;
}

static size_t rstrip(const char *t, size_t s, size_t e)
{
	while (e > s && sp(t[e - 1]))
		e--;
	return e;
}

/* ini.c's ini_find_chars_or_comment() on [i, e): the first of chars, or an
   inline comment (a ';' after a space), or e */
static size_t find_cc(const char *t, size_t i, size_t e, const char *chars)
{
	int was_space = 0;
	while (i < e && (!chars || !strchr(chars, t[i])) && !(was_space && t[i] == ';')) {
		was_space = sp(t[i]);
		i++;
	}
	return i;
}

struct cls {
	int kind;
	size_t a, b;            /* L_SECTION, L_KEY: the name */
	size_t vs, ve;          /* L_KEY: the value as written (quotes and all) */
	size_t cs;              /* L_COMMENT: the first character after the marker */
};

/* the line as ini.c reads it: first is the file's first line (a byte order
   mark may begin it); have_prev says whether a setting came before it since
   the last section line (an indented line then continues it) */
static void classify(const char *t, const struct ln *l, int first, int have_prev,
		     struct cls *c)
{
	size_t s = l->s;
	if (first && l->e - s >= 3 && (unsigned char) t[s] == 0xef &&
	    (unsigned char) t[s + 1] == 0xbb && (unsigned char) t[s + 2] == 0xbf)
		s += 3;
	size_t e = rstrip(t, s, l->e);
	size_t q = lskip(t, s, e);
	if (q == e) {
		c->kind = L_BLANK;
		return;
	}
	if (t[q] == ';' || t[q] == '#') {
		c->kind = L_COMMENT;
		c->cs = q;
		return;
	}
	if (have_prev && q > l->s) {
		c->kind = L_CONT;
		return;
	}
	if (t[q] == '[') {
		size_t r = find_cc(t, q + 1, e, "]");
		if (r < e && t[r] == ']') {
			c->kind = L_SECTION;
			c->a = q + 1;
			c->b = r;
			return;
		}
		c->kind = L_BAD;
		return;
	}
	size_t r = find_cc(t, q, e, "=:");
	if (r < e && (t[r] == '=' || t[r] == ':')) {
		c->kind = L_KEY;
		c->a = q;
		c->b = rstrip(t, q, r);
		size_t v = lskip(t, r + 1, e);
		size_t w;
		if (v < e && t[v] == '"') {
			/* quoted: to the closing quote, \ escaping the next */
			w = v + 1;
			while (w < e && t[w] != '"')
				w += t[w] == '\\' && w + 1 < e ? 2 : 1;
			w = w < e ? w + 1 : e;
		} else {
			w = rstrip(t, v, find_cc(t, v, e, NULL));
		}
		c->vs = v;
		c->ve = w;
		return;
	}
	c->kind = L_BAD;
}

static int span_is(const char *t, size_t a, size_t b, const char *str)
{
	size_t n = strlen(str);
	return b - a == n && !memcmp(t + a, str, n);
}

/* a comment line that sets name out of use: ";vbe_blocks = 1", "# vbe_blocks" */
static int comment_names(const char *t, size_t cs, size_t e, const char *name)
{
	size_t i = cs;
	while (i < e && (t[i] == ';' || t[i] == '#' || sp(t[i])))
		i++;
	size_t n = strlen(name);
	if (e - i < n || memcmp(t + i, name, n))
		return 0;
	i += n;
	return i == e || sp(t[i]) || t[i] == '=' || t[i] == ':';
}

/* the value as ini.c hands it on, held against value */
static int value_is(const char *t, size_t vs, size_t ve, const char *value)
{
	if (ve - vs >= 2 && t[vs] == '"' && t[ve - 1] == '"')
		return span_is(t, vs + 1, ve - 1, value);
	return span_is(t, vs, ve, value);
}

struct outbuf {
	char *p;
	size_t n, size;
	int full;
};

static void put(struct outbuf *o, const char *s, size_t n)
{
	if (o->full || n > o->size - o->n) {
		o->full = 1;
		return;
	}
	memcpy(o->p + o->n, s, n);
	o->n += n;
}

static void puts0(struct outbuf *o, const char *s)
{
	put(o, s, strlen(s));
}

int ini_edit_set(const char *in, size_t inlen, const char *section, const char *name,
		 const char *value, char *out, size_t outsize, size_t *outlen)
{
	/* the line end of new lines: as the first line's */
	const char *eol = "\n";
	for (size_t i = 0; i < inlen; i++) {
		if (in[i] == '\n') {
			if (i > 0 && in[i - 1] == '\r')
				eol = "\r\n";
			break;
		}
	}

	/* first pass: where the setting is, or where it would go */
	struct ln l;
	struct cls c;
	size_t at = 0;
	int first = 1, have_prev = 0, in_target = 0, have_target = 0;
	int matches = 0, all_same = 1, dropping = 0, drops = 0, cmt_open = 0;
	size_t cand_last = SIZE_MAX, cand_cmt = SIZE_MAX;
	while (next_line(in, inlen, at, &l)) {
		classify(in, &l, first, have_prev, &c);
		first = 0;
		switch (c.kind) {
		case L_SECTION:
			in_target = span_is(in, c.a, c.b, section);
			have_prev = 0;
			dropping = 0;
			cmt_open = 0;
			if (in_target) {
				have_target = 1;
				cand_last = l.nx;
				cand_cmt = SIZE_MAX;
			}
			break;
		case L_KEY:
			have_prev = c.b > c.a;
			dropping = 0;
			cmt_open = 0;
			if (in_target) {
				cand_last = l.nx;
				if (span_is(in, c.a, c.b, name)) {
					matches++;
					if (!value_is(in, c.vs, c.ve, value))
						all_same = 0;
					dropping = 1;
				}
			}
			break;
		case L_COMMENT:
			if (in_target) {
				cand_last = l.nx;
				if (comment_names(in, c.cs, rstrip(in, c.cs, l.e), name)) {
					cand_cmt = l.nx;
					cmt_open = 1;
				}
			}
			break;
		case L_CONT:
			if (dropping)
				drops++;
			if (in_target) {
				cand_last = l.nx;
				/* a new line straight after the comment would take
				   this continuation over: after it, or (with blank
				   lines between) after the section's last line */
				if (cmt_open) {
					if (l.s == cand_cmt) {
						cand_cmt = l.nx;
					} else {
						cand_cmt = SIZE_MAX;
						cmt_open = 0;
					}
				}
			}
			break;
		case L_BAD:
			if (in_target)
				cand_last = l.nx;
			break;
		}
		at = l.nx;
	}

	struct outbuf o = { out, 0, outsize, 0 };
	int ret;
	if (matches && all_same && !drops) {
		put(&o, in, inlen);
		ret = INI_EDIT_SAME;
	} else if (matches) {
		/* second pass: each of its lines with the new value; the lines
		   that continue it left out */
		at = 0;
		first = 1;
		have_prev = 0;
		in_target = 0;
		dropping = 0;
		while (next_line(in, inlen, at, &l)) {
			classify(in, &l, first, have_prev, &c);
			first = 0;
			int keep = 1;
			switch (c.kind) {
			case L_SECTION:
				in_target = span_is(in, c.a, c.b, section);
				have_prev = 0;
				dropping = 0;
				break;
			case L_KEY:
				have_prev = c.b > c.a;
				dropping = 0;
				if (in_target && span_is(in, c.a, c.b, name)) {
					put(&o, in + l.s, c.vs - l.s);
					puts0(&o, value);
					put(&o, in + c.ve, l.nx - c.ve);
					keep = 0;
					dropping = 1;
				}
				break;
			case L_CONT:
				if (dropping)
					keep = 0;
				break;
			}
			if (keep)
				put(&o, in + l.s, l.nx - l.s);
			at = l.nx;
		}
		ret = INI_EDIT_CHANGED;
	} else {
		size_t ins = !have_target ? inlen : cand_cmt != SIZE_MAX ? cand_cmt : cand_last;
		put(&o, in, ins);
		if (ins > 0 && in[ins - 1] != '\n')
			puts0(&o, eol);         /* the line before had no end */
		if (!have_target) {
			if (ins > 0)
				puts0(&o, eol);
			puts0(&o, "[");
			puts0(&o, section);
			puts0(&o, "]");
			puts0(&o, eol);
		}
		puts0(&o, name);
		puts0(&o, " = ");
		puts0(&o, value);
		puts0(&o, eol);
		put(&o, in + ins, inlen - ins);
		ret = have_target ? INI_EDIT_ADDED : INI_EDIT_NEW_SECTION;
	}
	if (o.full)
		return INI_EDIT_NO_ROOM;
	*outlen = o.n;
	return ret;
}
