/*
 * vga_get_info() (vga.c) for the information port: the mode as the guest
 * set it, in text, 640x480 in 16 colours, mode 13h, a VBE mode and blank;
 * and the drawing's counts of VBE pictures, whole pictures, blocks and
 * rows.  vga.c as a board builds it (run.sh gives the board header), with
 * the stand-ins test_vga.c uses.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"
#include "pci.h"

static uint32_t fake_us;
uint32_t get_uticks(void) { return fake_us; }
void *pcmalloc(long size) { return calloc(1, size); }
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

static int checks, failed;
#define CHECK(c, ...) do { \
        checks++; \
        if (!(c)) { \
            failed++; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

#define VRAM (768 * 1024)
static uint8_t vram[VRAM + VGA_MEM_SLACK] __attribute__((aligned(4)));
static uint16_t fb[LCD_WIDTH * LCD_HEIGHT] __attribute__((aligned(4)));

static int bands, rows;
static void redraw(void *opaque, int x, int y, int w, int h)
{
    (void) opaque; (void) x; (void) y; (void) w;
    bands++;
    rows += h;
}

static void crtc(VGAState *s, int index, int value)
{
    vga_ioport_write(s, 0x3d4, index);
    vga_ioport_write(s, 0x3d5, value);
}

/* a BIOS mode's registers */
static void set_regs(VGAState *s, int msr, const uint8_t *sr, const uint8_t *cr,
                     const uint8_t *gr, const uint8_t *ar)
{
    vga_ioport_write(s, 0x3c2, msr);
    for (int i = 0; i < 5; i++) {
        vga_ioport_write(s, 0x3c4, i);
        vga_ioport_write(s, 0x3c5, sr[i]);
    }
    crtc(s, 0x11, 0x0c);
    for (int i = 0; i < 25; i++)
        crtc(s, i, cr[i]);
    for (int i = 0; i < 9; i++) {
        vga_ioport_write(s, 0x3ce, i);
        vga_ioport_write(s, 0x3cf, gr[i]);
    }
    vga_ioport_read(s, 0x3da);
    for (int i = 0; i < 21; i++) {
        vga_ioport_write(s, 0x3c0, i);
        vga_ioport_write(s, 0x3c0, ar[i]);
    }
    vga_ioport_write(s, 0x3c0, 0x20);
}

static void set_mode_12h(VGAState *s)
{
    static const uint8_t cr[25] = {
        0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3, 0xff };
    static const uint8_t ar[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
        0x01, 0x00, 0x0f, 0x00, 0x00 };
    static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x06 };
    static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff };
    set_regs(s, 0xe3, sr, cr, gr, ar);
}

static void set_mode_13h(VGAState *s)
{
    static const uint8_t cr[25] = {
        0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9c, 0x8e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff };
    static const uint8_t ar[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x41, 0x00, 0x0f, 0x00, 0x00 };
    static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x0e };
    static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff };
    set_regs(s, 0x63, sr, cr, gr, ar);
}

static void vbe(VGAState *s, int index, int value)
{
    vbe_write(s, 0, index);
    vbe_write(s, 1, value);
}

int main(void)
{
    VGAState *s = vga_init((char *) vram, VRAM, (uint8_t *) fb, LCD_WIDTH, LCD_HEIGHT);
    struct vga_info vi;

    vga_get_info(s, &vi);
    CHECK(vi.kind == 1 && vi.width == 80 && vi.height == 25 && vi.bpp == 0,
          "text at the start: kind %d %dx%d bpp %d", vi.kind, vi.width, vi.height, vi.bpp);
    CHECK(vi.screen_w == LCD_WIDTH && vi.screen_h == LCD_HEIGHT, "the screen");
    CHECK(vi.vbe_pictures == 0 && vi.vbe_rows == 0, "no VBE picture yet");

    set_mode_12h(s);
    vga_get_info(s, &vi);
    CHECK(vi.kind == 2 && vi.width == 640 && vi.height == 480 && vi.bpp == 4,
          "mode 12h: kind %d %dx%d bpp %d", vi.kind, vi.width, vi.height, vi.bpp);

    set_mode_13h(s);
    vga_get_info(s, &vi);
    CHECK(vi.kind == 2 && vi.width == 320 && vi.height == 200 && vi.bpp == 8,
          "mode 13h: kind %d %dx%d bpp %d", vi.kind, vi.width, vi.height, vi.bpp);

    vga_ioport_read(s, 0x3da);
    vga_ioport_write(s, 0x3c0, 0x00);   /* the attribute index without 20h: blank */
    vga_get_info(s, &vi);
    CHECK(vi.kind == 0, "blank: kind %d", vi.kind);
    vga_ioport_write(s, 0x3c0, 0x00);
    vga_ioport_write(s, 0x3c0, 0x20);

    /* VBE at the screen's size, 16 bits, the start 10 lines down */
    vbe(s, 1, LCD_WIDTH);
    vbe(s, 2, LCD_HEIGHT);
    vbe(s, 3, 16);
    vbe(s, 4, 0x41);
    vbe(s, 9, 10);
    vga_get_info(s, &vi);
    CHECK(vi.kind == 3 && vi.width == LCD_WIDTH && vi.height == LCD_HEIGHT && vi.bpp == 16 &&
          vi.line_bytes == LCD_WIDTH * 2 && vi.start == 10u * LCD_WIDTH * 2,
          "VBE: kind %d %dx%d bpp %d line %u start %u", vi.kind, vi.width, vi.height, vi.bpp,
          vi.line_bytes, vi.start);
#ifdef VGA_DIRTY_BLOCKS
    /* the first picture is whole; a store into one block draws its rows */
    vga_refresh(s, redraw, NULL, 0);
    vga_get_info(s, &vi);
    CHECK(vi.vbe_pictures == 1 && vi.vbe_whole == 1 && vi.vbe_rows == LCD_HEIGHT,
          "first VBE picture whole: %u %u %u", vi.vbe_pictures, vi.vbe_whole, vi.vbe_rows);
    uint32_t blocks0 = vi.vbe_blocks;
    CHECK(blocks0 == 0, "no block found written before: %u", blocks0);
    /* line 100 of the picture (the start is 10 lines in): 1600 bytes, in
       two or three blocks of 1 KiB, or (v4.4) seven or eight of 256 bytes */
    uint32_t a = (10 + 100) * LCD_WIDTH * 2;
    memset(vram + a, 0x5a, LCD_WIDTH * 2);
    vga_dirty_mark(vga_dirty_map(s), a, LCD_WIDTH * 2);
    rows = bands = 0;
    vga_refresh(s, redraw, NULL, 0);
    vga_get_info(s, &vi);
    uint32_t nb = ((a + LCD_WIDTH * 2 - 1) >> VGA_DIRTY_SHIFT) - (a >> VGA_DIRTY_SHIFT) + 1;
    /* the port counts KiB found written (perfport.h), whole KiB of the
       running total */
    uint32_t want = (nb << VGA_DIRTY_SHIFT) >> 10;
    CHECK(vi.vbe_pictures == 2 && vi.vbe_whole == 1, "second picture from blocks: %u %u",
          vi.vbe_pictures, vi.vbe_whole);
    CHECK(vi.vbe_blocks - blocks0 == want, "blocks %u KiB, %u wanted (%u blocks of %u bytes)",
          vi.vbe_blocks - blocks0, want, nb, 1u << VGA_DIRTY_SHIFT);
    CHECK(vi.vbe_rows - LCD_HEIGHT >= 1 && vi.vbe_rows - LCD_HEIGHT <= 3,
          "rows converted %u", vi.vbe_rows - LCD_HEIGHT);
    CHECK(bands == 1 && rows >= 1, "one band to redraw(): %d bands, %d rows", bands, rows);
    /* nothing written: nothing counted but the picture */
    vga_refresh(s, redraw, NULL, 0);
    struct vga_info v2;
    vga_get_info(s, &v2);
    CHECK(v2.vbe_pictures == 3 && v2.vbe_blocks == vi.vbe_blocks && v2.vbe_rows == vi.vbe_rows,
          "a still picture");
    /* full_update: whole again */
    vga_refresh(s, redraw, NULL, 1);
    vga_get_info(s, &v2);
    CHECK(v2.vbe_whole == 2 && v2.vbe_rows == vi.vbe_rows + LCD_HEIGHT, "a full update");
#else
    printf("(no VGA_DIRTY_BLOCKS in this build: the counts are not tried)\n");
#endif

    printf("vga_get_info: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
