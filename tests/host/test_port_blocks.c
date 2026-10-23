/*
 * The information port and Ctrl+Alt+F11 together (v4.3): perfport.c and
 * vga.c as a board builds it (run.sh gives the board header), each picture
 * drawn as pc_vga_step() draws it with the port (through perfport_redraw(),
 * then perfport_picture()), and vbe_blocks turned the other way as
 * speed.c does on Ctrl+Alt+F11 (vga_set_dirty_blocks(!vga_dirty_blocks)).
 * A guest's snapshot must give the setting as it is at that moment, with or
 * without a picture since, and the VBE counts must show the two whole
 * pictures v4.2 draws after each change.
 */
#define TINY386_PERFPORT
#include "../../perfport.c"

#include <stdio.h>
#include <stdlib.h>

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

/* ==== the rest of the emulator, pretended (vga.c is the real one) ==== */
static uint32_t fake_now = 1000;
uint32_t get_uticks() { return fake_now; }
long cpui386_get_cycle(CPUI386 *cpu) { (void) cpu; return 0; }
int i8254_max_owed = 1;
uint32_t i8254_owed_max, i8254_dropped;
volatile unsigned ps2_key_events, ps2_mouse_reports, ps2_mouse_packets;
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

#define VRAM (768 * 1024)
static uint8_t vram[VRAM + VGA_MEM_SLACK] __attribute__((aligned(4)));
static uint16_t fb[LCD_WIDTH * LCD_HEIGHT] __attribute__((aligned(4)));
static char guest_mem[0x1000];
static PC pc;

static int rows;
static void panel(void *opaque, int x, int y, int w, int h)
{
    (void) opaque; (void) x; (void) y; (void) w;
    rows += h;
}

/* ==== the guest's side ==== */
static void outw(unsigned port, uint32_t v) { perfport_io_write(&pc, port - PP_PORT_BASE, v & 0xffff, 2); }
static uint32_t ind(unsigned port) { return perfport_io_read(&pc, port - PP_PORT_BASE, 4); }
static uint32_t rd(unsigned i)
{
    outw(PP_PORT_INDEX, i);
    return ind(PP_PORT_DATA);
}
static void snap(void) { outw(PP_PORT_CMD, PP_CMD_SNAPSHOT); }
static int valid_bit(unsigned i) { return (rd(PP_I_VALID0 + i / 32) >> (i % 32)) & 1; }

/* ==== the board's side ==== */
/* a picture as pc_vga_step() draws it with TINY386_PERFPORT, 30 ms on */
static void picture(void)
{
    fake_now += 30000;
    uint32_t t0 = get_uticks();
    vga_refresh(pc.vga, perfport_redraw, &pc, 0);
    perfport_picture(t0, get_uticks() - t0);
}

/* Ctrl+Alt+F11, as speed_screen_poll() takes it between two pictures */
static void ctrl_alt_f11(void)
{
    vga_set_dirty_blocks(!vga_dirty_blocks);
}

static void vbe(VGAState *s, int index, int value)
{
    vbe_write(s, 0, index);
    vbe_write(s, 1, value);
}

/* a line of the picture written by the guest through the linear frame
   buffer, marked as pc.c's stores mark it (unless vbe_blocks is 0) */
static void guest_line(int line, uint8_t v)
{
    uint32_t a = (uint32_t) line * LCD_WIDTH * 2;
    memset(vram + a, v, LCD_WIDTH * 2);
    if (vga_dirty_blocks)
        vga_dirty_mark(vga_dirty_map(pc.vga), a, LCD_WIDTH * 2);
}

struct counts { uint32_t on, pictures, whole, blocks; };
static struct counts now(void)
{
    struct counts c;
    snap();
    c.on = rd(PP_I_VBE_BLOCKS_ON);
    c.pictures = rd(PP_I_VBE_PICTURES);
    c.whole = rd(PP_I_VBE_WHOLE);
    c.blocks = rd(PP_I_VBE_BLOCKS);
    return c;
}

int main(void)
{
    perfport_reset();
    VGAState *s = vga_init((char *) vram, VRAM, (uint8_t *) fb, LCD_WIDTH, LCD_HEIGHT);
    memset(&pc, 0, sizeof(pc));
    pc.vga = s;
    pc.phys_mem = guest_mem;
    pc.phys_mem_size = 14L << 20;
    pc.vga_mem_size = VRAM;
    pc.redraw = panel;
    pc.redraw_data = NULL;
    vga_dirty_blocks = 1;               /* [esp] vbe_blocks = 1, the default */

    /* Windows at 800x480 in 16 bits */
    vbe(s, 1, 800);
    vbe(s, 2, 480);
    vbe(s, 3, 16);
    vbe(s, 4, 0x41);
    picture();
    struct counts c = now();
    CHECK(rd(PP_I_MAGIC) == PP_MAGIC && valid_bit(PP_I_VBE_BLOCKS_ON), "the port and its bit");
    CHECK(c.on == 1 && c.pictures == 1 && c.whole == 1,
          "at the start: on %u, VBE pictures %u, whole %u", c.on, c.pictures, c.whole);
    picture();
    c = now();
    CHECK(c.on == 1 && c.pictures == 2 && c.whole == 1, "a still picture: %u %u %u",
          c.on, c.pictures, c.whole);

    /* Ctrl+Alt+F11: 0, read with no picture since */
    struct counts b = c;
    ctrl_alt_f11();
    c = now();
    CHECK(c.on == 0, "after Ctrl+Alt+F11, before any picture: vbe_blocks %u, 0 wanted", c.on);
    CHECK(c.pictures == b.pictures && c.whole == b.whole, "nothing drawn by the change itself");
    /* the snapshot holds: the setting changed after it is not seen until
       the next snapshot */
    ctrl_alt_f11();
    CHECK(rd(PP_I_VBE_BLOCKS_ON) == 0, "the last snapshot's value until the next one");
    c = now();
    CHECK(c.on == 1, "Ctrl+Alt+F11 again, no picture between: %u, 1 wanted", c.on);
    ctrl_alt_f11();
    c = now();
    CHECK(c.on == 0, "and again: %u, 0 wanted", c.on);

    /* vbe_blocks 0: the old loop draws every picture whole, and no VBE
       picture is counted from the blocks */
    b = c;
    for (int i = 0; i < 3; i++) {
        guest_line(100 + i, 0x11 * (i + 1));
        rows = 0;
        picture();
        CHECK(rows > 0, "vbe_blocks 0, picture %d: rows went to the panel", i);
    }
    c = now();
    CHECK(c.on == 0 && c.pictures == b.pictures && c.whole == b.whole,
          "vbe_blocks 0: on %u, VBE pictures %u (%u before), whole %u (%u)", c.on,
          c.pictures, b.pictures, c.whole, b.whole);
    CHECK(rd(PP_I_CHANGED) >= 3, "the pictures that changed are counted: %u", rd(PP_I_CHANGED));

    /* back to 1: on at once; the two pictures after the change whole, then
       from the blocks again */
    ctrl_alt_f11();
    c = now();
    CHECK(c.on == 1, "Ctrl+Alt+F11 back to 1: %u", c.on);
    b = c;
    picture();
    c = now();
    CHECK(c.on == 1 && c.pictures == b.pictures + 1 && c.whole == b.whole + 1,
          "the first picture after the change whole: pictures %u, whole %u (%u, %u before)",
          c.pictures, c.whole, b.pictures, b.whole);
    picture();
    c = now();
    CHECK(c.pictures == b.pictures + 2 && c.whole == b.whole + 2,
          "the second whole too: pictures %u, whole %u", c.pictures, c.whole);
    b = c;
    guest_line(200, 0x5a);
    rows = 0;
    picture();
    c = now();
    CHECK(c.on == 1 && c.pictures == b.pictures + 1 && c.whole == b.whole &&
          c.blocks > b.blocks && rows >= 1 && rows <= 3,
          "then from the blocks: pictures %u, whole %u (%u), blocks %u (%u), rows %d",
          c.pictures, c.whole, b.whole, c.blocks, b.blocks, rows);

    /* a change at each of 20 pictures, read after each: always the setting
       of that moment */
    int wrong = 0;
    for (int i = 0; i < 20; i++) {
        ctrl_alt_f11();
        snap();
        if (rd(PP_I_VBE_BLOCKS_ON) != (uint32_t) vga_dirty_blocks)
            wrong++;
        picture();
    }
    CHECK(wrong == 0, "20 changes, each read at once: %d wrong", wrong);

    printf("perfport and Ctrl+Alt+F11: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
