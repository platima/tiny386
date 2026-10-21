#ifndef TERM_H
#define TERM_H

typedef struct Term Term;
typedef struct VGAState VGAState;
Term *term_init(
	VGAState *vga,
	void (*kbd_cb)(void *, unsigned char scan_code, int is_pressed),
	void *kbd);
void term_step(Term *s);

/* If set, called when Ctrl+] is typed, and the key goes no further. */
extern void (*term_quit_key)(void);


#endif /* TERM_H */
