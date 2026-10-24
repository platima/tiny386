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
 * video memory is noted a block of 256 bytes at a time (1 KiB before v4.4),
 * a byte for each block, and a VBE picture of 8, 15, 16, 24 or 32 bits a
 * pixel is drawn again only in the rows that the blocks written lie in,
 * and (v4.4) drawn 1:1, only in the part of each row they reach (see
 * vga_refresh()).  The
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
/* v4.4: 256 bytes, where it was 1 KiB: with the part of each row drawn,
   a quarter of the pixels converted for the sprite of T386MON's tests, a
   little over half for its text and two thirds for its fills, on the PC
   harness (BUILD-NOTES 29); the map is four times as large (3 KiB at 768K
   of video memory) */
#ifndef VGA_DIRTY_SHIFT
#define VGA_DIRTY_SHIFT 8
#endif
uint8_t *vga_dirty_map(VGAState *s);
void vga_dirty(VGAState *s, uint32_t addr, uint32_t len);
/* With VGA_DIRTY_BLOCKS: 1 (which it starts as) draws VBE pictures from the
   blocks written, 0 every pixel of every picture and no store marked, as
   before v4.0 (the board's [esp] vbe_blocks).  Set before the PC is built
   (the board), or changed between pictures with a picture drawn at each
   setting (test_vbe): a picture drawn while it is 0 makes the next one
   with 1 a whole picture.  While the PC runs, change it only through
   vga_set_dirty_blocks() (the board's Ctrl+Alt+F11), from any core at any
   time: the two VBE pictures after the change are drawn whole, so that a
   store made as it changed, which may have read the old value and not
   marked, is drawn too, even with no picture drawn while it was 0. */
extern int vga_dirty_blocks;
void vga_set_dirty_blocks(int on);

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

/*
 * v4.4: pictures taken at the guest's quiet moments (the board's [esp]
 * pace_quiet and pace_wait_ms, and Ctrl+Alt+F10; the PC harness's -pace).
 * Every store into video memory sets vga_wrote: pc.c's linear frame buffer
 * and vga.c's window, in every mode.  The emulator's core calls
 * vga_pace_after_step() after each of its steps with the guest's
 * instruction count and whether the step ran none (the guest halted); it
 * clears the flag, notes when the guest last wrote, and says whether the
 * guest is quiet now: no store for vga_pace_quiet instructions, or halted.
 * A picture that is due is then drawn at the first quiet step, or late by
 * at most the board's wait (display.c).  vga_pace_quiet 0 is off: the
 * pictures are drawn when they are due, as before v4.4.  vga_wrote and the
 * note belong to the emulator's core alone; vga_pace_quiet is set from the
 * drawing's core between pictures and read with a plain load.
 */
extern uint8_t vga_wrote;
extern uint32_t vga_pace_quiet;
bool vga_pace_after_step(uint32_t insns, bool halted);

typedef struct VGATextOps {
    void (*set_cursor)(void *o, int y, int x);
    void (*put_char2)(void *o, int y, int x, int ch, int attr);
    void (*refresh)(void *o, const uint8_t *ram, int w, int h);
} VGATextOps;

void vga_set_text_ops(VGAState *s, VGATextOps *ops, void *o);

/* The mode the guest has set and what the drawing has done, for the
   information port (perfport.c).  From either thread: each figure is read
   once, so a set may be a picture apart.  kind: 0 blank, 1 text, 2 VGA
   graphics, 3 VBE; width and height in pixels (text: characters); bpp 0 in
   text; line_bytes and start (bytes) for VBE.  The vbe_ counts, from the
   start, wrapping: VBE pictures drawn from the blocks written, of them
   drawn whole, blocks found written, rows converted. */
struct vga_info {
    int kind, width, height, bpp;
    uint32_t line_bytes, start;
    int screen_w, screen_h;
    uint32_t vbe_pictures, vbe_whole, vbe_blocks, vbe_rows;
};
void vga_get_info(VGAState *s, struct vga_info *info);

#endif /* VGA_H */
