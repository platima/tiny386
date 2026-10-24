/*
 * One setting of an ini file changed in its text, every other byte left
 * as it was (v4.4: Ctrl+Alt+F11 saves vbe_blocks in the ini the PC was
 * started from).  Plain C, no ESP-IDF: tests/host/test_ini_edit.c runs it
 * on the PC.
 *
 * The lines are read as tiny386's ini.c (inih, with its settings in ini.h)
 * reads them: ';' and '#' begin a comment line, an inline comment is a ';'
 * after a space, "name = value" or "name: value", a line that begins with
 * a space after a setting continues that setting's value, [section] names
 * are taken as they are written (case and spaces count), and a UTF-8 byte
 * order mark may begin the file.
 */
#ifndef INI_EDIT_H
#define INI_EDIT_H

#include <stddef.h>

enum {
	INI_EDIT_SAME = 0,              /* the setting had that value already */
	INI_EDIT_CHANGED = 1,           /* its line (or lines) changed */
	INI_EDIT_ADDED = 2,             /* a line added to the section */
	INI_EDIT_NEW_SECTION = 3,       /* the section and the line added at the end */
	INI_EDIT_NO_ROOM = -1,          /* out is too small */
};

/*
 * name = value in [section] of the ini text in (inlen bytes, no NUL needed)
 * into out (outsize bytes; 2 * inlen + 256 is always enough), its length
 * in *outlen.  Where the setting is, the value on each of its lines is
 * replaced and the rest of the line kept (spaces, an inline comment, the
 * line's end); continuation lines under it go.  Where it is not, a line
 * "name = value" goes into the last [section] of that name: after a
 * comment line there that begins with the name (";vbe_blocks = 1", the
 * package's inis), else after the section's last line that is not blank.
 * With no such section, the section and the line go at the end.  New
 * lines end as the file's first line does (CR LF or LF; LF in a file of
 * one line).  Returns one of the codes above; with INI_EDIT_SAME out holds
 * the text unchanged.
 */
int ini_edit_set(const char *in, size_t inlen, const char *section, const char *name,
		 const char *value, char *out, size_t outsize, size_t *outlen);

#endif /* INI_EDIT_H */
