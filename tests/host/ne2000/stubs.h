#ifndef NE2000TEST_STUBS_H
#define NE2000TEST_STUBS_H

#include <stddef.h>

/* the exit status of a test's child process */
#define EXIT_HANG  10           /* the copy loop went round and round */
#define EXIT_GUARD 11           /* the guard after the card's state changed */
#define EXIT_BAD   12           /* frames wrong */
#define EXIT_ASAN  20           /* AddressSanitizer (ASAN_OPTIONS exitcode) */
#define EXIT_UBSAN 21           /* UBSan (UBSAN_OPTIONS exitcode) */

extern long memcpy_calls;       /* the test sets it to 0 before a frame */
extern long memcpy_limit;       /* more calls than this: EXIT_HANG */
extern void (*memcpy_after)(void);  /* run once, just after the next copy */

long guard_dirty(void);         /* bytes of the guard that changed */

#endif
