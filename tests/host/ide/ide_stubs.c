/*
 * What ide.c, built as the board builds it (BUILD_ESP32), links against in
 * the firmware: the device state's memory, the fatal stop, the PCI device,
 * and the card's disk images and partitions.  The tests here attach their
 * own disks (ide_side.c), so the card's are never opened; these only say
 * no.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pci.h"
#include "esp/main/disk_image.h"
#include "esp/main/sd_part.h"

void *pcmalloc(long size)
{
    return calloc(1, size);
}

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

int disk_image_open(const char *path, bool writable, struct disk_image **out, uint64_t *size)
{
    return DISK_IMAGE_NOT_HERE;
}

const char *disk_image_error(void)
{
    return "not here";
}

int disk_image_read(struct disk_image *d, uint64_t offset, void *buf, size_t n)
{
    return -1;
}

int disk_image_write(struct disk_image *d, uint64_t offset, const void *buf, size_t n)
{
    return -1;
}

int disk_image_sync(struct disk_image *d)
{
    return -1;
}

void disk_image_close(struct disk_image *d)
{
}

int sd_part_name(const char *name)
{
    return 0;
}

int sd_part_open(int part, struct sd_part **out, char *why, size_t size)
{
    return -1;
}

uint64_t sd_part_sectors(const struct sd_part *p)
{
    return 0;
}

int sd_part_read(struct sd_part *p, uint64_t lba, void *buf, unsigned n)
{
    return -1;
}

int sd_part_write(struct sd_part *p, uint64_t lba, const void *buf, unsigned n)
{
    return -1;
}

int sd_part_sync(struct sd_part *p)
{
    return -1;
}
