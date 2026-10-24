/*
 * Host test of v4.4's saving of a setting in the ini (Ctrl+Alt+F11 saves
 * vbe_blocks): esp/main/ini_edit.c, and setup_ini_save() and
 * setup_ini_tidy() of esp/main/setup.c on files in a scratch folder.
 *
 *  1. ini_edit_set() on inis with the setting there (LF and CR LF, an
 *     inline comment, spaces, ':' for '=', quotes, twice, in two [esp]
 *     sections, lines that continue it), with it there already, with it
 *     commented out (the package's own win95-800x480.ini), with it only in
 *     another section, with no [esp] at all, an empty file, a byte order
 *     mark, a last line with no line end, names that only look alike.
 *     Each result is held against the text it should be, byte for byte,
 *     and then read with tiny386's own ini.c: [esp] vbe_blocks must come
 *     out as the new value and every other setting as it was, in order.
 *  2. setup_ini_save(): the file afterwards, nothing left beside it, the
 *     ini rewritten in place (v4.5: the same file, never renamed), and no
 *     write at all when the ini has the value already; leftovers beside it
 *     tidied first, and a refusal while v4.4's may share its clusters; an
 *     ini that cannot be read, one too large, a folder that cannot be
 *     written: an error, the ini as it was, nothing left beside.
 *  2b. (v4.5) a cut in power at each of the five steps of a save
 *     (setup_test_cut, -DSETUP_TEST_CUT), then the next start's tidy: the
 *     ini whole, as it was or saved, nothing beside it, and listed.
 *  3. setup_ini_tidy(): each state a cut in power can leave part way
 *     through a save, v4.5's (no rename: a copy with a check line) and
 *     v4.4's (two renames), put right; what may be two names on one chain
 *     (v4.4, a cut inside a rename) left, with a warning; other files left
 *     alone; setup_find() lists an ini that a cut had left only as its
 *     .t386new.
 *
 *   test_ini_edit OUT TREE
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ini.h"
#include "ini_edit.h"
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

/* ==== what ini.c reads ==== */

struct kv {
	char s[64], n[64], v[200];
};
struct kvs {
	int n;
	struct kv e[256];
};

static int collect(void *user, const char *section, const char *name, const char *value)
{
	struct kvs *k = user;
	if (k->n < 256) {
		snprintf(k->e[k->n].s, sizeof(k->e[0].s), "%s", section);
		snprintf(k->e[k->n].n, sizeof(k->e[0].n), "%s", name);
		snprintf(k->e[k->n].v, sizeof(k->e[0].v), "%s", value);
		k->n++;
	}
	return 1;
}

static void parse(const char *text, size_t len, struct kvs *k)
{
	char *z = malloc(len + 1);
	memcpy(z, text, len);
	z[len] = 0;
	k->n = 0;
	ini_parse_string(z, collect, k);
	free(z);
}

/* the settings of a and b the same but [esp] vbe_blocks, which b must
   end with as want (the last one read is the one the firmware keeps) */
static bool same_but(const struct kvs *a, const struct kvs *b, const char *want, char *why,
		     size_t whylen)
{
	struct kvs x = { 0 }, y = { 0 };
	const char *last = NULL;
	for (int i = 0; i < a->n; i++)
		if (strcmp(a->e[i].s, "esp") || strcmp(a->e[i].n, "vbe_blocks"))
			x.e[x.n++] = a->e[i];
	for (int i = 0; i < b->n; i++) {
		if (!strcmp(b->e[i].s, "esp") && !strcmp(b->e[i].n, "vbe_blocks"))
			last = b->e[i].v;
		else
			y.e[y.n++] = b->e[i];
	}
	if (!last || strcmp(last, want)) {
		snprintf(why, whylen, "[esp] vbe_blocks read as %s", last ? last : "(none)");
		return false;
	}
	if (x.n != y.n) {
		snprintf(why, whylen, "%d other settings before, %d after", x.n, y.n);
		return false;
	}
	for (int i = 0; i < x.n; i++)
		if (strcmp(x.e[i].s, y.e[i].s) || strcmp(x.e[i].n, y.e[i].n) ||
		    strcmp(x.e[i].v, y.e[i].v)) {
			snprintf(why, whylen, "setting %d: [%s] %s = %s became [%s] %s = %s", i,
				 x.e[i].s, x.e[i].n, x.e[i].v, y.e[i].s, y.e[i].n, y.e[i].v);
			return false;
		}
	return true;
}

/* ==== 1. the text ==== */

static void edit_case(const char *what, const char *in, const char *value, int want_code,
		      const char *want)
{
	char out[8192];
	size_t outlen = 0;
	int r = ini_edit_set(in, strlen(in), "esp", "vbe_blocks", value, out, sizeof(out), &outlen);
	CHECK(r == want_code, "%s: code %d, wanted %d", what, r, want_code);
	if (r < 0)
		return;
	CHECK(outlen == strlen(want) && !memcmp(out, want, outlen),
	      "%s: got\n--8<--\n%.*s\n--8<-- wanted\n--8<--\n%s\n--8<--", what, (int) outlen, out,
	      want);
	struct kvs *a = malloc(sizeof(*a)), *b = malloc(sizeof(*b));
	parse(in, strlen(in), a);
	parse(out, outlen, b);
	char why[300];
	CHECK(same_but(a, b, value, why, sizeof(why)), "%s: read back: %s", what, why);
	free(a);
	free(b);
	printf("  ok: %s (code %d)\n", what, r);
}

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char *b = malloc(65536);
	*len = fread(b, 1, 65535, f);
	b[*len] = 0;
	fclose(f);
	return b;
}

static void test_edit(const char *tree)
{
	printf("1. ini_edit_set()\n");
	edit_case("there, LF", "[esp]\nconsole = 2\nvbe_blocks = 1\n\n[pc]\nmem_size = max\n", "0",
		  INI_EDIT_CHANGED, "[esp]\nconsole = 2\nvbe_blocks = 0\n\n[pc]\nmem_size = max\n");
	edit_case("there, CR LF, spaces and an inline comment kept",
		  "; mine\r\n[esp]\r\n  vbe_blocks=1   ; was on\r\nconsole = 2\r\n", "0",
		  INI_EDIT_CHANGED, "; mine\r\n[esp]\r\n  vbe_blocks=0   ; was on\r\nconsole = 2\r\n");
	edit_case("there already: nothing changed", "[esp]\nvbe_blocks = 0\n", "0", INI_EDIT_SAME,
		  "[esp]\nvbe_blocks = 0\n");
	edit_case("there already, quoted", "[esp]\nvbe_blocks = \"1\" ; q\n", "1", INI_EDIT_SAME,
		  "[esp]\nvbe_blocks = \"1\" ; q\n");
	edit_case("quoted, the quotes replaced, the comment kept", "[esp]\nvbe_blocks = \"1\" ; q\n",
		  "0", INI_EDIT_CHANGED, "[esp]\nvbe_blocks = 0 ; q\n");
	edit_case("with ':'", "[esp]\nvbe_blocks: 1\n", "0", INI_EDIT_CHANGED,
		  "[esp]\nvbe_blocks: 0\n");
	edit_case("no value written", "[esp]\nvbe_blocks =\n", "1", INI_EDIT_CHANGED,
		  "[esp]\nvbe_blocks =1\n");
	edit_case("twice in [esp]: both", "[esp]\nvbe_blocks = 1\nconsole = 2\nvbe_blocks = 1\n", "0",
		  INI_EDIT_CHANGED, "[esp]\nvbe_blocks = 0\nconsole = 2\nvbe_blocks = 0\n");
	edit_case("in two [esp] sections: both",
		  "[esp]\nvbe_blocks = 0\n[pc]\nfda = a.img\n[esp]\nvbe_blocks = 1\n", "0",
		  INI_EDIT_CHANGED, "[esp]\nvbe_blocks = 0\n[pc]\nfda = a.img\n[esp]\nvbe_blocks = 0\n");
	edit_case("its continuation lines go", "[esp]\nvbe_blocks = 1\n  1\n\n  0\nconsole = 2\n", "0",
		  INI_EDIT_CHANGED, "[esp]\nvbe_blocks = 0\n\nconsole = 2\n");
	edit_case("the last line, with no line end", "[esp]\nvbe_blocks = 1", "0", INI_EDIT_CHANGED,
		  "[esp]\nvbe_blocks = 0");
	edit_case("a section line with an inline comment", "[esp] ; mine\nvbe_blocks = 1\n", "0",
		  INI_EDIT_CHANGED, "[esp] ; mine\nvbe_blocks = 0\n");

	edit_case("not there: after the comment that names it",
		  "[esp]\nconsole = 2\n; v4.0: 1 draws ...\n;vbe_blocks = 1\n\n[pc]\nhda = x.img\n", "0",
		  INI_EDIT_ADDED,
		  "[esp]\nconsole = 2\n; v4.0: 1 draws ...\n;vbe_blocks = 1\nvbe_blocks = 0\n\n[pc]\nhda = x.img\n");
	edit_case("not there: '#' comment", "[esp]\n# vbe_blocks: 1\nconsole = 2\n", "0",
		  INI_EDIT_ADDED, "[esp]\n# vbe_blocks: 1\nvbe_blocks = 0\nconsole = 2\n");
	edit_case("not there: after the section's last line",
		  "[esp]\r\nconsole = 2\r\nenable_usb = 1\r\n\r\n[pc]\r\nhda = x.img\r\n", "0",
		  INI_EDIT_ADDED,
		  "[esp]\r\nconsole = 2\r\nenable_usb = 1\r\nvbe_blocks = 0\r\n\r\n[pc]\r\nhda = x.img\r\n");
	edit_case("not there: an empty [esp]", "[esp]\n[pc]\nhda = x.img\n", "1", INI_EDIT_ADDED,
		  "[esp]\nvbe_blocks = 1\n[pc]\nhda = x.img\n");
	edit_case("not there: the comment's continuation (blank line between) not taken over",
		  "[esp]\nconsole = 2\n;vbe_blocks = 1\n\n  3\n[pc]\n", "0", INI_EDIT_ADDED,
		  "[esp]\nconsole = 2\n;vbe_blocks = 1\n\n  3\nvbe_blocks = 0\n[pc]\n");
	edit_case("not there: a continuation straight after the comment kept before it",
		  "[esp]\nconsole = 2\n;vbe_blocks = 1\n  3\n[pc]\n", "0", INI_EDIT_ADDED,
		  "[esp]\nconsole = 2\n;vbe_blocks = 1\n  3\nvbe_blocks = 0\n[pc]\n");
	edit_case("not there: into the last of two [esp] sections",
		  "[esp]\nconsole = 2\n[pc]\nhda = x.img\n[esp]\nenable_usb = 1\n", "0", INI_EDIT_ADDED,
		  "[esp]\nconsole = 2\n[pc]\nhda = x.img\n[esp]\nenable_usb = 1\nvbe_blocks = 0\n");
	edit_case("not there: [esp] on the last line, no line end", "[pc]\nhda = x.img\n[esp]", "0",
		  INI_EDIT_ADDED, "[pc]\nhda = x.img\n[esp]\nvbe_blocks = 0\n");
	edit_case("not there: a byte order mark before [esp]",
		  "\xef\xbb\xbf[esp]\nconsole = 2\n", "0", INI_EDIT_ADDED,
		  "\xef\xbb\xbf[esp]\nconsole = 2\nvbe_blocks = 0\n");
	edit_case("only in [pc]: left there, [esp] gets its own",
		  "[esp]\nconsole = 2\n[pc]\nvbe_blocks = 1\n", "0", INI_EDIT_ADDED,
		  "[esp]\nconsole = 2\nvbe_blocks = 0\n[pc]\nvbe_blocks = 1\n");
	edit_case("names that only look alike: vbe_blocksX, ; vbe_blocks_old",
		  "[esp]\nvbe_blocksX = 1\n; vbe_blocks_old = 1\n", "0", INI_EDIT_ADDED,
		  "[esp]\nvbe_blocksX = 1\n; vbe_blocks_old = 1\nvbe_blocks = 0\n");

	edit_case("no [esp]: added at the end", "[pc]\nhda = x.img\n", "0", INI_EDIT_NEW_SECTION,
		  "[pc]\nhda = x.img\n\n[esp]\nvbe_blocks = 0\n");
	edit_case("no [esp], CR LF, no line end at the end", "[pc]\r\nhda = x.img", "1",
		  INI_EDIT_NEW_SECTION, "[pc]\r\nhda = x.img\r\n\r\n[esp]\r\nvbe_blocks = 1\r\n");
	edit_case("no [esp]: \"[ esp ]\" is another section to ini.c", "[ esp ]\nvbe_blocks = 1\n", "0",
		  INI_EDIT_NEW_SECTION, "[ esp ]\nvbe_blocks = 1\n\n[esp]\nvbe_blocks = 0\n");
	edit_case("an empty file", "", "0", INI_EDIT_NEW_SECTION, "[esp]\nvbe_blocks = 0\n");

	/* the package's own ini, as Plat has it on his card */
	char path[600];
	size_t len;
	snprintf(path, sizeof(path), "%s/s31/card/tiny386/win95-800x480.ini", tree);
	char *pk = slurp(path, &len);
	if (pk) {
		char want[8192];
		const char *at = strstr(pk, ";vbe_blocks = 1\n");
		CHECK(at != NULL, "the package's ini has its ;vbe_blocks = 1 line");
		if (at) {
			size_t cut = at - pk + strlen(";vbe_blocks = 1\n");
			snprintf(want, sizeof(want), "%.*svbe_blocks = 0\n%s", (int) cut, pk, pk + cut);
			edit_case("the package's win95-800x480.ini: after its ;vbe_blocks = 1", pk, "0",
				  INI_EDIT_ADDED, want);
		}
		free(pk);
	} else {
		printf("  (no %s: the package's ini not tried)\n", path);
	}

	char small[16];
	size_t sl;
	const char *t = "[esp]\nvbe_blocks = 1\n";
	CHECK(ini_edit_set(t, strlen(t), "esp", "vbe_blocks", "0", small, sizeof(small), &sl) ==
	      INI_EDIT_NO_ROOM, "too small an output: INI_EDIT_NO_ROOM");
}

/* ==== 2 and 3. the files ==== */

static char dir[512];

static void put(const char *name, const char *text)
{
	char p[700];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	FILE *f = fopen(p, "wb");
	if (!f) {
		perror(p);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

static bool has(const char *name)
{
	char p[700];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	return access(p, F_OK) == 0;
}

static bool is(const char *name, const char *text)
{
	char p[700];
	size_t len;
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	char *b = slurp(p, &len);
	bool ok = b && len == strlen(text) && !memcmp(b, text, len);
	free(b);
	return ok;
}

static ino_t inode(const char *name)
{
	char p[700];
	struct stat st;
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	return stat(p, &st) == 0 ? st.st_ino : 0;
}

static void clean(void)
{
	static const char *const names[] = {
		"a.ini", "a.ini.t386new", "a.ini.t386old", "b.ini", "b.ini.t386new",
		"b.ini.t386old", "c.ini", "c.ini.t386new", "c.ini.t386old", "d.ini",
		"d.ini.t386new", "d.ini.t386old", "e.ini", "e.ini.t386old", "h.ini",
		"h.ini.t386old", "i.ini", "i.ini.t386new", "keep.ini.old", "big.ini", NULL
	};
	for (int i = 0; names[i]; i++) {
		char p[700];
		snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
		remove(p);
	}
}

/* v4.5: a whole path.t386new as setup.c writes it, made here with a CRC-32
   of the test's own: the text, then "\r\n;t386new LLLLLLLL CCCCCCCC\r\n" */
static uint32_t crc32_test(const char *p, size_t n)
{
	static uint32_t table[256];
	if (!table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = table[(c ^ (uint8_t) p[i]) & 0xFF] ^ (c >> 8);
	return ~c;
}

static void put_whole_new(const char *name, const char *text)
{
	char buf[4096];
	size_t n = strlen(text);
	snprintf(buf, sizeof(buf), "%s\r\n;t386new %08x %08x\r\n", text, (unsigned) n,
		 (unsigned) crc32_test(text, n));
	put(name, buf);
}

static bool s_made_tmp;          /* dir is a short one in TMPDIR, removed at the end */

static void test_files(const char *out)
{
	char p[700], why[400];
	/* under OUT if the folder fits in setup_list.dir from there (the
	   setup_find() check below), else a short one made in TMPDIR (default
	   /tmp), as test_setup.c does */
	if (strlen(out) + strlen("/inisave") < sizeof(((struct setup_list *) 0)->dir)) {
		snprintf(dir, sizeof(dir), "%s/inisave", out);
		mkdir(dir, 0755);
	} else {
		const char *tmp = getenv("TMPDIR");
		snprintf(dir, sizeof(dir), "%s/t386inisave-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
		if (!mkdtemp(dir)) {
			perror(dir);
			exit(2);
		}
		s_made_tmp = true;
		printf("(OUT is too long a path for setup_find(): the files are in %s)\n", dir);
	}
	chmod(dir, 0755);
	clean();

	printf("2. setup_ini_save()\n");
	put("a.ini", "[esp]\nconsole = 2\n;vbe_blocks = 1\n\n[pc]\nhda = x.img\n");
	snprintf(p, sizeof(p), "%s/a.ini", dir);
	ino_t i0 = inode("a.ini");
	int r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r == INI_EDIT_ADDED && !why[0], "save: code %d, why \"%s\"", r, why);
	CHECK(is("a.ini", "[esp]\nconsole = 2\n;vbe_blocks = 1\nvbe_blocks = 0\n\n[pc]\nhda = x.img\n"),
	      "save: the line added after the comment");
	CHECK(!has("a.ini.t386new") && !has("a.ini.t386old"), "save: nothing left beside the ini");
	/* v4.5: rewritten in place, never renamed: the same file */
	CHECK(inode("a.ini") == i0, "save: the ini rewritten in place (the same file), not renamed");
	r = setup_ini_save(p, "esp", "vbe_blocks", "1", why, sizeof(why));
	CHECK(r == INI_EDIT_CHANGED, "save again: code %d", r);
	CHECK(is("a.ini", "[esp]\nconsole = 2\n;vbe_blocks = 1\nvbe_blocks = 1\n\n[pc]\nhda = x.img\n"),
	      "save again: the line changed");
	CHECK(inode("a.ini") == i0, "save again: the same file");
	r = setup_ini_save(p, "esp", "vbe_blocks", "1", why, sizeof(why));
	CHECK(r == INI_EDIT_SAME && inode("a.ini") == i0 && !has("a.ini.t386new"),
	      "the same value: code %d, the same file (nothing written)", r);
	printf("  ok: saved, changed, in place, and not written when it was there already\n");

	/* v4.5: what an earlier save left beside the ini, tidied first */
	put("a.ini.t386new", "half of a c");
	r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r == INI_EDIT_CHANGED && !has("a.ini.t386new") &&
	      is("a.ini", "[esp]\nconsole = 2\n;vbe_blocks = 1\nvbe_blocks = 0\n\n[pc]\nhda = x.img\n"),
	      "a copy cut short beside it: removed, then the save: %d \"%s\"", r, why);
	put_whole_new("a.ini.t386new", "[esp]\nvbe_blocks = 1\npace_quiet = 7\n");
	r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r == INI_EDIT_CHANGED && !has("a.ini.t386new") &&
	      is("a.ini", "[esp]\nvbe_blocks = 0\npace_quiet = 7\n"),
	      "a whole copy beside it: put in place first, then the save from it: %d \"%s\"", r, why);
	put("a.ini.t386old", "[esp]\nvbe_blocks = 9\n");
	r = setup_ini_save(p, "esp", "vbe_blocks", "1", why, sizeof(why));
	CHECK(r == INI_EDIT_CHANGED && !has("a.ini.t386old") &&
	      is("a.ini", "[esp]\nvbe_blocks = 1\npace_quiet = 7\n"),
	      "v4.4's old copy with other bytes beside it: removed, then the save: %d \"%s\"", r, why);
	put("a.ini.t386old", "[esp]\nvbe_blocks = 1\npace_quiet = 7\n");
	r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r < 0 && strstr(why, "chkdsk") && has("a.ini.t386old") &&
	      is("a.ini", "[esp]\nvbe_blocks = 1\npace_quiet = 7\n"),
	      "v4.4's old copy with the ini's very bytes: refused, both left: %d \"%s\"", r, why);
	printf("  ok: leftovers tidied first; refused: %s\n", why);
	clean();

	snprintf(p, sizeof(p), "%s/none.ini", dir);
	r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r < 0 && strstr(why, "cannot be opened"), "no such ini: %d, \"%s\"", r, why);
	CHECK(!has("none.ini"), "no such ini: none made");
	char big[SETUP_INI_MAX + 64];
	memset(big, ';', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	put("big.ini", big);
	snprintf(p, sizeof(p), "%s/big.ini", dir);
	r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
	CHECK(r < 0 && strstr(why, "larger than"), "too large: %d, \"%s\"", r, why);
	CHECK(!has("big.ini.t386new"), "too large: nothing beside it");
	if (geteuid() != 0) {
		put("b.ini", "[esp]\nvbe_blocks = 1\n");
		chmod(dir, 0555);
		snprintf(p, sizeof(p), "%s/b.ini", dir);
		r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
		chmod(dir, 0755);
		CHECK(r < 0 && why[0], "a folder that cannot be written: %d, \"%s\"", r, why);
		CHECK(is("b.ini", "[esp]\nvbe_blocks = 1\n") && !has("b.ini.t386new") &&
		      !has("b.ini.t386old"), "a folder that cannot be written: the ini as it was");
		printf("  ok: errors: %s\n", why);
	} else {
		printf("  (run as root: the folder that cannot be written not tried)\n");
	}
	clean();

	/* v4.5: a cut in power at each step of a save (setup_test_cut), then
	   the next start's tidy: the ini whole, old or new, and nothing left
	   beside it; setup_find() lists it */
	printf("2b. a cut at each step of a save, then the next start\n");
	static const char *const t0 = "; cut test\n[esp]\nconsole = 2\nvbe_blocks = 1\n\n[pc]\nhda = x.img\n";
	static const char *const t1 = "; cut test\n[esp]\nconsole = 2\nvbe_blocks = 0\n\n[pc]\nhda = x.img\n";
	for (int step = 1; step <= 5; step++) {
		put("c.ini", t0);
		snprintf(p, sizeof(p), "%s/c.ini", dir);
		setup_test_cut = step;
		r = setup_ini_save(p, "esp", "vbe_blocks", "0", why, sizeof(why));
		setup_test_cut = 0;
		CHECK(r == -2, "cut at step %d: the save stopped (%d)", step, r);
		/* what the card holds after the cut */
		bool before = is("c.ini", t0), after = is("c.ini", t1);
		bool copy = has("c.ini.t386new");
		CHECK(copy, "cut at step %d: the copy is beside the ini", step);
		if (step <= 2)
			CHECK(before, "cut at step %d: the ini not yet touched", step);
		if (step == 5)
			CHECK(after, "cut at step 5: the ini already new");
		if (step == 3 || step == 4)
			CHECK(!before && !after, "cut at step %d: the ini neither (emptied or half written)", step);
		int acts = setup_ini_tidy(dir);
		const char *want = step == 1 ? t0 : t1;
		CHECK(is("c.ini", want) && !has("c.ini.t386new") && !has("c.ini.t386old"),
		      "cut at step %d, then the tidy: the ini %s, nothing beside it (%d acts)",
		      step, step == 1 ? "old" : "new", acts);
		CHECK(!setup_tidy_warning()[0], "cut at step %d: no warning", step);
		struct setup_place places[] = { { dir, NULL }, { NULL, NULL } };
		struct setup_list *l = malloc(sizeof(*l));
		int n = setup_find(places, l);
		CHECK(n == 1 && !strcmp(l->ini[0].name, "c.ini") && !strcmp(l->ini[0].desc, "cut test"),
		      "cut at step %d: setup_find() lists c.ini (%d inis)", step, n);
		free(l);
		printf("  ok: cut at step %d: the ini %s after the next start\n", step,
		       step == 1 ? "as it was" : "saved");
	}
	clean();

	printf("3. setup_ini_tidy()\n");
	/* v4.5 cut while the copy was written, or v4.4 before its first
	   rename: X and a copy that is not whole */
	put("a.ini", "old a\n");
	put("a.ini.t386new", "half of the n");
	/* v4.5 cut while the ini was rewritten: X (not yet new) and a whole
	   copy */
	put("b.ini", "old b, or half of the new one");
	put_whole_new("b.ini.t386new", "new b\n");
	/* v4.5 cut before the copy was removed: X new, and the whole copy */
	put("c.ini", "new c\n");
	put_whole_new("c.ini.t386new", "new c\n");
	/* v4.4 cut between its two renames: X.t386old and X.t386new */
	put("d.ini.t386old", "old d\n");
	put("d.ini.t386new", "new d\n");
	/* v4.4 cut before the old one was removed: X and X.t386old, other
	   bytes */
	put("e.ini", "new e\n");
	put("e.ini.t386old", "old e\n");
	/* v4.4: X.t386old alone */
	put("h.ini.t386old", "old h\n");
	/* X.t386new alone: left */
	put("i.ini.t386new", "who knows\n");
	/* a file of Plat's own with a name like it */
	put("keep.ini.old", "mine\n");
	int acts = setup_ini_tidy(dir);
	CHECK(is("a.ini", "old a\n") && !has("a.ini.t386new"), "X + a copy not whole: the copy removed");
	CHECK(is("b.ini", "new b\n") && !has("b.ini.t386new"),
	      "X + a whole copy: X made the copy's text, the copy removed");
	CHECK(is("c.ini", "new c\n") && !has("c.ini.t386new"), "X already new + a whole copy: the copy removed");
	CHECK(is("d.ini", "new d\n") && !has("d.ini.t386new") && !has("d.ini.t386old"),
	      "v4.4's X.t386old + X.t386new: X written with the new text, both removed");
	CHECK(is("e.ini", "new e\n") && !has("e.ini.t386old"), "v4.4's X + X.t386old: the old one removed");
	CHECK(is("h.ini", "old h\n") && !has("h.ini.t386old"), "v4.4's X.t386old alone: its bytes back as X");
	CHECK(is("i.ini.t386new", "who knows\n") && !has("i.ini"), "X.t386new alone: left");
	CHECK(is("keep.ini.old", "mine\n"), "another file left alone");
	CHECK(acts == 10, "10 files written or removed (%d)", acts);
	CHECK(!setup_tidy_warning()[0], "no warning (\"%s\")", setup_tidy_warning());
	CHECK(setup_ini_tidy(dir) == 0, "a second tidy does nothing");
	clean();

	/* v4.4, a cut inside a rename: what may be two names on one chain is
	   never removed */
	put("a.ini", "the same\n");
	put("a.ini.t386old", "the same\n");
	struct setup_place places[] = { { dir, NULL }, { NULL, NULL } };
	struct setup_list *l = malloc(sizeof(*l));
	int n = setup_find(places, l);
	CHECK(n == 1 && is("a.ini", "the same\n") && is("a.ini.t386old", "the same\n") &&
	      strstr(setup_tidy_warning(), "chkdsk") && strstr(setup_tidy_warning(), "a.ini"),
	      "X + X.t386old with the same bytes: both left, a warning (\"%s\")", setup_tidy_warning());
	printf("  ok: the same bytes: %s\n", setup_tidy_warning());
	clean();
	put("b.ini", "x\n");
	put("b.ini.t386old", "o\n");
	put_whole_new("b.ini.t386new", "n\n");
	n = setup_find(places, l);
	CHECK(n == 1 && is("b.ini", "x\n") && is("b.ini.t386old", "o\n") && has("b.ini.t386new") &&
	      strstr(setup_tidy_warning(), "chkdsk") && strstr(setup_tidy_warning(), "b.ini"),
	      "all three: left, a warning (\"%s\")", setup_tidy_warning());
	printf("  ok: all three: %s\n", setup_tidy_warning());
	clean();
	put("b.ini.t386old", "old b\n");
	acts = setup_ini_tidy(dir);
	CHECK(is("b.ini", "old b\n") && !has("b.ini.t386old") && acts == 2,
	      "X.t386old alone: back to X (%d)", acts);
	clean();

	/* setup_find() tidies first: an ini a cut left as X.t386new and
	   X.t386old is listed */
	put("a.ini", "; first\n[pc]\n");
	put("b.ini.t386old", "; old b\n[pc]\n");
	put("b.ini.t386new", "; new b\n[pc]\n");
	n = setup_find(places, l);
	CHECK(n == 2 && !strcmp(l->ini[1].name, "b.ini") && !strcmp(l->ini[1].desc, "new b") &&
	      !setup_tidy_warning()[0],
	      "setup_find() lists b.ini put back from its .t386new (%d inis)", n);
	free(l);
	clean();
	if (s_made_tmp)
		rmdir(dir);
	printf("  ok: every state put right, other files left alone\n");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_ini_edit OUT TREE\n");
		return 2;
	}
	test_edit(argv[2]);
	test_files(argv[1]);
	printf("test_ini_edit: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
