/*
 * What the guest's IDE commands cost the card: ide.c as the board builds it
 * (BUILD_ESP32), its disk image on a FAT volume through esp/main/
 * disk_image.c and ESP-IDF's own ff.c, diskio.c and diskio_sdmmc.c, with
 * tests/host/storage's card glue (one pretend card command for each
 * sdmmc_read_sectors() or sdmmc_write_sectors()).  run_ide.sh builds it
 * with ide.c with and without IDE_ENABLE_WRITE_GATHER and
 * IDE_ENABLE_READ_AHEAD, and with disk_image.c's buffer at other sizes.
 *
 *   ide_count VOLUME SECTORS mix SEED COMMANDS    a made-up mix of commands
 *   ide_count VOLUME SECTORS trace FILE           the port traffic of a run
 *                                                 (ide_trace.c), again
 *
 * VOLUME is a FAT volume image holding IMAGE.IMG.  It prints one line of
 * counts: the block device's reads and writes (read_async, write_async),
 * FatFs's f_read() and f_write(), the diskio layer's ff_disk_read() and
 * ff_disk_write(), the card's commands, and what the same block device
 * calls would cost through esp/main/sd_part.c (hda = sd:N, a card
 * partition read and written raw) with its buffer of 4 to 128 sectors:
 * one card command for each run of that many sectors.  Then a hash of
 * every value the guest read, and of the image after.  A trace's values
 * must be the ones the run read (exit status 1 if not).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "host_glue.h"

/* ==== the counts ==== */

static struct {
    unsigned long rw_calls[2], rw_sectors[2];
    unsigned long f_calls[2];
    unsigned long disk_calls[2];
    unsigned long sdp[6][2];                    /* sd_part.c with 4 << i sectors */
} n;

FRESULT __real_f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT __real_f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
DRESULT __real_ff_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT __real_ff_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);

FRESULT __wrap_f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    n.f_calls[0]++;
    return __real_f_read(fp, buff, btr, br);
}

FRESULT __wrap_f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    n.f_calls[1]++;
    return __real_f_write(fp, buff, btw, bw);
}

DRESULT __wrap_ff_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    n.disk_calls[0]++;
    return __real_ff_disk_read(pdrv, buff, sector, count);
}

DRESULT __wrap_ff_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    n.disk_calls[1]++;
    return __real_ff_disk_write(pdrv, buff, sector, count);
}

/* ==== what disk_image.c and sd_part.c want from the firmware ==== */

#include "disk_image.h"

uint32_t get_uticks(void)
{
    static uint32_t t;
    return t += 10;
}

/* the image is named by FatFs's own path, "0:/IMAGE.IMG" */
bool storage_fatfs_path(const char *path, char *out, size_t size)
{
    if (strncmp(path, "0:/", 3))
        return false;
    snprintf(out, size, "%s", path);
    return true;
}

int storage_card_pdrv(void)
{
    return -1;
}

uint64_t storage_card_sectors(void)
{
    return 0;
}

bool storage_part_mounted(int part)
{
    return false;
}

/* ==== ide.c, as the board builds it ==== */

#include "pci.h"
void *pcmalloc(long size) { return calloc(1, size); }
void tiny386_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("tiny386_fatal: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    exit(3);
}
PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
                               uint16_t vendor_id, uint16_t device_id,
                               uint8_t revision_id, uint16_t class_id)
{
    return NULL;
}
void pci_device_set_config8(PCIDevice *d, uint8_t addr, uint8_t val)
{
}

#define BUILD_ESP32
#include IDE_C

/* ==== the block device's calls, counted ==== */

static BlockDevice *s_bs;
static int (*real_read)(BlockDevice *, uint64_t, uint8_t *, int, BlockDeviceCompletionFunc *, void *);
static int (*real_write)(BlockDevice *, uint64_t, const uint8_t *, int, BlockDeviceCompletionFunc *,
                         void *);

static void count_rw(int w, int sectors)
{
    n.rw_calls[w]++;
    n.rw_sectors[w] += sectors;
    for (int i = 0; i < 6; i++)
        n.sdp[i][w] += (sectors + (4 << i) - 1) / (4 << i);
}

static int counted_read(BlockDevice *bs, uint64_t sector, uint8_t *buf, int k,
                        BlockDeviceCompletionFunc *cb, void *opaque)
{
    count_rw(0, k);
    return real_read(bs, sector, buf, k, cb, opaque);
}

static int counted_write(BlockDevice *bs, uint64_t sector, const uint8_t *buf, int k,
                         BlockDeviceCompletionFunc *cb, void *opaque)
{
    count_rw(1, k);
    return real_write(bs, sector, buf, k, cb, opaque);
}

/* ==== the guest ==== */

static uint64_t hash = 1469598103934665603ull;
static void mix(uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        hash ^= (v >> (8 * i)) & 0xff;
        hash *= 1099511628211ull;
    }
}

static int s_irqs;
static void irq(void *pic, int irq, int level)
{
    if (level)
        s_irqs++;
}

static IDEIFState *ifs;
static uint64_t rng;
static uint32_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t) ((rng * 2685821657736338717ull) >> 32);
}

/* one command at LBA s, c sectors (256 at most), by a guest that moves
   each block with REP INSW or OUTSW; the status after it */
static uint32_t command(int op, uint32_t s, int c, int blk)
{
    static uint8_t buf[2048];
    ide_ioport_write(ifs, 6, 0xE0 | ((s >> 24) & 0x0f));
    ide_ioport_write(ifs, 2, c & 0xff);
    ide_ioport_write(ifs, 3, s & 0xff);
    ide_ioport_write(ifs, 4, (s >> 8) & 0xff);
    ide_ioport_write(ifs, 5, (s >> 16) & 0xff);
    ide_ioport_write(ifs, 7, op);
    bool write = op == 0x30 || op == 0xC5;
    int left = c;
    uint32_t st;
    for (;;) {
        st = ide_ioport_read(ifs, 7);
        mix(st);
        if ((st & 0x01) || !(st & 0x08))
            break;
        int k = left < blk ? left : blk;
        if (write) {
            for (int i = 0; i < k * 512; i++)
                buf[i] = rnd();
            ide_data_write_string(ifs, buf, 2, k * 256);
        } else {
            ide_data_read_string(ifs, buf, 2, k * 256);
            for (int i = 0; i < k * 512; i += 4)
                mix(buf[i] | buf[i + 1] << 8 | buf[i + 2] << 16 | (uint32_t) buf[i + 3] << 24);
        }
        left -= k;
    }
    return st;
}

/* The made-up mix: files read and written in runs, as DOS (READ and WRITE
   SECTORS, one sector a block) and as Windows or Linux (READ and WRITE
   MULTIPLE, 4 a block) do it, the sizes of FAT and directory sectors,
   clusters and big transfers, at places that jump now and then. */
static void run_mix(uint64_t seed, int commands, uint32_t nb)
{
    static const int sizes[] = { 1, 1, 1, 2, 4, 4, 8, 8, 8, 16, 16, 32, 64, 64, 128, 255 };
    rng = seed * 0x9E3779B97F4A7C15ull + 1;
    ide_ioport_write(ifs, 6, 0xE0);
    ide_ioport_write(ifs, 2, 4);
    ide_ioport_write(ifs, 7, 0xC6);
    ide_ioport_read(ifs, 7);
    uint32_t pos = 0;
    for (int i = 0; i < commands; i++) {
        int c = sizes[rnd() % 16];
        if (rnd() % 4 == 0 || pos + c > nb)
            pos = rnd() % (nb - 256);
        int k = rnd() % 8;
        int op = k < 3 ? 0x20 : k < 5 ? 0x30 : k < 7 ? 0xC4 : 0xC5;
        int blk = op == 0x20 || op == 0x30 ? 1 : 4;
        command(op, pos, c, blk);
        pos += c;
    }
}

/* the port traffic ide_trace.c wrote, given to ide.c again; the values
   read must be the ones the run read */
static int run_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 2;
    }
    static char line[20000];
    static uint8_t buf[8192];
    unsigned long lineno = 0, bad = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char op[4];
        unsigned a = 0, b = 0, want = 0;
        int got = sscanf(line, "%3s %x %x %x", op, &a, &b, &want);
        uint32_t v = 0;
        bool check = false;
        if (!strcmp(op, "w") && got >= 3) {
            ide_ioport_write(ifs, a, b);
        } else if (!strcmp(op, "r") && got >= 3) {
            v = ide_ioport_read(ifs, a);
            want = b;
            check = true;
        } else if (!strcmp(op, "c") && got >= 2) {
            ide_cmd_write(ifs, a);
        } else if (!strcmp(op, "s") && got >= 2) {
            v = ide_status_read(ifs);
            want = a;
            check = true;
        } else if (!strcmp(op, "W2") && got >= 2) {
            ide_data_writew(ifs, a);
        } else if (!strcmp(op, "W4") && got >= 2) {
            ide_data_writel(ifs, a);
        } else if (!strcmp(op, "R2") && got >= 2) {
            v = ide_data_readw(ifs);
            want = a;
            check = true;
        } else if (!strcmp(op, "R4") && got >= 2) {
            v = ide_data_readl(ifs);
            want = a;
            check = true;
        } else if (!strcmp(op, "WS") && got >= 3) {
            /* WS size count result hex... */
            char *p = line;
            for (int k = 0; k < 4 && p; k++)
                p = strchr(p + 1, ' ');
            unsigned bytes = a * b;
            if (bytes > sizeof(buf) || !p) {
                bad++;
                continue;
            }
            for (unsigned i = 0; i < bytes; i++) {
                unsigned x = 0;
                sscanf(p + 1 + 2 * i, "%2x", &x);
                buf[i] = x;
            }
            v = ide_data_write_string(ifs, buf, a, b);
            check = true;
        } else if (!strcmp(op, "RS") && got >= 4) {
            /* RS size count result hash */
            unsigned bytes = a * b;
            if (bytes > sizeof(buf)) {
                bad++;
                continue;
            }
            int r = ide_data_read_string(ifs, buf, a, b);
            uint32_t h = 2166136261u;
            for (int i = 0; i < r * (int) a; i++)
                h = (h ^ buf[i]) * 16777619u;
            unsigned wr = 0, wh = 0;
            sscanf(line, "%*s %*x %*x %x %x", &wr, &wh);
            if ((unsigned) r != wr || h != wh) {
                if (bad++ < 5)
                    printf("  trace line %lu: string read %d units hash %08x, the run had %u "
                           "%08x\n", lineno, r, h, wr, wh);
            }
            mix(h);
            continue;
        } else {
            continue;
        }
        if (check) {
            if (!strcmp(op, "WS"))
                sscanf(line, "%*s %*x %*x %x", &want);
            mix(v);
            if (v != want && bad++ < 5)
                printf("  trace line %lu: %s gave %x, the run had %x\n", lineno, op, v, want);
        }
    }
    fclose(f);
    if (bad)
        printf("  %lu of %lu trace lines not as in the run\n", bad, lineno);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: ide_count VOLUME SECTORS mix SEED COMMANDS | trace FILE\n");
        return 2;
    }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 2;
    }
    static sdmmc_card_t card;
    card.csd.capacity = atoi(argv[2]);
    card.csd.sector_size = 512;
    card.fd = fd;
    ff_diskio_register_sdmmc(0, &card);
    ff_sdmmc_set_disk_status_check(0, false);
    static FATFS fs;
    if (f_mount(&fs, "0:", 1) != FR_OK) {
        printf("the volume did not mount\n");
        return 2;
    }
    disk_image_say = NULL;
    ifs = ide_allocate(14, NULL, irq);
    ide_attach(ifs, 0, "0:/IMAGE.IMG");
    s_bs = ifs->drives[0]->bs;
    real_read = s_bs->read_async;
    real_write = s_bs->write_async;
    s_bs->read_async = counted_read;
    s_bs->write_async = counted_write;
    uint32_t nb = (uint32_t) ifs->drives[0]->nb_sectors;
    unsigned c0 = host_card.reads, w0 = host_card.writes;
    memset(&n, 0, sizeof(n));
    int st = 0;
    if (!strcmp(argv[3], "mix") && argc >= 6)
        run_mix(strtoull(argv[4], NULL, 0), atoi(argv[5]), nb);
    else if (!strcmp(argv[3], "trace"))
        st = run_trace(argv[4]);
    else
        return 2;
    ide_flush();
    unsigned cr = host_card.reads - c0, cw = host_card.writes - w0;
    printf("rw %lu %lu sectors %lu %lu f %lu %lu disk %lu %lu card %u %u sdp",
           n.rw_calls[0], n.rw_calls[1], n.rw_sectors[0], n.rw_sectors[1], n.f_calls[0],
           n.f_calls[1], n.disk_calls[0], n.disk_calls[1], cr, cw);
    for (int i = 0; i < 6; i++)
        printf(" %lu %lu", n.sdp[i][0], n.sdp[i][1]);

    /* the image after, read back through FatFs */
    uint64_t ih = 1469598103934665603ull;
    {
        FIL fil;
        memset(&fil, 0, sizeof(fil));
        static uint8_t b[65536];
        UINT got;
        if (f_open(&fil, "0:/IMAGE.IMG", FA_READ) == FR_OK) {
            while (f_read(&fil, b, sizeof(b), &got) == FR_OK && got)
                for (UINT i = 0; i < got; i++)
                    ih = (ih ^ b[i]) * 1099511628211ull;
            f_close(&fil);
        }
    }
    printf(" irqs %d data %016llx image %016llx\n", s_irqs, (unsigned long long) hash,
           (unsigned long long) ih);
    return st;
}
