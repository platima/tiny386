/*
 * [esp] profile = 1 (v5.1, s31/PROFILE.md): a sampling profiler of core 1,
 * the emulator's core, run from core 0, written to PROFILE.TXT beside the
 * ini.  With profile = 0 (the default) nothing of it is set up.
 */
#ifndef PROFILE_H
#define PROFILE_H

#include <stdbool.h>

/* app_main (core 0), after the ini and before the PC's task starts: with
   on, the tables in PSRAM, the cache counters, the sampling timer (whose
   interrupt is then on core 0) and Ctrl+Alt+F8; a console line says it is
   on, or why not.  ini_path is the ini the PC starts from, ini_dir its
   folder (the file goes there; it must be on the card).  Returns whether
   it is on. */
bool profile_setup(int on, const char *ini_path, const char *ini_dir, int fast_cpu);
/* On the emulator's core when the PC starts to run (after the boot
   console's hold): the samples count from now.  cpu is the PC's CPUI386. */
void profile_start(void *cpu);
/* On the emulator's core between two steps (speed_take_saves() gave
   SPEED_SAVE_PROFILE), or with why "end" when the PC is switched off:
   PROFILE.TXT written with the counts so far, the sampling paused while
   it is (and the guest waits); a console line says how long it took. */
void profile_write(const char *why);
bool profile_on(void);

#endif /* PROFILE_H */
