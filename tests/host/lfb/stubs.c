/*
 * What the emulator's core wants from the board's side (esp/main) when it
 * is built with the Korvo-1's header, for test_lfb.c: no disks, no card,
 * no network, one thread.  None of it is reached by test_lfb.c's guests.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pc.h"
#include "esp/main/disk_image.h"
#include "esp/main/sd_part.h"
#include "freertos/semphr.h"

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
	(void) phys_mem; (void) file; (void) addr; (void) backward;
	return 0;
}

void tiny386_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(3);
}

void (*_Atomic esp32_send_packet)(uint8_t *buf, int size);

int disk_image_open(const char *path, bool writable, struct disk_image **out, uint64_t *size)
{
	(void) path; (void) writable; (void) out; (void) size;
	return -1;
}
const char *disk_image_error(void) { return "no disks in test_lfb"; }
int disk_image_read(struct disk_image *d, uint64_t o, void *b, size_t n)
{ (void) d; (void) o; (void) b; (void) n; return -1; }
int disk_image_write(struct disk_image *d, uint64_t o, const void *b, size_t n)
{ (void) d; (void) o; (void) b; (void) n; return -1; }
int disk_image_sync(struct disk_image *d) { (void) d; return 0; }
void disk_image_close(struct disk_image *d) { (void) d; }

int sd_part_name(const char *name) { (void) name; return 0; }
int sd_part_open(int part, struct sd_part **out, char *why, size_t size)
{ (void) part; (void) out; snprintf(why, size, "no card in test_lfb"); return -1; }
uint64_t sd_part_sectors(const struct sd_part *p) { (void) p; return 0; }
int sd_part_read(struct sd_part *p, uint64_t lba, void *buf, unsigned n)
{ (void) p; (void) lba; (void) buf; (void) n; return -1; }
int sd_part_write(struct sd_part *p, uint64_t lba, const void *buf, unsigned n)
{ (void) p; (void) lba; (void) buf; (void) n; return -1; }
int sd_part_sync(struct sd_part *p) { (void) p; return 0; }

/* one thread: a mutex is never contended */
struct HostMutex {
	int held;
};
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	return calloc(1, sizeof(struct HostMutex));
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks)
{
	(void) ticks;
	m->held++;
	return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
	m->held--;
	return pdTRUE;
}
