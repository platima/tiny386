/*
 * One ide.c, as the board builds it (BUILD_ESP32), behind a table of its
 * entry points (ide_side.h).  run_ide.sh compiles this once for each ide.c
 * it compares and makes every symbol of the object but the table local, so
 * that two of them can be linked into one program:
 *
 *   gcc -DIDE_C='"path/to/ide.c"' -DSIDE=ide_side_a -DSIDE_WHAT='"..."'
 *       [-DIDE_ENABLE_WRITE_GATHER] [-DIDE_ENABLE_READ_AHEAD] -c ide_side.c
 *   objcopy -G ide_side_a ide_side.o
 *
 * The drives' disks are the test's (ide_model_rw()), attached the way
 * ide_attach() attaches an image, without a file.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ide_side.h"

#define BUILD_ESP32
#include IDE_C

static int64_t side_sectors(BlockDevice *bs)
{
    return ide_model_sectors(bs->opaque);
}

static int side_read(BlockDevice *bs, uint64_t sector_num, uint8_t *buf, int n,
                     BlockDeviceCompletionFunc *cb, void *opaque)
{
    return ide_model_rw(bs->opaque, sector_num, buf, n, 0);
}

static int side_write(BlockDevice *bs, uint64_t sector_num, const uint8_t *buf, int n,
                      BlockDeviceCompletionFunc *cb, void *opaque)
{
    return ide_model_rw(bs->opaque, sector_num, (uint8_t *) buf, n, 1);
}

static void *side_create(struct ide_model *d0, struct ide_model *d1, void *pic,
                         void (*set_irq)(void *pic, int irq, int level))
{
    IDEIFState *s = ide_allocate(14, pic, set_irq);
    struct ide_model *d[2] = { d0, d1 };
    for (int i = 0; i < 2; i++) {
        if (!d[i])
            continue;
        BlockDevice *bs = calloc(1, sizeof(*bs));
        bs->opaque = d[i];
        bs->get_sector_count = side_sectors;
        bs->read_async = side_read;
        bs->write_async = side_write;
        s->drives[i] = ide_hddrive_init(s, bs);
    }
    return s;
}

static void side_destroy(void *p)
{
    IDEIFState *s = p;
    for (int i = 0; i < 2; i++) {
        IDEState *d = s->drives[i];
        if (!d)
            continue;
        free(d->bs);
#ifdef IDE_ENABLE_WRITE_GATHER
        free(d->wbuf);
#elif defined(IDE_ENABLE_READ_AHEAD)
        free(d->rbuf);
#endif
        free(d);
    }
    free(s);
}

struct ide_side SIDE = {
    .what = SIDE_WHAT,
#ifdef IDE_ENABLE_WRITE_GATHER
    .gather = WRITE_GATHER_SECTORS,
#endif
#ifdef IDE_ENABLE_READ_AHEAD
    .read_ahead = READ_AHEAD_SECTORS,
#endif
    .create = side_create,
    .destroy = side_destroy,
    .ioport_write = ide_ioport_write,
    .ioport_read = ide_ioport_read,
    .cmd_write = ide_cmd_write,
    .status_read = ide_status_read,
    .data_writew = ide_data_writew,
    .data_readw = ide_data_readw,
    .data_writel = ide_data_writel,
    .data_readl = ide_data_readl,
    .data_write_string = ide_data_write_string,
    .data_read_string = ide_data_read_string,
};
