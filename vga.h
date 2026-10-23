#ifndef VGA_H
#define VGA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct FBDevice FBDevice;

typedef void SimpleFBDrawFunc(void *opaque,
                              int x, int y, int w, int h);

typedef struct VGAState VGAState;
VGAState *vga_init(char *vga_ram, int vga_ram_size,
                   uint8_t *fb, int width, int height);
void vga_set_force_8dm(VGAState *s, int v);

int vga_step(VGAState *vga);
void vga_refresh(VGAState *s,
                 SimpleFBDrawFunc *redraw_func, void *opaque, int full_update);

void vga_ioport_write(VGAState *s, uint32_t addr, uint32_t val);
uint32_t vga_ioport_read(VGAState *s, uint32_t addr);

void vbe_write(VGAState *s, uint32_t offset, uint32_t val);
uint32_t vbe_read(VGAState *s, uint32_t offset);

/* bytes behind video memory that a line of the picture may be read into */
#define VGA_MEM_SLACK 16384

void vga_mem_write(VGAState *s, uint32_t addr, uint8_t val);
uint8_t vga_mem_read(VGAState *s, uint32_t addr);
void vga_mem_write16(VGAState *s, uint32_t addr, uint16_t val);
void vga_mem_write32(VGAState *s, uint32_t addr, uint32_t val);
bool vga_mem_write_string(VGAState *s, uint32_t addr, uint8_t *buf, int len);

/*
 * VGA_DIRTY_BLOCKS (a board header turns it on): what the guest writes into
 * video memory is noted a block of 1 KiB at a time, a byte for each block,
 * and a VBE picture of 8, 15, 16, 24 or 32 bits a pixel is drawn again only
 * in the rows that the blocks written lie in (see vga_refresh()).  The
 * stores that such a picture can show say so: the chain-4 stores of
 * vga_mem_write*() in vga.c while VBE is on, and the linear frame buffer
 * in pc.c, which stores straight into video memory and then marks the map
 * that vga_dirty_map() gives with vga_dirty_mark().  Neither marks
 * anything while vga_dirty_blocks is 0, and vga.c marks nothing while VBE
 * is off (mode 13h and the other 256-colour modes of the BIOS, whose
 * pictures never read the marks): turning VBE on asks for a clear, and so
 * for a picture drawn in full, before the marks are read again.
 * vga_dirty_map() is NULL without VGA_DIRTY_BLOCKS.  vga_dirty() marks
 * for anything else that writes video memory behind vga.c's back.
 *
 * The drawing may run on the other core (VGA_TWO_CORES): it clears a
 * block's byte and then reads the block.  So the byte is set after the
 * bytes it stands for are stored, with a release fence between them, and
 * a store that lands after the drawing has read its block leaves the byte
 * set for the next picture.
 */
#define VGA_DIRTY_SHIFT 10
uint8_t *vga_dirty_map(VGAState *s);
void vga_dirty(VGAState *s, uint32_t addr, uint32_t len);
/* With VGA_DIRTY_BLOCKS: 1 (which it starts as) draws VBE pictures from the
   blocks written, 0 every pixel of every picture and no store marked, as
   before v4.0 (the board's [esp] vbe_blocks).  Set before the PC is built
   (the board), or changed between pictures with a picture drawn at each
   setting (test_vbe): a picture drawn while it is 0 makes the next one
   with 1 a whole picture. */
extern int vga_dirty_blocks;

/* Bytes addr to addr + len - 1 of video memory were written: len 1 or
   more, all of them inside video memory. */
static inline void vga_dirty_mark(uint8_t *map, uint32_t addr, uint32_t len)
{
    uint32_t b = addr >> VGA_DIRTY_SHIFT;
    uint32_t e = (addr + len - 1) >> VGA_DIRTY_SHIFT;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&map[b], 1, __ATOMIC_RELAXED);
    while (b < e)
        __atomic_store_n(&map[++b], 1, __ATOMIC_RELAXED);
}

typedef struct PCIDevice PCIDevice;
typedef struct PCIBus PCIBus;
PCIDevice *vga_pci_init(VGAState *s, PCIBus *bus,
                        void *o, void (*set_bar)(void *, int, uint32_t, bool));

#ifndef BPP
#define BPP 32
#endif

/* The drawing from blocks written (VGA_DIRTY_BLOCKS above) is for a screen
   of RGB565 drawn 1:1, whose changed rows go to redraw(); vga.c and pc.c
   both see this. */
#if defined(VGA_DIRTY_BLOCKS) && (BPP != 16 || !defined(VGA_REDRAW_DIRTY_ROWS) || \
    defined(SCALE_3_2) || defined(SCALE_2_1) || defined(SWAPXY) || \
    defined(HOST_WORDS_BIGENDIAN))
#undef VGA_DIRTY_BLOCKS
#endif

/* In a build with BPP 16 and VGA_STORE_CHANGED: 0 draws the 16-colour modes
   pixel by pixel like every other mode, 1 (which it starts as) the quick
   way.  See vga_graphic_refresh(). */
extern int vga_planar_fast;

typedef struct VGATextOps {
    void (*set_cursor)(void *o, int y, int x);
    void (*put_char2)(void *o, int y, int x, int ch, int attr);
    void (*refresh)(void *o, const uint8_t *ram, int w, int h);
} VGATextOps;

void vga_set_text_ops(VGAState *s, VGATextOps *ops, void *o);

#endif /* VGA_H */
