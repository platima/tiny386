/*
 * Which ini to start from, and the files it names.
 *
 * The ini files are looked for in a list of places, in order: on the card,
 * every .ini in /sdcard/tiny386/ (the folder the Linux route uses), then
 * /sdcard/tiny386.ini, then /spiflash/tiny386.ini.  The first place with any
 * gives the list; with more than one in it, the boot menu offers them.
 *
 * A file named in an ini (bios, vga_bios, the disks, ...) that does not
 * start with / is taken from the ini's own folder, so the Linux route's
 * inis ("bios = bios.bin", "hda = freedos-hd.img") work as they are.  The
 * Linux route's mount points are mapped to this firmware's: /mnt/sd/... is
 * the card's first FAT partition, /mnt/win/... the WIN9X partition.
 *
 * The choice made in the menu is kept in lastboot.txt beside the inis, and
 * is the default next time.
 *
 * Plain POSIX: tests/host/test_setup.c builds it on a PC.
 */
#ifndef SETUP_H
#define SETUP_H

#include <stdbool.h>
#include <stddef.h>

#define SETUP_MAX_INIS 16
#define SETUP_NAME_MAX 64
#define SETUP_DESC_MAX 72
#define SETUP_LAST_FILE "lastboot.txt"

struct setup_place {
	const char *dir;        /* "/sdcard/tiny386" */
	const char *only;       /* that one name, or NULL for every .ini */
};

struct setup_ini {
	char name[SETUP_NAME_MAX];      /* "freedos.ini" */
	char desc[SETUP_DESC_MAX];      /* its first comment line, or "" */
};

struct setup_list {
	char dir[96];
	int n;                          /* inis listed, sorted by name */
	int more;                       /* found beyond SETUP_MAX_INIS */
	struct setup_ini ini[SETUP_MAX_INIS];
};

/* The first place (the list ends with dir NULL) that has an ini: its inis.
   Returns how many (0: none anywhere). */
int setup_find(const struct setup_place *places, struct setup_list *out);

/* dir/name of entry i */
void setup_path(const struct setup_list *l, int i, char *out, size_t size);

/* The entry to start without a choice: the one named in lastboot.txt if
   it is still there, else tiny386.ini, else freedos.ini, else the first. */
int setup_default(const struct setup_list *l, const char *last);

/* lastboot.txt in dir: the name of the ini chosen last.  false if none. */
bool setup_last_read(const char *dir, char *name, size_t size);
/* Writes it, only if it differs from what is there.  false on an error. */
bool setup_last_write(const char *dir, const char *name);

struct setup_map {
	const char *from;       /* "/mnt/sd" */
	const char *to;         /* "/sdcard", or NULL: nothing there */
};

/* A file named in an ini: relative to dir, or an absolute path with a
   mapped prefix swapped.  Returns out.  *mapped_missing is set when the
   path starts with a mapped prefix whose target is NULL (the WIN9X
   partition is not on this card). */
const char *setup_resolve(const char *dir, const char *value,
			  const struct setup_map *maps, int nmaps,
			  char *out, size_t size, bool *mapped_missing);

/* true for the ini keys that name a file */
bool setup_is_path_key(const char *section, const char *name);

/* v4.4: the largest ini setup_ini_save() rewrites */
#define SETUP_INI_MAX (32 * 1024)
/* the names an ini's rewrite goes through beside it (path + suffix) */
#define SETUP_NEW_SUFFIX ".t386new"
#define SETUP_OLD_SUFFIX ".t386old"

/* v4.4 (Ctrl+Alt+F11 saves vbe_blocks): [section] name = value in the ini
   at path, every other byte of it kept (ini_edit.c), written so that a cut
   in power leaves either the old ini or the new one: the new text into
   path.t386new, flushed, synced and read back; then path renamed to
   path.t386old, path.t386new to path, and path.t386old removed
   (setup_ini_tidy() puts right what a cut part way leaves).  Nothing is
   written when the ini has that value already.  Returns an INI_EDIT_ code
   (ini_edit.h), or -1 with the reason in why. */
int setup_ini_save(const char *path, const char *section, const char *name,
		   const char *value, char *why, size_t whylen);

/* What an interrupted setup_ini_save() left in dir, put right: X.t386new
   beside X (the new text not yet in place) removed; X.t386old beside X
   (the old one not yet removed) removed; X.t386new and X.t386old with no X
   (cut between the two renames: the new text was whole, it was read back)
   X.t386new renamed to X and X.t386old removed; X.t386old alone renamed
   back to X.  Only names with these two endings are touched.  setup_find()
   calls it for each folder it looks in.  Returns how many files it
   renamed or removed. */
int setup_ini_tidy(const char *dir);

#endif /* SETUP_H */
