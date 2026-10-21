/*
 * Which ini, and the files it names: see setup.h.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "setup.h"

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
