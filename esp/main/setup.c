/*
 * Which ini, and the files it names: see setup.h.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
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

int setup_find(const struct setup_place *places, struct setup_list *out)
{
	memset(out, 0, sizeof(*out));
	for (int p = 0; places[p].dir; p++) {
		/* a folder whose path does not fit out->dir is passed over, not
		   cut short to some other folder's name */
		if (strlen(places[p].dir) >= sizeof(out->dir))
			continue;
		/* v4.4: an ini whose rewrite (Ctrl+Alt+F11) a cut in power
		   stopped part way is put back first, so that it is listed */
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
	size_t inlen, outlen;
	char *in = read_whole(path, SETUP_INI_MAX, &inlen, why, whylen);
	if (!in)
		return -1;
	size_t outsize = 2 * inlen + 256;
	char *out = setup_malloc(outsize);
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

	/* the new text beside the ini, all of it on the card before anything
	   else changes */
	remove(newp);
	FILE *f = fopen(newp, "wb");
	if (!f) {
		snprintf(why, whylen, "%s cannot be made (%s)", newp, strerror(errno));
		free(out);
		return -1;
	}
	bool ok = fwrite(out, 1, outlen, f) == outlen;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (ok) {
		size_t backlen;
		char tmp[160];
		char *back = read_whole(newp, outlen, &backlen, tmp, sizeof(tmp));
		ok = back && backlen == outlen && !memcmp(back, out, outlen);
		free(back);
	}
	free(out);
	if (!ok) {
		remove(newp);
		snprintf(why, whylen, "%s could not be written whole", newp);
		return -1;
	}

	/* then the swap: a cut from here on leaves names setup_ini_tidy()
	   puts right at the next start */
	remove(oldp);
	if (rename(path, oldp) != 0) {
		snprintf(why, whylen, "%s could not be renamed (%s)", path, strerror(errno));
		remove(newp);
		return -1;
	}
	if (rename(newp, path) != 0) {
		snprintf(why, whylen, "%s could not be renamed (%s)", newp, strerror(errno));
		if (rename(oldp, path) != 0)
			snprintf(why + strlen(why), whylen - strlen(why),
				 "; the old ini is in %s", oldp);
		return -1;
	}
	if (remove(oldp) != 0)
		snprintf(why, whylen, "%s could not be removed", oldp);
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
		char x[200], xn[220], xo[220];
		/* a folder longer than any of the firmware's places is passed
		   over (a base is shorter than SETUP_NAME_MAX) */
		if (strlen(dir) > 135)
			continue;
		snprintf(x, sizeof(x), "%.135s/%.63s", dir, bases[i]);
		snprintf(xn, sizeof(xn), "%s%s", x, SETUP_NEW_SUFFIX);
		snprintf(xo, sizeof(xo), "%s%s", x, SETUP_OLD_SUFFIX);
		bool hx = exists(x), hn = exists(xn), ho = exists(xo);
		if (hx) {
			if (hn && remove(xn) == 0)
				acts++;
			if (ho && remove(xo) == 0)
				acts++;
		} else if (hn && ho) {
			if (rename(xn, x) == 0) {
				acts++;
				if (remove(xo) == 0)
					acts++;
			}
		} else if (ho) {
			if (rename(xo, x) == 0)
				acts++;
		}
		/* X.t386new alone (X gone some other way): left as it is */
	}
	return acts;
}
