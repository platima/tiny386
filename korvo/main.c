/*
 * tiny386 on a small board whose only screen is the Linux frame buffer
 * console.  Written for the ESP32-S31-Korvo-1 running esp32-s31-linux: one
 * RV32 processor, 16 MB of memory in all, an 800x480 RGB565 frame buffer
 * with one mode, and a keyboard and a shell on tty1 as the only way in.
 * Nothing in it is specific to that board.
 *
 * Next to sdl/main.c and main.c, which it is made from, this front end
 *
 *  - draws the guest's picture straight into the frame buffer (no second
 *    copy of the screen, no conversion on the way), and spends no more
 *    than a set part of its time doing so;
 *  - can also show the guest's text screen on the terminal it was started
 *    from (-term), or nothing at all (-none);
 *  - starts a console guard first, which puts the terminal back if the
 *    emulator is killed outright (see guard.c);
 *  - looks at how much memory the machine has before it takes any;
 *  - quits on Ctrl+Alt+Backspace (Ctrl+] with -term);
 *  - asks the kernel to put what the guest wrote to its disks on the card
 *    once the guest has stopped writing for a while;
 *  - says at the end how many instructions it emulated and how fast.
 *
 * Offered under the licence of tiny386 (BSD 3-clause, see LICENSE).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "pc.h"
#include "term.h"
#include "korvo/guard.h"
#include "korvo/hostbench.h"
#ifndef KORVO_NO_SDL
#include "SDL.h"
#endif

enum { MODE_SDL, MODE_TERM, MODE_NONE };

static int mode = MODE_SDL;
static volatile sig_atomic_t running = 1;

/*
 * Platform HAL
 */

uint32_t get_uticks()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint32_t) ts.tv_sec * 1000000 +
		(uint32_t) ts.tv_nsec / 1000);
}

static void text_mode(void);

/* What went wrong.  It is said by finish(), on the way out, when the screen
   and the terminal are what they were: anything printed before that is lost
   with the picture or with the terminal's other screen. */
static char last_words[512];

static void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(last_words, sizeof(last_words), fmt, ap);
	va_end(ap);
	text_mode();
	exit(1);
}

void *bigmalloc(size_t size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		fatal("no memory for %lu KiB: %s",
		      (unsigned long) (size >> 10), strerror(errno));
	return p;
}

static unsigned long guest_mem;	/* bytes, for load_rom() */

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
	FILE *fp = fopen(file, "rb");
	if (fp == NULL)
		fatal("%s: %s", file, strerror(errno));

	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	fprintf(stderr, "load_rom: %s, len %d\n", file, len);
	if (len < 0 || addr > guest_mem ||
	    (backward ? (unsigned long) len > addr
		      : (unsigned long) len > guest_mem - addr))
		fatal("%s: its %d bytes do not fit in the guest's memory at 0x%lx",
		      file, len, (unsigned long) addr);
	rewind(fp);
	if (backward)
		fread((char *) phys_mem + addr - len, 1, len, fp);
	else
		fread((char *) phys_mem + addr, 1, len, fp);
	fclose(fp);
	return len;
}

/*
 * Messages.  While the picture is on the screen, the terminal under it
 * takes what is printed and shows it when it is back in text mode: the
 * emulator's notes about ports nobody answers, on top of each other.  And
 * with -term the terminal is the guest's screen.  So stdout and stderr go
 * to a file or nowhere until the end.
 */

static int saved_out = -1, saved_err = -1;

static void hide_messages(const char *logfile)
{
	int fd;

	if (logfile)
		fd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
	else
		fd = open("/dev/null", O_WRONLY);
	if (fd < 0)
		fatal("%s: %s", logfile ? logfile : "/dev/null", strerror(errno));
	fflush(stdout);
	fflush(stderr);
	saved_err = dup(2);
	dup2(fd, 2);
	if (mode != MODE_TERM) {
		saved_out = dup(1);
		dup2(fd, 1);
	}
	close(fd);
}

static void show_messages(void)
{
	fflush(stdout);
	fflush(stderr);
	if (saved_out >= 0) {
		dup2(saved_out, 1);
		close(saved_out);
		saved_out = -1;
	}
	if (saved_err >= 0) {
		dup2(saved_err, 2);
		close(saved_err);
		saved_err = -1;
	}
}

/*
 * How much memory there is.  The guest's memory is touched from end to end
 * when the emulator starts, so what is asked for here is what is used, and
 * a machine that does not have it either swaps (far too slow to use) or
 * kills something: most likely this.
 */

static long meminfo_kb(const char *key)
{
	char line[128];
	size_t n = strlen(key);
	long val = -1;
	/* TINY386_MEMINFO names a file to read in its place: for trying out
	   what a machine with little memory is told */
	const char *path = getenv("TINY386_MEMINFO");
	FILE *f = fopen(path ? path : "/proc/meminfo", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, key, n) && line[n] == ':') {
			val = atol(line + n + 1);
			break;
		}
	}
	fclose(f);
	return val;
}

static long status_kb(const char *key)
{
	char line[128];
	size_t n = strlen(key);
	long val = -1;
	FILE *f = fopen("/proc/self/status", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, key, n) && line[n] == ':') {
			val = atol(line + n + 1);
			break;
		}
	}
	fclose(f);
	return val;
}

/* what the emulator needs besides the guest's memory: its own data and the
   part of its code that is in use.  Measured, see the notes that come with
   this build. */
#define OWN_KB 1024

static void check_memory(const PCConfig *conf, int force)
{
	long guest = (conf->mem_size + conf->vga_mem_size) >> 10;
	long avail = meminfo_kb("MemAvailable");
	long swap = meminfo_kb("SwapFree");

	fprintf(stderr, "tiny386: guest memory %ld KiB + video memory %ld KiB"
		" = %ld KiB, and about %d KiB for the emulator\n",
		conf->mem_size >> 10, conf->vga_mem_size >> 10, guest, OWN_KB);
	if (avail < 0) {
		fprintf(stderr, "tiny386: cannot tell how much memory is free"
			" (no /proc/meminfo)\n");
		return;
	}
	fprintf(stderr, "tiny386: this machine has %ld KiB available and"
		" %ld KiB of swap free\n", avail, swap < 0 ? 0 : swap);
	if (guest + OWN_KB <= avail)
		return;
	if (force) {
		fprintf(stderr, "tiny386: that is not enough: going on because"
			" of -force\n");
		return;
	}
	text_mode();
	fprintf(stderr, "tiny386: that is not enough. The emulator would run"
		" out of the swap file, far too\nslowly to use, or be killed."
		" Give the guest less (mem_size, vga_mem_size), stop\nsomething"
		" else, or say -force.\n");
	exit(1);
}

static void check_file(const char *what, const char *path, int writing)
{
	if (!path || !path[0])
		return;
	if (access(path, writing ? R_OK | W_OK : R_OK) != 0)
		fatal("%s = %s: %s", what, path, strerror(errno));
}

static void check_floppy(const char *what, const char *path)
{
	struct stat st;

	if (!path || !path[0])
		return;
	check_file(what, path, 1);
	if (stat(path, &st) == 0 && !emulink_floppy_size_ok(st.st_size))
		fatal("%s = %s: %ld bytes is not the size of any floppy",
		      what, path, (long) st.st_size);
}

/* Everything the configuration names, before anything is done to the
   screen: after that an error message cannot be read. */
static void check_config(const PCConfig *conf)
{
	static const char *const hd[4] = { "hda", "hdb", "hdc", "hdd" };
	static const char *const cd[4] = { "cda", "cdb", "cdc", "cdd" };
	int i;

	if (conf->mem_size < 1024 * 1024 || conf->mem_size > 1024L * 1024 * 1024)
		fatal("mem_size = %ld: not between 1M and 1G", conf->mem_size);
	if (conf->vga_mem_size < 256 * 1024 ||
	    conf->vga_mem_size > 16 * 1024 * 1024)
		fatal("vga_mem_size = %ld: not between 256K and 16M",
		      conf->vga_mem_size);
	if (conf->width < 320 || conf->height < 200 ||
	    conf->width > 2048 || conf->height > 2048)
		fatal("[display] %dx%d: not a size this can use",
		      conf->width, conf->height);
	if (conf->cpu_gen < 3 || conf->cpu_gen > 6)
		fatal("[cpu] gen = %d: 3, 4, 5 or 6", conf->cpu_gen);
	/* tiny386's serial port is the emulator's standard input, in raw
	   mode, read a byte at a time by the guest: on a board whose keyboard
	   is that same terminal it would take every key, the ones that end
	   the emulator among them */
	if (conf->enable_serial)
		fatal("enable_serial = 1: this front end has no serial port to give");

	if (conf->kernel && conf->kernel[0]) {
		check_file("kernel", conf->kernel, 0);
		check_file("initrd", conf->initrd, 0);
		check_file("linuxstart",
			   conf->linuxstart ? conf->linuxstart : "linuxstart.bin", 0);
	}
	/* pc.c takes these two names when none is given */
	check_file("bios", conf->bios ? conf->bios : "bios.bin", 0);
	check_file("vga_bios", conf->vga_bios ? conf->vga_bios : "vgabios.bin", 0);
	for (i = 0; i < 4; i++)
		check_file(conf->iscd[i] ? cd[i] : hd[i], conf->disks[i],
			   !conf->iscd[i]);
	check_floppy("fda", conf->fdd[0]);
	check_floppy("fdb", conf->fdd[1]);
}

/*
 * The screen through SDL
 */

#ifndef KORVO_NO_SDL

static SDL_Surface *screen;
static SDL_Surface *shadow;	/* only where the screen cannot be drawn on directly */
static int sdl_up;
static int keys_are_evdev;	/* scancodes are the kernel's key codes */

static void text_mode(void)
{
	if (sdl_up) {
		sdl_up = 0;
		SDL_Quit();
	}
	show_messages();
}

/*
 * Returns the memory the VGA is to draw in: `width` pixels of BPP bits to a
 * row, rows one after the other.  On the frame buffer console that is the
 * frame buffer itself, and the picture is on the glass the moment it is
 * drawn.  The size asked for has to be the size of the screen then: the
 * VGA takes a row to be `width` pixels long, and a smaller picture, which
 * SDL would centre, has rows as long as the screen's.  The VGA centres
 * what the guest shows inside this by itself.
 */
static uint8_t *screen_open(PCConfig *conf)
{
	const SDL_VideoInfo *info;
	char driver[32] = "";
	SDL_PixelFormat *f;
	int direct;

	/* the driver then says on stderr which terminal, mouse and mode it
	   uses, before it leaves text mode */
	setenv("SDL_FBCON_VERBOSE", "1", 0);

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		fatal("SDL: %s", SDL_GetError());
	sdl_up = 1;
	SDL_VideoDriverName(driver, sizeof(driver));
	keys_are_evdev = !strcmp(driver, "fbcon");

	info = SDL_GetVideoInfo();
	if (keys_are_evdev && info && info->current_w > 0 && info->current_h > 0 &&
	    (info->current_w != conf->width || info->current_h != conf->height)) {
		fprintf(stderr, "tiny386: the screen is %dx%d: using that, not"
			" the %dx%d of [display]\n", info->current_w,
			info->current_h, conf->width, conf->height);
		conf->width = info->current_w;
		conf->height = info->current_h;
	}

	screen = SDL_SetVideoMode(conf->width, conf->height, BPP, SDL_HWSURFACE);
	if (!screen)
		fatal("SDL: no %dx%d screen with %d bits a pixel: %s",
		      conf->width, conf->height, BPP, SDL_GetError());
	SDL_ShowCursor(SDL_DISABLE);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
			    SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_WM_SetCaption("tiny386", NULL);

	f = screen->format;
	direct = !SDL_MUSTLOCK(screen) || (screen->flags & SDL_HWSURFACE);
	direct = direct && screen->offset == 0 &&
		screen->w == conf->width && screen->h == conf->height &&
		screen->pitch == conf->width * (BPP / 8) &&
		f->BitsPerPixel == BPP &&
#if BPP == 16
		f->Rmask == 0xf800 && f->Gmask == 0x07e0 && f->Bmask == 0x001f;
#else
		f->Rmask == 0xff0000 && f->Gmask == 0x00ff00 && f->Bmask == 0x0000ff;
#endif
	if (direct)
		return screen->pixels;

	/* Some other screen.  The VGA draws in memory of its own, and SDL
	   copies and converts that. */
	shadow = SDL_CreateRGBSurfaceFrom(
		bigmalloc(conf->width * conf->height * (BPP / 8)),
		conf->width, conf->height, BPP, conf->width * (BPP / 8),
#if BPP == 16
		0xf800, 0x07e0, 0x001f, 0);
#else
		0xff0000, 0x00ff00, 0x0000ff, 0);
#endif
	if (!shadow)
		fatal("SDL: %s", SDL_GetError());
	return shadow->pixels;
}

static void redraw(void *opaque, int x, int y, int w, int h)
{
	if (shadow)
		SDL_BlitSurface(shadow, NULL, screen, NULL);
	/* nothing to do for a screen that is the frame buffer itself */
	if (!(screen->flags & SDL_HWSURFACE))
		SDL_UpdateRect(screen, 0, 0, 0, 0);
}

/* Return 0 if no keycode found.  What comes out is a key code of the Linux
   input layer, which is what the PS/2 keyboard takes. */
static int sdl_get_keycode(const SDL_KeyboardEvent *ev)
{
	int keycode = ev->keysym.scancode;
	int sym = ev->keysym.sym;

	if (keys_are_evdev) {
		/* on the frame buffer console SDL reads the keyboard in
		   medium raw mode, and the scancode is the kernel's key
		   code as it is */
		return keycode < 128 ? keycode : 0;
	}
	switch (sym) {
	case SDLK_UP: return 0x67;
	case SDLK_DOWN: return 0x6c;
	case SDLK_LEFT: return 0x69;
	case SDLK_RIGHT: return 0x6a;
	case SDLK_HOME: return 0x66;
	case SDLK_END: return 0x6b;
	case SDLK_PAGEUP: return 0x68;
	case SDLK_PAGEDOWN: return 0x6d;
	case SDLK_INSERT: return 0x6e;
	case SDLK_DELETE: return 0x6f;
	case SDLK_KP_DIVIDE: return 0x62;
	case SDLK_KP_ENTER: return 0x60;
	case SDLK_RCTRL: return 0x61;
	case SDLK_PAUSE: return 0x77;
	case SDLK_PRINT: return 0x63;
	case SDLK_RALT: return 0x64;
	default: if (keycode == 0) return 0;
	}
	/* an X server with a PC keyboard */
	if (keycode < 9) {
		keycode = 0;
	} else if (keycode < 127 + 9) {
		keycode -= 8;
	} else {
		keycode = 0;
	}
	return keycode;
}

#define KEYCODE_MAX 127
static uint8_t key_pressed[KEYCODE_MAX + 1];

/* release all pressed keys */
static void sdl_reset_keys(PC *pc)
{
	int i;

	for (i = 1; i <= KEYCODE_MAX; i++) {
		if (key_pressed[i]) {
			ps2_put_keycode(pc->kbd, 0, i);
			key_pressed[i] = 0;
		}
	}
}

static void sdl_handle_key_event(const SDL_KeyboardEvent *ev, PC *pc)
{
	int keycode, keypress;

	keycode = sdl_get_keycode(ev);
	if (keycode) {
		if (keycode == 0x3a || keycode == 0x45) {
			/* SDL does not generate key up for numlock & caps lock */
			ps2_put_keycode(pc->kbd, 1, keycode);
			ps2_put_keycode(pc->kbd, 0, keycode);
		} else {
			keypress = (ev->type == SDL_KEYDOWN);
			if (keycode <= KEYCODE_MAX)
				key_pressed[keycode] = keypress;
			ps2_put_keycode(pc->kbd, keypress, keycode);
		}
	} else if (ev->type == SDL_KEYUP) {
		sdl_reset_keys(pc);
	}
}

static void sdl_send_mouse(PC *pc, int dx, int dy, int dz, int state)
{
	int buttons = 0;

	if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
		buttons |= (1 << 0);
	if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
		buttons |= (1 << 1);
	if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
		buttons |= (1 << 2);
	ps2_mouse_event(pc->mouse, dx, dy, dz, buttons);
}

static void sdl_poll(PC *pc)
{
	SDL_Event ev;
	int keycode, state, dz;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_KEYDOWN:
			keycode = sdl_get_keycode(&(ev.key));
			/* Ctrl+Alt+Backspace: the way out.  Nothing a guest
			   is likely to want, and the keyboard has no other
			   key that gets past the guest. */
			if (keycode == 0x0e &&
			    (key_pressed[0x1d] || key_pressed[0x61]) &&
			    (key_pressed[0x38] || key_pressed[0x64])) {
				running = 0;
				break;
			}
			/* fall through */
		case SDL_KEYUP:
			sdl_handle_key_event(&(ev.key), pc);
			break;
		case SDL_MOUSEMOTION:
			sdl_send_mouse(pc, ev.motion.xrel, ev.motion.yrel, 0,
				       ev.motion.state);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			dz = 0;
			if (ev.type == SDL_MOUSEBUTTONDOWN) {
				if (ev.button.button == SDL_BUTTON_WHEELUP)
					dz = -1;
				else if (ev.button.button == SDL_BUTTON_WHEELDOWN)
					dz = 1;
			}
			state = SDL_GetMouseState(NULL, NULL);
			/* just in case */
			if (ev.type == SDL_MOUSEBUTTONDOWN)
				state |= SDL_BUTTON(ev.button.button);
			else
				state &= ~SDL_BUTTON(ev.button.button);
			sdl_send_mouse(pc, 0, 0, dz, state);
			break;
		case SDL_QUIT:
			running = 0;
			break;
		}
	}
}

#else /* KORVO_NO_SDL */

static void text_mode(void)
{
	show_messages();
}

#endif

static void no_redraw(void *opaque, int x, int y, int w, int h)
{
}

/*
 * The terminal, with -term
 */

static void term_put_key(void *o, unsigned char scan_code, int is_pressed)
{
	ps2_put_keycode(o, is_pressed, scan_code);
}

static void quit_key(void)
{
	running = 0;
}

static void on_signal(int sig)
{
	running = 0;
}

/*
 * Counting
 */

static struct {
	uint64_t insns;		/* guest instructions carried out */
	uint64_t run_us;	/* while carrying them out */
	uint64_t halt_us;	/* while the guest waited for an interrupt */
	uint64_t draw_us;	/* putting pictures on the screen */
	uint64_t sync_us;	/* waiting for the card */
	uint64_t wall_us;
	unsigned draws, syncs;
	uint32_t draw_max;
} count;

/*
 * -marks: a guest that says where it is in its work is timed from one mark
 * to the next.  The marks are 0xA1 to 0xAE in the byte at 0000:04F0 (the
 * BIOS's "intra-application communication area"), and 0xAF, which ends the
 * emulator.  BENCH.COM on the FreeDOS image of this build is such a guest:
 * tests/guest/bench.S.
 */
#define MAXMARKS 16
static struct {
	int mark;
	uint64_t insns, us;
} marks[MAXMARKS];
static int nmarks, watch_marks;

static const char *const mark_names[16] = {
	[1] = "16-bit arithmetic in registers",
	[2] = "16-bit loads and stores",
	[3] = "16-bit calls and stack",
	[4] = "rep movsw and stosw, 32 MB",
	[5] = "DOS calls (int 21h)",
	[6] = "32-bit arithmetic, paging on",
	[7] = "32-bit loads and stores, paging on",
	[8] = "32-bit calls and stack, paging on",
	[9] = "translation cache miss every time",
};

static void print_marks(void)
{
	int i;

	for (i = 0; i < nmarks; i++) {
		const char *name = mark_names[marks[i].mark & 15];
		double s = marks[i].us / 1e6;

		if (marks[i].mark == 0xa4) {
			/* a few thousand instructions that move and fill
			   1000 times 32 KB: the bytes are what count */
			fprintf(stderr, "tiny386: %02x %-34s  32768000 bytes        in %7.2f s:"
				" %5.0f thousand a second\n", marks[i].mark, name, s,
				s > 0 ? 32768.0 / s : 0.0);
			continue;
		}
		fprintf(stderr, "tiny386: %02x %-34s %9llu instructions in %7.2f s:"
			" %5.0f thousand a second\n", marks[i].mark,
			name ? name : "", (unsigned long long) marks[i].insns, s,
			s > 0 ? marks[i].insns / 1e3 / s : 0.0);
	}
}

static void print_counts(const PCConfig *conf, long avail_before)
{
	double run = count.run_us / 1e6;
	long hwm = status_kb("VmHWM"), swapped = status_kb("VmSwap");
	long avail = meminfo_kb("MemAvailable");

	print_marks();
	fprintf(stderr, "tiny386: %llu instructions in %.1f s of running:"
		" %.3f million a second\n",
		(unsigned long long) count.insns, run,
		run > 0 ? count.insns / 1e6 / run : 0.0);
	fprintf(stderr, "tiny386: beside that, %.1f s for %u pictures"
		" (%.1f ms each, %.1f at most), %.1f s with the guest halted,"
		" %.1f s in %u waits for the card\n",
		count.draw_us / 1e6, count.draws,
		count.draws ? count.draw_us / 1e3 / count.draws : 0.0,
		count.draw_max / 1e3, count.halt_us / 1e6,
		count.sync_us / 1e6, count.syncs);
	fprintf(stderr, "tiny386: %.1f s in all\n", count.wall_us / 1e6);
	if (hwm > 0)
		fprintf(stderr, "tiny386: most memory this process had in use:"
			" %ld KiB, of which the guest's %ld; %ld KiB of it"
			" was in the swap file at the end\n", hwm,
			(conf->mem_size + conf->vga_mem_size) >> 10,
			swapped < 0 ? 0 : swapped);
	if (avail >= 0 && avail_before >= 0)
		fprintf(stderr, "tiny386: available memory was %ld KiB before"
			" and is %ld KiB now, while this is still running\n",
			avail_before, avail);
}

/*
 * The way out, whatever the way: registered before anything else that
 * registers something, so it runs after all of that, when term.c has put
 * the terminal back.  What the guest wrote to its disks goes to the card,
 * the screen goes back to text, and then what there is to say is said.
 */
static PC *the_pc;
static const PCConfig *the_conf;
static long avail_before = -1;
static int term_up;

static void finish(void)
{
	if (the_pc) {
		ide_flush();
		emulink_flush(the_pc->emulink);
	}
	text_mode();
	if (term_up) {
		/* the last line of the terminal, wherever the guest's
		   cursor was */
		fputs("\033[0m\033[999;1H\n", stderr);
	}
	if (last_words[0])
		fprintf(stderr, "tiny386: %s\n", last_words);
	else if (the_pc)
		print_counts(the_conf, avail_before);
}

static void usage(const char *argv0)
{
	fprintf(stderr,
"Usage: %s [options] file.ini\n"
"  (no option)    the guest's picture on the frame buffer console\n"
"  -term          the guest's text screen on this terminal; Ctrl+] quits\n"
"  -none          no screen and no keyboard, for timing\n"
"  -insns N       stop after N million guest instructions\n"
"  -time S        stop after S seconds\n"
"  -kips N        run no faster than N thousand instructions a second: for\n"
"                 trying out a slow machine on a fast one\n"
"  -marks         time a guest that marks its work (BENCH.COM) from mark to\n"
"                 mark, and stop at its last one\n"
"  -share P       at most P in 100 of the time goes on drawing the screen (20)\n"
"  -fps N         at most N pictures a second (10)\n"
"  -log FILE      the emulator's and the BIOS's messages go to FILE;\n"
"                 without it they are dropped while the screen is in use\n"
"  -nosync        do not wait for disk writes to reach the card\n"
"  -plainvga      draw the 16-colour modes pixel by pixel, like the others:\n"
"                 slower, and what to try if such a picture looks wrong\n"
"  -force         run even if the machine has less memory than the guest needs\n"
"  --hostbench    time this machine's loads and stores, and stop\n"
"  --fix-console  put the console back in text mode after a killed run\n"
"Ctrl+Alt+Backspace quits. TINY386_NOGUARD=1 runs without the console guard.\n",
		argv0);
}

int main(int argc, char *argv[])
{
	static PCConfig conf;	/* finish() reads it after main() has returned */
	const char *inifile = NULL, *logfile = NULL;
	long max_minsns = 0, max_seconds = 0, max_kips = 0;
	/* Looking at a 640x480 screen that has not changed costs this kind of
	   machine some milliseconds; ten looks a second leave the guest most
	   of the processor and are enough for a caret and a pointer. */
	int share = 20, fps = 10, force = 0, nosync = 0;
	int status, i;

	if (guard_main(argc, argv, &status))
		return status;
	if (argc == 2 && !strcmp(argv[1], "--hostbench"))
		return hostbench();

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : NULL;

		if (!strcmp(a, "-term")) {
			mode = MODE_TERM;
		} else if (!strcmp(a, "-none")) {
			mode = MODE_NONE;
		} else if (!strcmp(a, "-force")) {
			force = 1;
		} else if (!strcmp(a, "-nosync")) {
			nosync = 1;
		} else if (!strcmp(a, "-plainvga")) {
			vga_planar_fast = 0;
		} else if (!strcmp(a, "-marks")) {
			watch_marks = 1;
		} else if (!strcmp(a, "-insns") && v) {
			max_minsns = atol(v);
			i++;
		} else if (!strcmp(a, "-time") && v) {
			max_seconds = atol(v);
			i++;
		} else if (!strcmp(a, "-kips") && v) {
			max_kips = atol(v);
			i++;
		} else if (!strcmp(a, "-share") && v) {
			share = atoi(v);
			i++;
		} else if (!strcmp(a, "-fps") && v) {
			fps = atoi(v);
			i++;
		} else if (!strcmp(a, "-log") && v) {
			logfile = v;
			i++;
		} else if (a[0] != '-' && !inifile && i == argc - 1) {
			inifile = a;
		} else {
			usage(argv[0]);
			return 1;
		}
	}
	if (!inifile || share < 1 || share > 90 || fps < 1 || fps > 60 ||
	    max_minsns < 0 || max_seconds < 0 || max_kips < 0) {
		usage(argv[0]);
		return 1;
	}
#ifdef KORVO_NO_SDL
	if (mode == MODE_SDL) {
		fprintf(stderr, "tiny386: this build has no screen of its own:"
			" say -term or -none\n");
		return 1;
	}
#endif

	memset(&conf, 0, sizeof(conf));
	conf.mem_size = 8 * 1024 * 1024;
	conf.vga_mem_size = 256 * 1024;
	conf.width = 800;
	conf.height = 480;
	conf.cpu_gen = 4;
	conf.fpu = 0;
	ne2000_set_config_file(inifile);
	status = ini_parse(inifile, parse_conf_ini, &conf);
	if (status) {
		if (status < 0)
			fprintf(stderr, "tiny386: %s: cannot be read\n", inifile);
		else
			fprintf(stderr, "tiny386: %s: line %d is not understood\n",
				inifile, status);
		return 1;
	}
#ifndef I386_ENABLE_FPU
	if (conf.fpu) {
		fprintf(stderr, "tiny386: [cpu] fpu = 1, and this build has no"
			" floating point unit\n");
		return 1;
	}
#endif

	atexit(finish);
	check_config(&conf);
	avail_before = meminfo_kb("MemAvailable");
	check_memory(&conf, force);
	the_conf = &conf;
	guest_mem = conf.mem_size;

	/* From here on the terminal is changed.  The guard first. */
	uint8_t *fb = NULL;
	SimpleFBDrawFunc *draw = no_redraw;

	if (mode == MODE_SDL || (mode == MODE_TERM && guard_is_console(0)))
		guard_start();
#ifndef KORVO_NO_SDL
	if (mode == MODE_SDL) {
		fb = screen_open(&conf);
		draw = redraw;
	}
#endif
	if (!fb) {
		/* No screen of our own.  The VGA still wants somewhere to
		   draw; one too small for a text screen makes it leave the
		   drawing out, and what a graphics mode would draw in it is
		   next to nothing. */
		conf.width = conf.height = 16;
		fb = bigmalloc(conf.width * conf.height * (BPP / 8));
	}
	hide_messages(logfile);

	PC *pc = pc_new(draw, NULL, fb, &conf);
	Term *term = NULL;
	if (mode == MODE_TERM) {
		term_quit_key = quit_key;
		term = term_init(pc->vga, term_put_key, pc->kbd);
		term_up = 1;
	}
	if (mode != MODE_SDL) {
		/* with SDL its frame buffer driver has these, and puts the
		   console back before the signal does what it does */
		signal(SIGTERM, on_signal);
		signal(SIGINT, on_signal);
		signal(SIGHUP, on_signal);
	}
	load_bios_and_reset(pc);
	the_pc = pc;	/* from here on there are counts to print */

	/*
	 * The picture is drawn when the guest's vertical retrace comes
	 * round, but not more often than `fps` times a second, and after a
	 * picture that took t the next one waits t * (100 - share) / share:
	 * a full picture of a graphics mode costs this processor a large
	 * part of a second, and it has only the one to run the guest on.
	 */
	const uint32_t min_gap = 1000000 / fps;
	uint32_t now = get_uticks();
	uint32_t last = now, next_draw = now;
	uint32_t last_cycle = (uint32_t) cpui386_get_cycle(pc->cpu);
	uint32_t aside = 0;	/* time since `last` that was not the guest's */
	unsigned writes = ide_write_count() + emulink_write_count();
	uint32_t write_time = now;
	int unsynced = 0;
	int mark = 0;
	uint64_t mark_insns = 0, mark_us = 0;

	pc->boot_start_time = now;
	while (running && pc->shutdown_state != 8) {
		pc_step(pc);

		now = get_uticks();
		uint32_t cycle = (uint32_t) cpui386_get_cycle(pc->cpu);
		uint32_t done = cycle - last_cycle;
		if (max_kips && done) {
			/* as long as a slower machine would have taken */
			uint64_t want = (uint64_t) done * 1000 / max_kips;
			uint32_t took = now - last - aside;

			/* not so long that the keyboard goes unheard */
			if (want > 200000)
				want = 200000;
			if (took < want) {
				usleep(want - took);
				now = get_uticks();
			}
		}
		uint32_t dt = now - last - aside;
		last_cycle = cycle;
		count.wall_us += now - last;
		last = now;
		aside = 0;
		count.insns += done;
		if (done)
			count.run_us += dt;
		else
			count.halt_us += dt;
		if (watch_marks) {
			int m = (uint8_t) pc->phys_mem[0x4f0];

			if (m != mark && m >= 0xa1 && m <= 0xaf) {
				if (mark && nmarks < MAXMARKS) {
					marks[nmarks].mark = mark;
					marks[nmarks].insns = count.insns - mark_insns;
					marks[nmarks].us = count.run_us - mark_us;
					nmarks++;
				}
				mark = m;
				mark_insns = count.insns;
				mark_us = count.run_us;
				if (m == 0xaf)
					break;
			}
		}
		if (max_minsns && count.insns >= (uint64_t) max_minsns * 1000000)
			break;
		if (max_seconds && count.wall_us >= (uint64_t) max_seconds * 1000000)
			break;
		/* a signal has said to stop: do not go back to a terminal
		   that may have gone */
		if (!running)
			break;

#ifndef KORVO_NO_SDL
		if (mode == MODE_SDL)
			sdl_poll(pc);
#endif
		if (term)
			term_step(term);

		if (vga_step(pc->vga) && mode != MODE_NONE &&
		    (int32_t) (now - next_draw) >= 0) {
			uint32_t t0 = get_uticks();
			vga_refresh(pc->vga, pc->redraw, pc->redraw_data,
				    pc->full_update != 0);
			if (pc->full_update == 2)
				pc->full_update = 0;
			uint32_t t1 = get_uticks();
			uint32_t cost = t1 - t0;
			uint32_t gap = (uint64_t) cost * (100 - share) / share;

			if (gap < min_gap)
				gap = min_gap;
			if (gap > 2000000)
				gap = 2000000;
			next_draw = t1 + gap;
			count.draw_us += cost;
			count.draws++;
			if (cost > count.draw_max)
				count.draw_max = cost;
			aside += cost;
		}

		if (!nosync) {
			unsigned w = ide_write_count() + emulink_write_count();

			if (w != writes) {
				writes = w;
				write_time = now;
				unsynced = 1;
			} else if (unsynced && now - write_time > 2000000) {
				uint32_t t0 = get_uticks();
				ide_flush();
				emulink_flush(pc->emulink);
				uint32_t t1 = get_uticks();

				unsynced = 0;
				count.sync_us += t1 - t0;
				count.syncs++;
				aside += t1 - t0;
			}
		}
	}

	/* the screen back in text mode first; finish() does the rest, as it
	   does for every other way out */
	text_mode();
	return 0;
}
