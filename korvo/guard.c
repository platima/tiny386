/*
 * The console guard.
 *
 * On a board whose screen is the Linux frame buffer console, the screen the
 * emulator draws on is also the terminal it was started from.  SDL puts that
 * terminal in graphics mode and its keyboard in raw mode, and puts both back
 * when the program ends, by any exit and by any signal that can be caught.
 * SIGKILL cannot be caught, and it is what the kernel sends a process when
 * memory runs out: the likeliest way for an emulator with megabytes of guest
 * memory to die on a machine with 16 MB.  Nothing then puts the terminal
 * back, and with no other way into the machine that costs a power cycle.
 *
 * So a second small process is started first.  It holds one end of a pipe
 * that only the emulator holds the other end of, and sleeps until that end
 * closes, which it does when the emulator has gone, however it went.  Then
 * it looks at the terminal and puts back what was left behind.
 *
 * "--fix-console" does the same by hand: from another login, or from a
 * timer started on the panel beforehand.
 *
 * The same idea as the guard of the Wolfenstein 3D port for this board,
 * written again for this program.  Offered under the licence of tiny386
 * (BSD 3-clause, see LICENSE).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include "guard.h"

/*
 * What the terminal on the screen looked like when the emulator started.
 * The emulator finds this out before it changes anything and hands it to
 * the guard through the pipe.
 */
static struct {
	int vt;			/* -1: not a console */
	int kbmode;
	int haveterm;
	struct termios term;
} start = { -1 };

int guard_is_console(int fd)
{
	int mode;

	return ioctl(fd, KDGKBMODE, &mode) == 0;
}

/*
 * The terminal that is on the screen now.  /dev/tty0 is that one, for
 * whoever may open it; /dev/tty is the one we were started on.
 */
static int open_console(int *vt)
{
	struct vt_stat state;
	int fd, mode;

	fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fd < 0)
		fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -1;

	if (ioctl(fd, KDGKBMODE, &mode) < 0 ||
	    ioctl(fd, VT_GETSTATE, &state) < 0) {
		close(fd);	/* not a console */
		return -1;
	}

	*vt = state.v_active;
	return fd;
}

static void remember_console(void)
{
	int fd = open_console(&start.vt);

	if (fd < 0) {
		start.vt = -1;
		return;
	}
	if (ioctl(fd, KDGKBMODE, &start.kbmode) < 0)
		start.kbmode = K_UNICODE;
	start.haveterm = tcgetattr(fd, &start.term) == 0;
	close(fd);
}

/*
 * Puts one terminal right if it is in graphics mode, or its keyboard sends
 * raw codes, or its settings are the ones a program like this gives it.
 * Returns 1 if there was something to put right.
 */
static int fix_terminal(int fd, int vt)
{
	struct termios term;
	int kdmode = KD_TEXT, kbmode = K_UNICODE;
	int haveterm, rawterm;

	ioctl(fd, KDGETMODE, &kdmode);
	ioctl(fd, KDGKBMODE, &kbmode);
	haveterm = tcgetattr(fd, &term) == 0;
	/* SDL's settings, and those of "-term": no line editing, no echo, no
	   signals from keys, and reads that do not wait.  No shell leaves a
	   terminal like that. */
	rawterm = haveterm && !(term.c_lflag & (ICANON | ECHO | ISIG)) &&
		term.c_cc[VMIN] == 0;

	if (kdmode == KD_TEXT && kbmode != K_MEDIUMRAW && kbmode != K_RAW &&
	    !rawterm)
		return 0;

	/* The keyboard and the terminal settings first, and text mode after:
	   going back to text mode redraws the screen, which takes a while,
	   and the shell that was waiting for the emulator is about to read
	   the settings. */
	if (vt == start.vt && start.haveterm) {
		ioctl(fd, KDSKBMODE, start.kbmode);
		tcsetattr(fd, TCSAFLUSH, &start.term);
	} else {
		/* What this terminal was like before is not known.  Give it
		   the keyboard mode that the kernel starts terminals in, and
		   undo the changes that were made to the settings. */
		ioctl(fd, KDSKBMODE, vt == start.vt ? start.kbmode : K_UNICODE);
		if (haveterm) {
			term.c_iflag |= ICRNL | IXON;
			term.c_lflag |= ICANON | ECHO | ISIG;
			term.c_cc[VMIN] = 1;
			term.c_cc[VTIME] = 0;
			tcsetattr(fd, TCSAFLUSH, &term);
		}
	}
	ioctl(fd, KDSETMODE, KD_TEXT);
	/* and what "-term" asks of a terminal: its other screen, and cursor
	   keys that send the application's codes.  A terminal that was not
	   asked takes no notice. */
	{
		static const char seq[] = "\x1b[?1l\x1b[?1049l\x1b[?25h";

		if (write(fd, seq, sizeof(seq) - 1) < 0) {
			/* nothing to be done about it */
		}
	}
	return 1;
}

/*
 * Looks at the terminal on the screen, and at the one the emulator was
 * started on if that is another, and puts right what an emulator that has
 * gone left behind.  If SDL had moved the screen to a terminal of its own,
 * the screen goes back.  Returns 1 if there was something to put right.
 *
 * Only call this when no emulator is running: it cannot tell one that has
 * died from one that is in use.
 */
static int fix_console(void)
{
	char path[32];
	int fixed, other = 0;
	int vt, fd, fd2;

	fd = open_console(&vt);
	if (fd < 0)
		return 0;

	fixed = fix_terminal(fd, vt);

	if (start.vt > 0 && vt != start.vt) {
		snprintf(path, sizeof(path), "/dev/tty%d", start.vt);
		fd2 = open(path, O_RDWR | O_NOCTTY);
		if (fd2 >= 0) {
			other = fix_terminal(fd2, start.vt);
			close(fd2);
		}
	}

	if (fixed) {
		ioctl(fd, VT_UNLOCKSWITCH, 1);
		if (start.vt > 0 && vt != start.vt)
			ioctl(fd, VT_ACTIVATE, start.vt);
	}

	close(fd);
	return fixed || other;
}

/*
 * The guard itself: started by the emulator before it touches anything,
 * with one end of a pipe.  When the emulator ends, however it ends, the
 * pipe closes and the read below returns.
 */
static int guard_run(int pipefd)
{
	static const int ignored[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT,
		SIGPIPE, SIGALRM, SIGUSR1, SIGUSR2, SIGTTOU, SIGTTIN, SIGTSTP };
	struct sched_param param;
	size_t i, got = 0;
	ssize_t n;
	char c;

	/* Whatever is sent to the emulator by name or from the keyboard comes
	   here too.  This has to outlast the emulator. */
	for (i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++)
		signal(ignored[i], SIG_IGN);

	/* Hold nothing open but the pipe */
	for (i = 0; i < 3; i++) {
		if ((int) i != pipefd) {
			close(i);
			open("/dev/null", O_RDWR);
		}
	}

	/* When the emulator dies, the shell that started it wakes up as well,
	   and one of the first things a shell does is to read the terminal
	   settings so that it can put them back after each command.  This has
	   to get there before it does.  A real-time priority sees to that;
	   nothing else here needs one, and nothing is lost if it is refused.
	   (The sched_setscheduler() of the C library does nothing, on
	   purpose.) */
	param.sched_priority = 1;
	syscall(SYS_sched_setscheduler, 0, SCHED_FIFO, &param);

	/* First what the console was like before anything changed it ... */
	while (got < sizeof(start)) {
		n = read(pipefd, (char *) &start + got, sizeof(start) - got);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return 1;	/* not a pipe: started by hand */
		if (n == 0)
			break;
		got += n;
	}
	if (got < sizeof(start)) {
		/* the emulator went before it had said: go by what can be
		   seen now */
		remember_console();
		start.haveterm = 0;
		start.kbmode = K_UNICODE;
	} else {
		/* ... and then nothing, until the emulator has gone */
		while ((n = read(pipefd, &c, 1)) != 0) {
			if (n < 0 && errno != EINTR)
				return 1;
		}
	}

	fix_console();
	return 0;
}

void guard_start(void)
{
	void (*oldpipe)(int);
	char arg[16];
	int fds[2];
	pid_t pid;

	if (getenv("TINY386_NOGUARD"))
		return;

	remember_console();
	if (start.vt < 0)
		return;

	if (pipe(fds) < 0)
		return;

	pid = fork();
	if (pid == 0) {
		/* A new program rather than a copy of this one, so that it
		   does not hold on to this one's memory.  /proc/self/exe is
		   this file whatever it was started as. */
		close(fds[1]);
		snprintf(arg, sizeof(arg), "%d", fds[0]);
		execl("/proc/self/exe", "tiny386-guard", "--console-guard",
		      arg, (char *) NULL);
		_exit(127);
	}

	/* Tell the guard what the console is like now, and then keep fds[1]
	   open, and silent, for as long as the emulator runs.  No message if
	   there is no guard: the emulator runs as well without.  (A guard
	   that could not be started has gone already, and a write to a pipe
	   that nobody reads is a SIGPIPE: not one to die of.) */
	oldpipe = signal(SIGPIPE, SIG_IGN);

	close(fds[0]);
	if (pid < 0 ||
	    write(fds[1], &start, sizeof(start)) != (ssize_t) sizeof(start))
		close(fds[1]);
	else
		fcntl(fds[1], F_SETFD, FD_CLOEXEC);

	signal(SIGPIPE, oldpipe);
}

int guard_main(int argc, char *argv[], int *status)
{
	if (argc == 3 && !strcmp(argv[1], "--console-guard")) {
		*status = guard_run(atoi(argv[2]));
		return 1;
	}
	if (argc == 2 && !strcmp(argv[1], "--fix-console")) {
		/* Started from a timer, this is a background job on the
		   terminal it is about to change the settings of, and the
		   kernel stops such a job with SIGTTOU unless it ignores
		   that */
		signal(SIGTTOU, SIG_IGN);
		signal(SIGTTIN, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);

		remember_console();
		/* What it was before is not known any more: the kernel's
		   defaults */
		start.kbmode = K_UNICODE;
		start.haveterm = 0;
		if (start.vt < 0)
			puts("The screen is not showing a console that this can open.");
		else if (fix_console())
			puts("The console was in graphics mode or had a raw keyboard. It has been put back.");
		else
			puts("The console is in text mode with an ordinary keyboard. Nothing to do.");
		*status = 0;
		return 1;
	}
	return 0;
}
