/*
 * What ne2000.c links against in these tests.  run_ne2000.sh compiles
 * ne2000.c with -Dmemcpy=ne_memcpy -Dmalloc=ne_malloc (and the ESP32 build
 * calls pcmalloc), so that:
 *
 * - the card's state is followed by a 64 KiB guard, poisoned for
 *   AddressSanitizer and filled with a pattern, so that a write past s->mem
 *   is seen however far past it lands (the state ends with mem[]);
 * - every memcpy() is counted: ne2000_receive()'s copy loop calls it once
 *   each time round, even when it copies nothing, so an endless loop shows as
 *   too many calls and the test stops there rather than at its timer;
 * - a test can have the guest write a register just after one of the copies,
 *   as the guest on the other core can in the ESP32 builds.
 *
 * This file is compiled without those defines.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#endif
#include "stubs.h"
#include "shim/esp_mac.h"

#define GUARD (64 * 1024)

static uint8_t *block_end;

void *ne_malloc(size_t size)
{
    uint8_t *p = malloc(size + GUARD);
    if (!p)
        abort();
    memset(p, 0, size);
    memset(p + size, 0xa5, GUARD);
    block_end = p + size;
#ifdef __SANITIZE_ADDRESS__
    ASAN_POISON_MEMORY_REGION(block_end, GUARD);
#endif
    return p;
}

void *pcmalloc(long size)
{
    return ne_malloc(size);
}

long guard_dirty(void)
{
    long n = 0;
    if (!block_end)
        return 0;
#ifdef __SANITIZE_ADDRESS__
    ASAN_UNPOISON_MEMORY_REGION(block_end, GUARD);
#endif
    for (int i = 0; i < GUARD; i++)
        n += block_end[i] != 0xa5;
#ifdef __SANITIZE_ADDRESS__
    ASAN_POISON_MEMORY_REGION(block_end, GUARD);
#endif
    return n;
}

long memcpy_calls;
long memcpy_limit = 64;
void (*memcpy_after)(void);

void *ne_memcpy(void *d, const void *s, size_t n)
{
    memcpy(d, s, n);
    if (++memcpy_calls > memcpy_limit)
        _exit(EXIT_HANG);
    if (memcpy_after) {
        void (*f)(void) = memcpy_after;
        memcpy_after = NULL;
        f();
    }
    return d;
}

#ifdef __SANITIZE_ADDRESS__
/* where the bad access was, from the end of the card's state */
static void on_asan_death(void)
{
    uint8_t *a = __asan_get_report_address();
    if (a && block_end)
        dprintf(2, "BADACCESS %s of %zu bytes at the end of the state %+ld\n",
                __asan_get_report_access_type() ? "write" : "read",
                __asan_get_report_access_size(), (long)(a - block_end));
}

__attribute__((constructor)) static void set_death_callback(void)
{
    __asan_set_death_callback(on_asan_death);
}
#endif

/* the ESP32 build */
void (*_Atomic esp32_send_packet)(uint8_t *buf, int size);

int esp_read_mac(uint8_t *m, esp_mac_type_t type)
{
    static const uint8_t board[6] = {0x02, 0x00, 0x5e, 0x10, 0x00, 0x01};
    memcpy(m, board, 6);
    return 0;
}
