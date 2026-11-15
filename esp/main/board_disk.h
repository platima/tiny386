/*
 * IDE_BOARD_DISK: a hard disk the board gives the guest, named in an ini
 * by something that is no file (the ESP-Mosaico's NAND, "hda = nand:",
 * nand_disk.c).  ide.c and esp_main.c call these on a board that defines
 * IDE_BOARD_DISK in its header.
 */
#ifndef BOARD_DISK_H
#define BOARD_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* name is the board's disk ("nand:") */
bool board_disk_name(const char *name);
/* esp_main.c's check of the ini, before the PC is built: 0 with a line
   for the console in info, or -1 with why in plain words */
int board_disk_check(int drive, bool iscd, char *info, size_t isize, char *why, size_t wsize);
/* ide.c: the disk, or NULL with why */
void *board_disk_open(const char *name, char *why, size_t size);
uint64_t board_disk_sectors(void *d);
/* n sectors at lba: 0, or -1 */
int board_disk_read(void *d, uint64_t lba, void *buf, unsigned n);
int board_disk_write(void *d, uint64_t lba, const void *buf, unsigned n);
int board_disk_sync(void *d);

#endif /* BOARD_DISK_H */
