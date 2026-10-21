/*
 * The console guard: see guard.c.
 */
#ifndef KORVO_GUARD_H
#define KORVO_GUARD_H

/* Handles "--console-guard FD" and "--fix-console", which do their work and
 * are done.  Returns 1 if the arguments were one of them, with the exit
 * status in *status. */
int guard_main(int argc, char *argv[], int *status);

/* Starts the guard, if the screen is showing a console.  Call before
 * anything changes that console.  TINY386_NOGUARD in the environment turns
 * it off. */
void guard_start(void);

/* 1 if file descriptor fd is a virtual terminal of the console. */
int guard_is_console(int fd);

#endif /* KORVO_GUARD_H */
