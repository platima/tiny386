/*
 * The guest's port traffic to the first IDE interface (hda and hdb), logged
 * as it goes into the file IDE_TRACE names, for ide_count to give to ide.c
 * again.  Linked into a PC build of tiny386 (tiny386_headless) with
 *
 *   -Wl,--wrap=ide_allocate,--wrap=ide_ioport_write,--wrap=ide_ioport_read
 *   -Wl,--wrap=ide_cmd_write,--wrap=ide_status_read,--wrap=ide_data_writew
 *   -Wl,--wrap=ide_data_readw,--wrap=ide_data_writel,--wrap=ide_data_readl
 *   -Wl,--wrap=ide_data_write_string,--wrap=ide_data_read_string
 *
 * One line for each access: w PORT VAL, r PORT VAL, c VAL (device
 * control), s VAL (alternate status), W2 VAL, W4 VAL, R2 VAL, R4 VAL, then
 * WS SIZE COUNT RESULT HEXBYTES (the units taken) and RS SIZE COUNT RESULT
 * FNV1A-HASH for the strings (REP OUTS and INS).  The disk's bytes are in
 * it: keep it with the image it came from, never anywhere public.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void *__real_ide_allocate(int irq, void *pic, void (*set_irq)(void *pic, int irq, int level));
void __real_ide_ioport_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t __real_ide_ioport_read(void *opaque, uint32_t addr);
void __real_ide_cmd_write(void *opaque, uint32_t val);
uint32_t __real_ide_status_read(void *opaque);
void __real_ide_data_writew(void *opaque, uint32_t val);
uint32_t __real_ide_data_readw(void *opaque);
void __real_ide_data_writel(void *opaque, uint32_t val);
uint32_t __real_ide_data_readl(void *opaque);
int __real_ide_data_write_string(void *opaque, uint8_t *buf, int size, int count);
int __real_ide_data_read_string(void *opaque, uint8_t *buf, int size, int count);

static void *s_ide;
static FILE *s_f;

/* a run ended by a signal (tools/drive.py ends its program so) keeps its
   log whole */
static void end(int sig)
{
    if (s_f)
        fflush(s_f);
    _exit(0);
}

void *__wrap_ide_allocate(int irq, void *pic, void (*set_irq)(void *pic, int irq, int level))
{
    void *p = __real_ide_allocate(irq, pic, set_irq);
    if (!s_ide) {
        s_ide = p;
        const char *path = getenv("IDE_TRACE");
        if (path)
            s_f = fopen(path, "w");
        signal(SIGTERM, end);
        signal(SIGINT, end);
        signal(SIGHUP, end);
    }
    return p;
}

#define LOG(...) do { if (s_f && opaque == s_ide) fprintf(s_f, __VA_ARGS__); } while (0)

void __wrap_ide_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    LOG("w %x %x\n", addr, val);
    __real_ide_ioport_write(opaque, addr, val);
}

uint32_t __wrap_ide_ioport_read(void *opaque, uint32_t addr)
{
    uint32_t v = __real_ide_ioport_read(opaque, addr);
    LOG("r %x %x\n", addr, v);
    return v;
}

void __wrap_ide_cmd_write(void *opaque, uint32_t val)
{
    LOG("c %x\n", val);
    __real_ide_cmd_write(opaque, val);
}

uint32_t __wrap_ide_status_read(void *opaque)
{
    uint32_t v = __real_ide_status_read(opaque);
    LOG("s %x\n", v);
    return v;
}

void __wrap_ide_data_writew(void *opaque, uint32_t val)
{
    LOG("W2 %x\n", val & 0xffff);
    __real_ide_data_writew(opaque, val);
}

uint32_t __wrap_ide_data_readw(void *opaque)
{
    uint32_t v = __real_ide_data_readw(opaque);
    LOG("R2 %x\n", v);
    return v;
}

void __wrap_ide_data_writel(void *opaque, uint32_t val)
{
    LOG("W4 %x\n", val);
    __real_ide_data_writel(opaque, val);
}

uint32_t __wrap_ide_data_readl(void *opaque)
{
    uint32_t v = __real_ide_data_readl(opaque);
    LOG("R4 %x\n", v);
    return v;
}

int __wrap_ide_data_write_string(void *opaque, uint8_t *buf, int size, int count)
{
    int r = __real_ide_data_write_string(opaque, buf, size, count);
    /* the units taken: ide.c takes no more than the block has left, so
       given again with that count it takes the same */
    if (s_f && opaque == s_ide && r > 0) {
        fprintf(s_f, "WS %x %x %x ", size, r, r);
        for (int i = 0; i < size * r; i++)
            fprintf(s_f, "%02x", buf[i]);
        fputc('\n', s_f);
    }
    return r;
}

int __wrap_ide_data_read_string(void *opaque, uint8_t *buf, int size, int count)
{
    int r = __real_ide_data_read_string(opaque, buf, size, count);
    if (s_f && opaque == s_ide) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < r * size; i++)
            h = (h ^ buf[i]) * 16777619u;
        fprintf(s_f, "RS %x %x %x %x\n", size, count, r, h);
    }
    return r;
}
