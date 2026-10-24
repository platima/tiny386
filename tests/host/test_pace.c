/*
 * Host test of v4.4's pictures at the guest's quiet moments, vga.c's part
 * (vga.h): vga_pace_after_step(), and vga_wrote set by every store through
 * the VGA window in every mode (chain 4, odd/even text, planar, the 16 and
 * 32-bit and string paths) and by nothing else (register writes, reads, a
 * picture).  pc.c's four stores into the linear frame buffer set it too;
 * the PC harness's -pace runs show them doing so (BUILD-NOTES 29).
 * display.c's waiting (FreeRTOS) is the board's.
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"
#include "pci.h"

static uint32_t fake_us;
uint32_t get_uticks(void)
{
	return fake_us;
}

void *pcmalloc(long size)
{
	return calloc(1, size);
}

PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
			       uint16_t vendor_id, uint16_t device_id,
			       uint8_t revision_id, uint16_t class_id)
{
	return NULL;
}

void pci_register_bar(PCIDevice *d, unsigned int bar_num, uint32_t size, int type,
		      void *opaque, void (*set_bar)(void *opaque, int bar_num,
						    uint32_t addr, bool enabled))
{
}

static int failures;

static void check(int ok, const char *what)
{
	printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

#define VRAM (256 * 1024)
static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];
static uint8_t vram[VRAM + VGA_MEM_SLACK];

static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque; (void) x; (void) y; (void) w; (void) h;
}

static void sr(VGAState *s, int i, int v)
{
	vga_ioport_write(s, 0x3c4, i);
	vga_ioport_write(s, 0x3c5, v);
}

static void gr(VGAState *s, int i, int v)
{
	vga_ioport_write(s, 0x3ce, i);
	vga_ioport_write(s, 0x3cf, v);
}

int main(void)
{
	char msg[160];
	uint8_t buf[16] = { 1, 2, 3, 4 };

	printf("vga_pace_after_step()\n");
	vga_pace_quiet = 1000;
	vga_wrote = 0;
	check(vga_pace_after_step(5000, false), "no store yet: quiet (count 5000, last store at 0)");
	vga_wrote = 1;
	check(!vga_pace_after_step(6000, false), "a store in the step: not quiet");
	check(vga_wrote == 0, "the flag cleared by the step");
	check(!vga_pace_after_step(6999, false), "999 instructions since: not quiet");
	check(vga_pace_after_step(7000, false), "1000 since: quiet");
	vga_wrote = 1;
	check(!vga_pace_after_step(7100, true), "a store, then halted in the same step: not quiet");
	check(vga_pace_after_step(7100, true), "halted with no store: quiet at once");
	vga_wrote = 1;
	vga_pace_after_step(0xfffffff0u, false);
	check(vga_pace_after_step(0x00000400u, false), "across the count's wrap: quiet after 1040");

	printf("which stores set vga_wrote\n");
	memset(vram, 0, sizeof(vram));
	VGAState *s = vga_init((char *) vram, VRAM, (uint8_t *) fb, LCD_WIDTH, LCD_HEIGHT);
	vga_wrote = 0;
	vga_ioport_write(s, 0x3c2, 0x67);       /* misc output: RAM enabled */
	sr(s, 2, 0x03);
	gr(s, 5, 0x10);
	vga_ioport_read(s, 0x3da);
	vga_mem_read(s, 0x18000);
	fake_us += 30000;
	vga_refresh(s, redraw, NULL, 1);
	check(vga_wrote == 0, "register writes, a read and a picture leave it clear");
	vga_mem_write(s, 0x18000, 'A');         /* odd/even: the text */
	check(vga_wrote == 1, "a text store (odd/even) sets it");
	vga_wrote = 0;
	gr(s, 5, 0x00);
	gr(s, 6, 0x05);                         /* graphics, A000h */
	sr(s, 4, 0x06);
	sr(s, 2, 0x0f);
	vga_mem_write(s, 0x100, 0xff);
	check(vga_wrote == 1, "a planar store sets it");
	vga_wrote = 0;
	sr(s, 4, 0x0e);                         /* chain 4 */
	vga_mem_write(s, 0x200, 7);
	snprintf(msg, sizeof(msg), "a chain-4 byte store sets it");
	check(vga_wrote == 1, msg);
	vga_wrote = 0;
	vga_mem_write16(s, 0x204, 0x1234);
	check(vga_wrote == 1, "a 16-bit store sets it");
	vga_wrote = 0;
	vga_mem_write32(s, 0x208, 0x12345678);
	check(vga_wrote == 1, "a 32-bit store sets it");
	vga_wrote = 0;
	vga_mem_write_string(s, 0x210, buf, sizeof(buf));
	check(vga_wrote == 1, "a string store sets it");
	vga_wrote = 0;
	vga_ioport_write(s, 0x3c2, 0x65);       /* misc output: RAM disabled */
	vga_mem_write(s, 0x300, 1);
	check(vga_wrote == 0, "a store with video memory switched off (it goes nowhere) does not");

	if (failures) {
		printf("test_pace: %d checks FAILED\n", failures);
		return 1;
	}
	printf("test_pace: all checks passed\n");
	return 0;
}
