/*
 * membench.h: the board's memory measured once (v5.2, [esp] membench = 1;
 * membench.c).
 */
#ifndef MEMBENCH_H
#define MEMBENCH_H

#include <stddef.h>
#include <stdint.h>

typedef void (*membench_say_fn)(const char *fmt, ...);

/* Before the PC is built, with pool the emulator's PSRAM pool (4 KiB
   aligned), of which it scribbles over the first 3.1 MiB.  Each line of
   results goes to out.  The PC must not be built after it: the cache has
   been poked.  0 done; 1 done, but the cache's preload engine once did not
   finish and its tests were cut short; -1 the pool is too small; -2 no
   internal RAM for it. */
int membench_run(membench_say_fn out, uint8_t *pool, size_t pool_len);

#endif
