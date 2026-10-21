/*
 * Host test of esp/main/setup.c (FIRMWARE-PLAN 3.4): which ini, and the
 * files it names, on a pretend card in a scratch folder.
 *
 *   the card as the Linux route lays it out: tiny386/ with its ini files
 *     (the Linux route's own, copied into tests/host/data/linux-route),
 *     with a desktop.ini, a macOS "._" file and the DOS board's
 *     dosboard.ini, none of which may be listed;
 *   the list sorted, each with its first comment line;
 *   the default: tiny386.ini, else freedos.ini, else the first; the last
 *     choice from lastboot.txt; lastboot.txt written only when it changes;
 *   the places in order: tiny386/ first, then tiny386.ini in the top
 *     folder, then nothing;
 *   every file named in the Linux route's inis, read with tiny386's own
 *     ini.c: relative names beside the ini, /mnt/sd and /mnt/win mapped,
 *     /mnt/win with no WIN9X partition flagged, other absolute paths kept.
 *
 *   test_setup OUT
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ini.h"
#include "setup.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static char root[512];          /* OUT/card */

static void put(const char *rel, const char *text)
{
	char p[700];
	snprintf(p, sizeof(p), "%s/%s", root, rel);
	FILE *f = fopen(p, "w");
	if (!f) {
		perror(p);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

static void copy_in(const char *from, const char *rel)
{
	FILE *f = fopen(from, "r");
	if (!f) {
		perror(from);
		exit(2);
	}
	static char buf[65536];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	fclose(f);
	put(rel, buf);
}

static void sh(const char *cmd)
{
	if (system(cmd) != 0) {
		fprintf(stderr, "failed: %s\n", cmd);
		exit(2);
	}
}

/* ==== the files in an ini, as esp_main.c's parse_ini() resolves them ==== */

struct resolved {
	char key[16];
	char value[128];
	char path[256];
	bool missing;
};
static struct resolved got[32];
static int ngot;
static const char *cur_dir;
static struct setup_map maps[2];

static int on_key(void *user, const char *section, const char *name, const char *value)
{
	(void) user;
	if (!setup_is_path_key(section, name) || ngot == 32)
		return 1;
	struct resolved *r = &got[ngot++];
	snprintf(r->key, sizeof(r->key), "%s", name);
	snprintf(r->value, sizeof(r->value), "%s", value);
	setup_resolve(cur_dir, value, maps, 2, r->path, sizeof(r->path), &r->missing);
	return 1;
}

static const struct resolved *find_key(const char *key)
{
	for (int i = 0; i < ngot; i++)
		if (!strcmp(got[i].key, key))
			return &got[i];
	return NULL;
}

static void read_ini(const char *dir, const char *name)
{
	char path[1400];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	ngot = 0;
	cur_dir = dir;
	int err = ini_parse(path, on_key, NULL);
	CHECK(err == 0, "%s: ini_parse says %d", name, err);
	printf("  %s:\n", name);
	for (int i = 0; i < ngot; i++)
		printf("    %-9s %-24s -> %s%s\n", got[i].key, got[i].value, got[i].path,
		       got[i].missing ? "  (no WIN9X partition: flagged)" : "");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_setup OUT DATA\n");
		return 2;
	}
	const char *data = argv[2];
	/* this test's own folder, made afresh each run: under OUT if the card's
	   tiny386 folder fits in setup_list.dir from there, else a short one
	   made in TMPDIR (default /tmp) and removed at the end */
	const char *suffix = "/test-setup-card/sdcard/tiny386";
	bool made_tmp = false;
	char cmd[1800];
	if (strlen(argv[1]) + strlen(suffix) < sizeof(((struct setup_list *) 0)->dir)) {
		snprintf(root, sizeof(root), "%s/test-setup-card", argv[1]);
	} else {
		const char *tmp = getenv("TMPDIR");
		snprintf(root, sizeof(root), "%s/t386setup-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
		if (!mkdtemp(root)) {
			perror(root);
			return 2;
		}
		made_tmp = true;
		printf("(OUT is too long a path for the card's folder: the card is in %s)\n", root);
	}
	snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/sdcard/tiny386' "
		 "'%s/sdcard3'", root, root, root);
	sh(cmd);

	char sd[600], dir[650], sd3[600];
	snprintf(sd, sizeof(sd), "%s/sdcard", root);
	snprintf(dir, sizeof(dir), "%s/sdcard/tiny386", root);
	snprintf(sd3, sizeof(sd3), "%s/sdcard3", root);
	struct setup_place places[] = {
		{ dir, NULL }, { sd, "tiny386.ini" }, { NULL, NULL },
	};
	struct setup_list l;

	/* ==== nothing on the card ==== */
	CHECK(setup_find(places, &l) == 0, "empty card: %d found", l.n);

	/* ==== a folder longer than the list can keep: passed over ==== */
	{
		static const char *const deep = "sdcard/a-folder-name-long-enough-on-its-own-"
			"to-pass-the-ninety-six-bytes-that-setup-list-keeps";
		char longdir[700];
		snprintf(longdir, sizeof(longdir), "%s/%s", root, deep);
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", longdir);
		sh(cmd);
		char rel[300];
		snprintf(rel, sizeof(rel), "%s/long.ini", deep);
		put(rel, "[pc]\n");
		struct setup_place lp[] = { { longdir, NULL }, { NULL, NULL } };
		CHECK(setup_find(lp, &l) == 0, "a folder path of %d characters gave %d inis",
		      (int) strlen(longdir), l.n);
		snprintf(cmd, sizeof(cmd), "rm -rf '%s/sdcard/a-folder-name-long-enough-on-its-own-"
			 "to-pass-the-ninety-six-bytes-that-setup-list-keeps'", root);
		sh(cmd);
	}

	/* ==== tiny386.ini in the top folder only ==== */
	put("sdcard/tiny386.ini", "; top folder\n[pc]\nhda = /sdcard/a.img\n");
	put("sdcard/other.ini", "[pc]\n");
	CHECK(setup_find(places, &l) == 1 && !strcmp(l.ini[0].name, "tiny386.ini") &&
	      !strcmp(l.dir, sd), "top folder: %d found, %s in %s", l.n,
	      l.n ? l.ini[0].name : "-", l.dir);
	printf("top folder only: %s/%s (other.ini there is not listed)\n", l.dir,
	       l.ini[0].name);

	/* ==== the Linux route's folder ==== */
	char from[700];
	static const char *const route[] = { "freedos.ini", "bench.ini", "win95.ini" };
	for (int i = 0; i < 3; i++) {
		char rel[100];
		snprintf(from, sizeof(from), "%s/%s", data, route[i]);
		snprintf(rel, sizeof(rel), "sdcard/tiny386/%s", route[i]);
		copy_in(from, rel);
	}
	put("sdcard/tiny386/desktop.ini", "[.ShellClassInfo]\n");
	put("sdcard/tiny386/._freedos.ini", "junk");
	put("sdcard/tiny386/DOSBOARD.INI", "[dosboard]\npclk_phase = 4\n");
	put("sdcard/tiny386/BIOS.BIN", "");
	put("sdcard/tiny386/notes.txt", "");
	CHECK(setup_find(places, &l) == 3, "tiny386 folder: %d found", l.n);
	CHECK(!strcmp(l.dir, dir), "dir %s", l.dir);
	printf("the Linux route's folder (%s):\n", l.dir);
	for (int i = 0; i < l.n; i++)
		printf("  %d  %-12s %s\n", i + 1, l.ini[i].name, l.ini[i].desc);
	CHECK(l.n == 3 && !strcmp(l.ini[0].name, "bench.ini") &&
	      !strcmp(l.ini[1].name, "freedos.ini") && !strcmp(l.ini[2].name, "win95.ini"),
	      "order");
	CHECK(!strncmp(l.ini[1].desc, "FreeDOS from the hard disk image", 32),
	      "freedos.ini description: \"%s\"", l.ini[1].desc);

	/* ==== the default and the last choice ==== */
	char last[SETUP_NAME_MAX];
	CHECK(!setup_last_read(l.dir, last, sizeof(last)), "lastboot.txt before any");
	CHECK(setup_default(&l, NULL) == 1, "default without a last choice: %d",
	      setup_default(&l, NULL));
	CHECK(setup_last_write(l.dir, "win95.ini"), "lastboot.txt not written");
	CHECK(setup_last_read(l.dir, last, sizeof(last)) && !strcmp(last, "win95.ini"),
	      "lastboot.txt reads back \"%s\"", last);
	CHECK(setup_default(&l, last) == 2, "default after win95.ini was chosen");
	CHECK(setup_default(&l, "gone.ini") == 1, "last choice no longer there");
	struct stat st1, st2;
	char lp[700];
	snprintf(lp, sizeof(lp), "%s/%s", l.dir, SETUP_LAST_FILE);
	stat(lp, &st1);
	sleep(1);
	CHECK(setup_last_write(l.dir, "win95.ini"), "lastboot.txt rewrite");
	stat(lp, &st2);
	CHECK(st1.st_mtime == st2.st_mtime, "lastboot.txt written again though unchanged");
	printf("default: freedos.ini; after win95.ini is chosen: win95.ini "
	       "(lastboot.txt; not rewritten when the same)\n");
	put("sdcard/tiny386/tiny386.ini", "[pc]\n");
	setup_find(places, &l);
	/* bench, freedos, tiny386, win95 */
	CHECK(l.n == 4 && setup_default(&l, NULL) == 2, "tiny386.ini is the default");
	CHECK(setup_default(&l, "WIN95.INI") == 3, "case of the last choice");

	/* ==== the files the Linux route's inis name ==== */
	maps[0] = (struct setup_map) { "/mnt/sd", sd };
	maps[1] = (struct setup_map) { "/mnt/win", sd3 };
	printf("files named in the Linux route's inis (/mnt/sd -> %s, "
	       "/mnt/win -> %s):\n", sd, sd3);
	char want[800];
	read_ini(dir, "freedos.ini");
	const struct resolved *r = find_key("bios");
	snprintf(want, sizeof(want), "%s/bios.bin", dir);
	CHECK(r && !strcmp(r->path, want), "freedos bios: %s", r ? r->path : "-");
	r = find_key("hda");
	snprintf(want, sizeof(want), "%s/freedos-hd.img", dir);
	CHECK(r && !strcmp(r->path, want) && !r->missing, "freedos hda: %s",
	      r ? r->path : "-");
	read_ini(dir, "bench.ini");
	r = find_key("vga_bios");
	snprintf(want, sizeof(want), "%s/vgabios.bin", dir);
	CHECK(r && !strcmp(r->path, want), "bench vga_bios: %s", r ? r->path : "-");
	read_ini(dir, "win95.ini");
	r = find_key("hda");
	snprintf(want, sizeof(want), "%s/win95.img", sd3);
	CHECK(r && !strcmp(r->path, want) && !r->missing, "win95 hda: %s",
	      r ? r->path : "-");
	maps[1].to = NULL;
	printf("and with no WIN9X partition on the card:\n");
	read_ini(dir, "win95.ini");
	r = find_key("hda");
	CHECK(r && r->missing, "win95 hda without WIN9X not flagged");
	maps[1].to = sd3;

	/* ==== other forms ==== */
	char out[256];
	bool miss;
	setup_resolve(dir, "./disks/a.img", maps, 2, out, sizeof(out), &miss);
	snprintf(want, sizeof(want), "%s/disks/a.img", dir);
	CHECK(!strcmp(out, want), "./disks/a.img -> %s", out);
	setup_resolve(dir, "/sdcard/x.img", maps, 2, out, sizeof(out), &miss);
	CHECK(!strcmp(out, "/sdcard/x.img") && !miss, "absolute kept: %s", out);
	setup_resolve(dir, "/mnt/sdcard/x.img", maps, 2, out, sizeof(out), &miss);
	CHECK(!strcmp(out, "/mnt/sdcard/x.img"), "/mnt/sdcard is not /mnt/sd: %s", out);
	setup_resolve(dir, "/mnt/sd", maps, 2, out, sizeof(out), &miss);
	CHECK(!strcmp(out, sd), "/mnt/sd alone: %s", out);
	CHECK(setup_is_path_key("pc", "fdb") && !setup_is_path_key("pc", "mem_size") &&
	      !setup_is_path_key("esp", "hda"), "path keys");

	printf("setup: %d checks, %d failed\n", checks, fails);
	if (made_tmp) {
		snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
		sh(cmd);
	}
	return fails ? 1 : 0;
}
