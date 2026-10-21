/*
 * Host test for the PS/2 queue's lock (i8042.c, THREAD_SAFE as in ESP
 * builds).  ps2_queue() takes the queue's mutex; when the 256-byte queue
 * was full it used to return without giving it back, so the next
 * ps2_queue() from the USB task, or ps2_read_data() from the emulator,
 * waited for ever.
 *
 * The queue is filled and then written past its end, and after every call
 * the mutex must be free.  Then another thread reads it back, which must
 * not wait, and must find exactly the first 256 bytes in order.  The same
 * again through ps2_put_keycode() with no reader at all, as when a guest
 * leaves IRQ 1 masked while someone types, and for the mouse's queue.
 *
 * Built with BUILD_ESP32 (so that i8042.c defines THREAD_SAFE) against the
 * stand-in FreeRTOS headers in shim/, whose mutex reports a second take by
 * the same thread instead of hanging.  Exit status 0 when every check
 * passes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i8042.h"

/* ==== the stand-in mutex ==== */

struct HostMutex {
    pthread_mutex_t m;
    volatile int held;
};

int host_mutex_errors;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct HostMutex *h = calloc(1, sizeof(*h));
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&h->m, &a);
    pthread_mutexattr_destroy(&a);
    return h;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t ticks)
{
    struct timespec ts;
    (void) ticks;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 200 * 1000000;            /* 0.2 s stands for "for ever" */
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    int r = pthread_mutex_timedlock(&h->m, &ts);
    if (r) {
        __atomic_add_fetch(&host_mutex_errors, 1, __ATOMIC_SEQ_CST);
        fprintf(stderr, "    take failed: %s\n",
                r == EDEADLK ? "this thread holds it already "
                               "(a FreeRTOS task would wait for ever)" :
                r == ETIMEDOUT ? "held by another thread for 0.2 s" :
                strerror(r));
        return pdFALSE;
    }
    h->held = 1;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t h)
{
    h->held = 0;
    int r = pthread_mutex_unlock(&h->m);
    if (r) {
        __atomic_add_fetch(&host_mutex_errors, 1, __ATOMIC_SEQ_CST);
        fprintf(stderr, "    give failed: %s\n",
                r == EPERM ? "this thread does not hold it" : strerror(r));
        return pdFALSE;
    }
    return pdTRUE;
}

int host_mutex_held(SemaphoreHandle_t h)
{
    return h->held;
}

/* ==== what i8042.c needs from the rest of the emulator ==== */

static uint32_t fake_us;
uint32_t get_uticks(void)
{
    return fake_us;
}

/* the queue's mutex: the first member of PS2Queue after the data and the
   three ints, as i8042.c lays it out with THREAD_SAFE */
struct QueueView {
    uint8_t data[256];
    int rptr, wptr, count;
    SemaphoreHandle_t mutex;
};

static SemaphoreHandle_t mutex_of(void *ps2state)
{
    return ((struct QueueView *) ps2state)->mutex;
}

static int irq_level = -1;
static void update_irq(void *opaque, int level)
{
    (void) opaque;
    irq_level = level;
}

/* ==== checks ==== */

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

struct ReadJob {
    void *q;
    int n;
    uint8_t got[300];
    int levels[300];
};

static void *reader(void *arg)
{
    struct ReadJob *j = arg;
    for (int i = 0; i < j->n; i++) {
        j->got[i] = ps2_read_data(j->q);
        j->levels[i] = irq_level;
    }
    return NULL;
}

/* reads n bytes from another thread, as the emulator does on core 1; NULL
   if it does not finish (the job is then left to the thread) */
static struct ReadJob *read_back(void *q, int n)
{
    pthread_t t;
    struct timespec ts;
    struct ReadJob *j = calloc(1, sizeof(*j));
    j->q = q;
    j->n = n;
    pthread_create(&t, NULL, reader, j);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    if (pthread_timedjoin_np(t, NULL, &ts)) {
        printf("  FAIL: the reader is still waiting after 3 s\n");
        failures++;
        return NULL;
    }
    return j;
}

static void test_fill_and_overflow(const char *name, void *q)
{
    struct ReadJob *j;
    int held_after_fill = 0, held_after_overflow = 0;
    int errors0 = host_mutex_errors;

    printf("%s: 256 bytes, then 44 more into the full queue\n", name);
    for (int i = 0; i < 256; i++) {
        ps2_queue(q, i);
        held_after_fill |= host_mutex_held(mutex_of(q));
    }
    check(!held_after_fill, "mutex left held while filling");
    for (int i = 0; i < 44; i++) {
        ps2_queue(q, 0xee);
        held_after_overflow |= host_mutex_held(mutex_of(q));
    }
    check(!held_after_overflow, "mutex left held by ps2_queue() on a full queue");
    check(host_mutex_errors == errors0, "a take or give failed while writing");

    if (!(j = read_back(q, 257)))
        return;
    int order = 1;
    for (int i = 0; i < 256; i++)
        if (j->got[i] != i)
            order = 0;
    check(order, "the reader did not get bytes 0 to 255 in order");
    check(j->levels[254] == 1, "IRQ not still raised with one byte left");
    check(j->levels[255] == 0, "IRQ still raised after the 256th byte: more than 256 kept");
    check(j->got[256] == 255, "an empty queue does not give the last byte again");
    free(j);
    check(!host_mutex_held(mutex_of(q)), "mutex held after reading");
    check(host_mutex_errors == errors0, "a take or give failed while reading");
}

static void test_typing_into_masked_irq(void)
{
    struct ReadJob *j;
    int errors0 = host_mutex_errors;
    PS2KbdState *kbd = ps2_kbd_init(update_irq, NULL);

    printf("keyboard: 300 key presses with nothing reading\n");
    for (int i = 0; i < 300; i++) {
        ps2_put_keycode(kbd, 1, 0x1e);          /* A down */
        fake_us += 10000;
    }
    check(!host_mutex_held(mutex_of(kbd)), "mutex left held after typing into a full queue");
    check(host_mutex_errors == errors0, "a take or give failed while typing");
    if (!(j = read_back(kbd, 256)))
        return;
    int all = 1;
    for (int i = 0; i < 256; i++)
        if (j->got[i] != 0x1e)
            all = 0;
    check(all, "the queue does not hold 256 make codes of A");
    check(j->levels[255] == 0, "more than 256 bytes kept");
    free(j);
    check(host_mutex_errors == errors0, "a take or give failed while reading");
}

int main(void)
{
    PS2KbdState *kbd = ps2_kbd_init(update_irq, NULL);
    PS2MouseState *mouse = ps2_mouse_init(update_irq, NULL);

    test_fill_and_overflow("keyboard queue", kbd);
    test_fill_and_overflow("mouse queue", mouse);
    test_typing_into_masked_irq();

    if (failures) {
        printf("test_ps2_queue: %d checks FAILED\n", failures);
        return 1;
    }
    printf("test_ps2_queue: all checks passed\n");
    return 0;
}
