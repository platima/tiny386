/*
 * Which ini, and the files it names: see setup.h.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "ini_edit.h"
#include "setup.h"

#ifdef ESP_PLATFORM
/* the ini's two copies in PSRAM, not internal RAM */
#include "esp_heap_caps.h"
static void *setup_malloc(size_t n)
{
	void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
	return p ? p : malloc(n);
}
#else
#define setup_malloc malloc
#endif

static bool ends_with_ini(const char *n)
{
	size_t l = strlen(n);
	return l > 4 && !strcasecmp(n + l - 4, ".ini");
}

/* names that are not a setup: hidden files (macOS leaves "._name" copies),
   the one Windows writes into folders it has shown, and the DOS board's
   own settings (dosboard.c reads them before the panel starts) */
static bool skip_name(const char *n)
{
	return n[0] == '.' || !strcasecmp(n, "desktop.ini") ||
		!strcasecmp(n, "dosboard.ini");
}

/* the first line that is a comment (";" or "#"), without the marker */
static void first_comment(const char *path, char *desc, size_t size)
{
	desc[0] = 0;
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[160];
	for (int lines = 0; lines < 8 && fgets(line, sizeof(line), f); lines++) {
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '[')
			break;          /* the settings have begun */
		if (*p != ';' && *p != '#')
			continue;
		while (*p == ';' || *p == '#' || *p == ' ' || *p == '\t')
			p++;
		size_t n = strcspn(p, "\r\n");
		if (!n)
			continue;
		if (n >= size)
			n = size - 1;
		memcpy(desc, p, n);
		desc[n] = 0;
		break;
	}
	fclose(f);
}

static int by_name(const void *a, const void *b)
{
	return strcasecmp(((const struct setup_ini *) a)->name,
			  ((const struct setup_ini *) b)->name);
}

static char s_warn[320];

int setup_find(const struct setup_place *places, struct setup_list *out)
{
	memset(out, 0, sizeof(*out));
	s_warn[0] = 0;
	for (int p = 0; places[p].dir; p++) {
		/* a folder whose path does not fit out->dir is passed over, not
		   cut short to some other folder's name */
		if (strlen(places[p].dir) >= sizeof(out->dir))
			continue;
		/* v4.4: an ini whose rewrite (Ctrl+Alt+F11) a cut in power
		   stopped part way is put back first, so that it is listed
		   (v4.5: what it leaves as it was, setup_tidy_warning()) */
		setup_ini_tidy(places[p].dir);
		DIR *d = opendir(places[p].dir);
		if (!d)
			continue;
		struct dirent *e;
		while ((e = readdir(d))) {
			const char *n = e->d_name;
			if (places[p].only ? strcasecmp(n, places[p].only) :
			    (!ends_with_ini(n) || skip_name(n)))
				continue;
			if (strlen(n) >= SETUP_NAME_MAX)
				continue;
			if (out->n == SETUP_MAX_INIS) {
				out->more++;
				continue;
			}
			strcpy(out->ini[out->n++].name, n);
		}
		closedir(d);
		if (out->n) {
			snprintf(out->dir, sizeof(out->dir), "%s", places[p].dir);
			qsort(out->ini, out->n, sizeof(out->ini[0]), by_name);
			for (int i = 0; i < out->n; i++) {
				char path[sizeof(out->dir) + SETUP_NAME_MAX + 1];
				setup_path(out, i, path, sizeof(path));
				first_comment(path, out->ini[i].desc, sizeof(out->ini[i].desc));
			}
			return out->n;
		}
	}
	return 0;
}

void setup_path(const struct setup_list *l, int i, char *out, size_t size)
{
	snprintf(out, size, "%s/%s", l->dir, l->ini[i].name);
}

static int find(const struct setup_list *l, const char *name)
{
	for (int i = 0; i < l->n; i++)
		if (!strcasecmp(l->ini[i].name, name))
			return i;
	return -1;
}

int setup_default(const struct setup_list *l, const char *last)
{
	int i;
	if (last && last[0] && (i = find(l, last)) >= 0)
		return i;
	if ((i = find(l, "tiny386.ini")) >= 0)
		return i;
	if ((i = find(l, "freedos.ini")) >= 0)
		return i;
	return 0;
}

bool setup_last_read(const char *dir, char *name, size_t size)
{
	char path[160];
	snprintf(path, sizeof(path), "%s/%s", dir, SETUP_LAST_FILE);
	name[0] = 0;
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	bool ok = fgets(name, size, f) != NULL;
	fclose(f);
	name[strcspn(name, "\r\n")] = 0;
	return ok && name[0];
}

bool setup_last_write(const char *dir, const char *name)
{
	char old[SETUP_NAME_MAX];
	if (setup_last_read(dir, old, sizeof(old)) && !strcmp(old, name))
		return true;            /* the card is not written for nothing */
	char path[160];
	snprintf(path, sizeof(path), "%s/%s", dir, SETUP_LAST_FILE);
	FILE *f = fopen(path, "w");
	if (!f)
		return false;
	bool ok = fprintf(f, "%s\n", name) > 0;
	return fclose(f) == 0 && ok;
}

const char *setup_resolve(const char *dir, const char *value,
			  const struct setup_map *maps, int nmaps,
			  char *out, size_t size, bool *mapped_missing)
{
	if (mapped_missing)
		*mapped_missing = false;
	if (!value[0]) {
		snprintf(out, size, "%s", value);
		return out;
	}
	if (value[0] == '/') {
		for (int i = 0; i < nmaps; i++) {
			size_t n = strlen(maps[i].from);
			if (strncmp(value, maps[i].from, n) || (value[n] && value[n] != '/'))
				continue;
			if (!maps[i].to) {
				if (mapped_missing)
					*mapped_missing = true;
				break;
			}
			snprintf(out, size, "%s%s", maps[i].to, value + n);
			return out;
		}
		snprintf(out, size, "%s", value);
		return out;
	}
	while (value[0] == '.' && value[1] == '/')
		value += 2;
	snprintf(out, size, "%s/%s", dir, value);
	return out;
}

bool setup_is_path_key(const char *section, const char *name)
{
	static const char *const keys[] = {
		"bios", "vga_bios", "hda", "hdb", "hdc", "hdd", "cda", "cdb", "cdc",
		"cdd", "fda", "fdb", "kernel", "initrd", "linuxstart", NULL,
	};
	if (strcmp(section, "pc"))
		return false;
	for (int i = 0; keys[i]; i++)
		if (!strcmp(name, keys[i]))
			return true;
	return false;
}

/* ==== v4.4: a setting saved in the ini ==== */

static bool exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fclose(f);
	return true;
}

/* the whole file into a new buffer of at least max + 1 bytes; its length in
   *len.  NULL (why set) if it cannot be read or is larger than max. */
static char *read_whole(const char *path, size_t max, size_t *len, char *why, size_t whylen)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(why, whylen, "%s cannot be opened (%s)", path, strerror(errno));
		return NULL;
	}
	char *buf = setup_malloc(max + 1);
	if (!buf) {
		fclose(f);
		snprintf(why, whylen, "no memory for %u bytes", (unsigned) max);
		return NULL;
	}
	size_t n = fread(buf, 1, max + 1, f);
	bool err = ferror(f);
	fclose(f);
	if (err) {
		free(buf);
		snprintf(why, whylen, "%s could not be read", path);
		return NULL;
	}
	if (n > max) {
		free(buf);
		snprintf(why, whylen, "%s is larger than %u bytes", path, (unsigned) max);
		return NULL;
	}
	*len = n;
	return buf;
}

/* ==== v4.5: no rename ==== */

#ifdef SETUP_TEST_CUT
int setup_test_cut;
#define CUT(n) (setup_test_cut == (n))
#else
#define CUT(n) 0
#endif

/* path.t386new ends with this line: "\r\n;t386new LLLLLLLL CCCCCCCC\r\n",
   the length of the text before it and that text's CRC-32, in hex */
#define TRAILER_LEN 30

static uint32_t crc32_of(const char *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= (uint8_t) p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}
	return ~c;
}

static void make_trailer(const char *text, size_t len, char out[TRAILER_LEN + 1])
{
	snprintf(out, TRAILER_LEN + 1, "\r\n;t386new %08x %08x\r\n", (unsigned) len,
		 (unsigned) crc32_of(text, len));
}

/* buf (n bytes, a path.t386new) is whole and v4.5's: its last line checks;
   *len is the text's length before it */
static bool whole_new(const char *buf, size_t n, size_t *len)
{
	char t[TRAILER_LEN + 1];
	if (n < TRAILER_LEN)
		return false;
	make_trailer(buf, n - TRAILER_LEN, t);
	if (memcmp(buf + n - TRAILER_LEN, t, TRAILER_LEN))
		return false;
	*len = n - TRAILER_LEN;
	return true;
}

/* path holds exactly these len bytes */
static bool same_as(const char *path, const char *buf, size_t len)
{
	size_t n;
	char tmp[160];
	char *back = read_whole(path, len, &n, tmp, sizeof(tmp));
	bool same = back && n == len && !memcmp(back, buf, len);
	free(back);
	return same;
}

/* path written with buf in place (made if it is not there: fopen "wb"
   keeps the name and its directory entry), flushed, synced, read back */
static bool put_whole(const char *path, const char *buf, size_t len)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	bool ok = fwrite(buf, 1, len, f) == len;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	return ok && same_as(path, buf, len);
}

#ifdef SETUP_TEST_CUT
/* what a cut part way through put_whole() leaves: len bytes of buf */
static void put_part(const char *path, const char *buf, size_t len)
{
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(buf, 1, len, f);
		fclose(f);
	}
}
#endif

const char *setup_tidy_warning(void)
{
	return s_warn;
}

static void warn_once(const char *fmt, const char *x)
{
	if (!s_warn[0])
		snprintf(s_warn, sizeof(s_warn), fmt, x);
}

/* x's leftovers put right (setup.h); returns the files written or
   removed, or -1 if they are left as they are (s_warn says why) */
static int tidy_one(const char *x)
{
	char xn[220], xo[220];
	snprintf(xn, sizeof(xn), "%s%s", x, SETUP_NEW_SUFFIX);
	snprintf(xo, sizeof(xo), "%s%s", x, SETUP_OLD_SUFFIX);
	bool hx = exists(x), hn = exists(xn), ho = exists(xo);
	int acts = 0;
	size_t n, len;
	char tmp[160];
	if (hx && hn && ho) {
		warn_once("%s has both .t386old and .t386new beside it: a save of v4.4 was cut "
			  "part way through a rename, and two of the three names may share the "
			  "card's clusters. All three are left as they are: check the card on a PC "
			  "(chkdsk with /f), then delete the .t386old and .t386new files", x);
		return -1;
	}
	if (hn) {
		char *buf = read_whole(xn, SETUP_INI_MAX + TRAILER_LEN, &n, tmp, sizeof(tmp));
		if (buf && whole_new(buf, n, &len)) {
			/* v4.5's copy, whole: the ini made the same, then the copy
			   (never renamed, so it shares nothing) removed */
			if (!hx || !same_as(x, buf, len)) {
				if (!put_whole(x, buf, len)) {
					free(buf);
					return acts;    /* both left; the next start tries again */
				}
				acts++;
			}
		} else if (hx) {
			/* cut before the ini was touched: the copy removed */
			free(buf);
			if (remove(xn) == 0)
				acts++;
			return acts;
		} else if (ho && buf) {
			/* v4.4, cut between its two renames: its new text was whole */
			if (!put_whole(x, buf, n)) {
				free(buf);
				return acts;
			}
			acts++;
		} else {
			/* the copy alone, not whole or not ours: left */
			free(buf);
			return acts;
		}
		free(buf);
		if (remove(xn) == 0)
			acts++;
		if (ho && remove(xo) == 0)
			acts++;
		return acts;
	}
	if (ho) {
		char *buf = read_whole(xo, SETUP_INI_MAX, &n, tmp, sizeof(tmp));
		if (hx) {
			if (buf && same_as(x, buf, n)) {
				free(buf);
				warn_once("%s.t386old holds the very bytes of the ini: a save of v4.4 was "
					  "cut inside a rename, and the two names may share the card's "
					  "clusters. Both are left as they are: check the card on a PC "
					  "(chkdsk with /f), then delete the .t386old file", x);
				return -1;
			}
			free(buf);
			/* v4.4, cut before the old one was removed: other bytes,
			   so other clusters */
			if (remove(xo) == 0)
				acts++;
			return acts;
		}
		/* v4.4: the old one alone: its bytes back as the ini */
		if (buf && put_whole(x, buf, n)) {
			acts++;
			if (remove(xo) == 0)
				acts++;
		}
		free(buf);
	}
	return acts;
}

int setup_ini_save(const char *path, const char *section, const char *name,
		   const char *value, char *why, size_t whylen)
{
	char newp[200], oldp[200];
	why[0] = 0;
	if (snprintf(newp, sizeof(newp), "%s%s", path, SETUP_NEW_SUFFIX) >= (int) sizeof(newp) ||
	    snprintf(oldp, sizeof(oldp), "%s%s", path, SETUP_OLD_SUFFIX) >= (int) sizeof(oldp)) {
		snprintf(why, whylen, "the ini's path is too long");
		return -1;
	}
	/* what an earlier save left beside this ini first (a whole copy put
	   in place), and nothing while v4.4's leftovers want a check */
	s_warn[0] = 0;
	if (tidy_one(path) < 0) {
		snprintf(why, whylen, "%s", s_warn);
		return -1;
	}
	size_t inlen, outlen;
	char *in = read_whole(path, SETUP_INI_MAX, &inlen, why, whylen);
	if (!in)
		return -1;
	size_t outsize = 2 * inlen + 256;
	char *out = setup_malloc(outsize + TRAILER_LEN + 1);
	if (!out) {
		free(in);
		snprintf(why, whylen, "no memory for %u bytes", (unsigned) outsize);
		return -1;
	}
	int r = ini_edit_set(in, inlen, section, name, value, out, outsize, &outlen);
	free(in);
	if (r == INI_EDIT_SAME || r < 0) {
		free(out);
		if (r < 0)
			snprintf(why, whylen, "the new text did not fit");
		return r;
	}

	/* 1. the new text and its check line beside the ini, all of it on the
	   card before the ini is touched */
	char trailer[TRAILER_LEN + 1];
	make_trailer(out, outlen, trailer);
	memcpy(out + outlen, trailer, TRAILER_LEN);
#ifdef SETUP_TEST_CUT
	if (CUT(1)) {
		put_part(newp, out, (outlen + TRAILER_LEN) / 2);
		free(out);
		return -2;
	}
#endif
	if (!put_whole(newp, out, outlen + TRAILER_LEN)) {
		remove(newp);
		free(out);
		snprintf(why, whylen, "%s could not be written whole", newp);
		return -1;
	}
	if (CUT(2)) {
		free(out);
		return -2;
	}

	/* 2. the ini itself, in place: a cut from here on leaves the whole copy
	   beside it, which setup_ini_tidy() puts in place */
#ifdef SETUP_TEST_CUT
	if (CUT(3) || CUT(4)) {
		put_part(path, out, CUT(3) ? 0 : outlen / 2);
		free(out);
		return -2;
	}
#endif
	if (!put_whole(path, out, outlen)) {
		free(out);
		snprintf(why, whylen, "%s could not be rewritten; the new text is whole in %s, "
			 "which the next start puts in place", path, newp);
		return -1;
	}
	free(out);
	if (CUT(5))
		return -2;

	/* 3. the copy, which shares nothing with the ini, removed */
	if (remove(newp) != 0)
		snprintf(why, whylen, "%s could not be removed (the next start removes it)", newp);
	return r;
}

int setup_ini_tidy(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return 0;
	/* the bases first, then each put right: the folder is not changed
	   while it is read */
	char bases[SETUP_MAX_INIS][SETUP_NAME_MAX];
	int nb = 0, acts = 0;
	size_t ln = strlen(SETUP_NEW_SUFFIX), lo = strlen(SETUP_OLD_SUFFIX);
	struct dirent *e;
	while ((e = readdir(d)) && nb < SETUP_MAX_INIS) {
		size_t l = strlen(e->d_name);
		size_t cut = 0;
		if (l > ln && !strcmp(e->d_name + l - ln, SETUP_NEW_SUFFIX))
			cut = ln;
		else if (l > lo && !strcmp(e->d_name + l - lo, SETUP_OLD_SUFFIX))
			cut = lo;
		if (!cut || l - cut >= SETUP_NAME_MAX)
			continue;
		char base[SETUP_NAME_MAX];
		memcpy(base, e->d_name, l - cut);
		base[l - cut] = 0;
		bool seen = false;
		for (int i = 0; i < nb && !seen; i++)
			seen = !strcmp(bases[i], base);
		if (!seen)
			strcpy(bases[nb++], base);
	}
	closedir(d);
	for (int i = 0; i < nb; i++) {
		char x[200];
		/* a folder longer than any of the firmware's places is passed
		   over (a base is shorter than SETUP_NAME_MAX) */
		if (strlen(dir) > 135)
			continue;
		snprintf(x, sizeof(x), "%.135s/%.63s", dir, bases[i]);
		int a = tidy_one(x);
		if (a > 0)
			acts += a;
	}
	return acts;
}
