/*
 * perfport.h: tiny386's information port, the protocol.
 *
 * A program inside the emulated PC reads the emulator's own counters
 * through eight I/O ports, 0F1F8h to 0F1FFh: how fast the guest runs, how
 * long the board takes to draw each picture, the display mode, memory,
 * the disks, the guest's timer, the keyboard and mouse.  Reads have no
 * effect on the emulator or on the guest; the one command takes a
 * snapshot, so that a set of values read one by one belongs together.
 *
 * This header is all a guest program needs: plain C89, no other headers,
 * every value fits an unsigned long, so that 16-bit compilers (int of 16
 * bits) and 32-bit ones take it alike.  The emulator's own part is at the
 * end, behind TINY386_PERFPORT, which a guest program does not define.
 *
 *   Port    Width  Way    What
 *   0F1F8h  16     write  INDEX: which value DATA gives (8 bits: the index
 *                         is that byte; 32 bits: the low 16 bits are the
 *                         index, then the high 16 bits are a COMMAND)
 *   0F1F8h  16     read   INDEX as last written
 *   0F1FAh  16     write  COMMAND (PP_CMD_*; 8 bits works too)
 *   0F1FAh  16     read   snapshots taken so far, the low 16 bits
 *   0F1FCh  32     read   DATA: the value at INDEX; at 0F1FCh and 0F1FEh
 *                         16 bits give its low and high halves, at 0F1FCh
 *                         to 0F1FFh 8 bits its four bytes
 *
 * Writes to DATA do nothing.  A read past 0F1FFh (32 bits at 0F1FEh) gives
 * 0FFh for each byte beyond.  A PC without this port gives 0FFFFFFFFh (or
 * whatever its bus floats to) for DATA, never PP_MAGIC.
 *
 * To find it:   out 0F1F8h, PP_I_MAGIC (16 bits); in 0F1FCh (32 bits);
 *               the value is PP_MAGIC.  Then PP_I_VERSION: the major
 *               version (high 16 bits) must be PP_VERSION_MAJOR.
 * To read:      out 0F1FAh, PP_CMD_SNAPSHOT; then for each value wanted,
 *               out 0F1F8h, index; in 0F1FCh.
 *
 * PP_I_MAGIC, PP_I_VERSION, PP_I_COUNT and PP_I_SNAPSHOTS are read live.
 * Every other value is the snapshot's: 0 before the first snapshot, and 0
 * for a value this build does not have (its bit in PP_I_VALID0 to 3 is
 * clear) or an index it does not know (PP_I_COUNT or more).  A minor
 * version adds indexes and never changes the meaning of one.
 *
 * Counters (the PP_I_* marked "total") count from the start and wrap at
 * 2^32: a program takes the difference of two snapshots, modulo 2^32, and
 * divides by the difference of PP_I_CLOCK_US (also modulo 2^32) for a rate.
 * Figures marked "last whole second" are the emulator's own, the same as
 * the board's Ctrl+Alt+F12 screen shows.
 *
 * Pictures: the board draws one every 30 ms (FRAME_MS) whether or not
 * anything on the screen changed, so PICTURES_1S reads 33 or 34 on a still
 * desktop and falls only when the drawing cannot keep up.  The panel's
 * frames a second, as a game would count them, are CHANGED_1S (1.1 on):
 * the pictures in which at least one row went to the panel.
 *
 * How busy the guest is: PP_I_HALT_PCT is the share of the last second in
 * which the guest's processor was halted, waiting for an interrupt.  Windows
 * 95 halts when it has nothing to do (on the PC harness: 67 to 89 per cent
 * at an idle desktop, 0 while it starts or works), so under Windows 100
 * less it is the guest's load, as a task manager would show it.  Under DOS
 * it depends on the program: many never halt, and show 0 even when idle.
 */
#ifndef PERFPORT_H
#define PERFPORT_H

#define PP_PORT_BASE            0xF1F8u
#define PP_PORT_INDEX           0xF1F8u
#define PP_PORT_CMD             0xF1FAu
#define PP_PORT_DATA            0xF1FCu
#define PP_PORT_COUNT           8       /* 0F1F8h to 0F1FFh */

#define PP_MAGIC                0x36383354UL    /* "T386" as four bytes */
#define PP_VERSION_MAJOR        1
#define PP_VERSION_MINOR        1       /* 1.1: CHANGED, CHANGED_1S */
#define PP_VERSION              0x00010001UL    /* major << 16 | minor */

/* COMMAND */
#define PP_CMD_NONE             0       /* nothing */
#define PP_CMD_SNAPSHOT         1       /* copy every value now */

/* ==== the indexes ==== */

/* identity, read live */
#define PP_I_MAGIC              0x00    /* PP_MAGIC */
#define PP_I_VERSION            0x01    /* PP_VERSION of this build */
#define PP_I_COUNT              0x02    /* indexes this build knows: 0 to COUNT - 1 */
#define PP_I_SNAPSHOTS          0x03    /* snapshots taken since the emulator started */

/* the snapshot itself */
#define PP_I_VALID0             0x04    /* bit n: index n has a value in this build */
#define PP_I_VALID1             0x05    /* bit n: index 32 + n */
#define PP_I_VALID2             0x06    /* bit n: index 64 + n */
#define PP_I_VALID3             0x07    /* bit n: index 96 + n */
#define PP_I_SOURCE             0x08    /* PP_SOURCE_*: who filled the values */
#define PP_I_CLOCK_US           0x09    /* the emulator's microsecond clock (wraps
                                           every 71.6 minutes; on the PC harness
                                           its virtual time) */
#define PP_I_UPTIME_MS          0x0A    /* ms since the board started */
#define PP_I_PC_UPTIME_MS       0x0B    /* ms since the emulated PC was switched on */
#define PP_I_SNAP_US            0x0C    /* how long this snapshot took, us */
#define PP_I_PORT_IO            0x0D    /* total: reads and writes of these ports */

/* the guest's processor */
#define PP_I_INSNS_LO           0x10    /* guest instructions since the PC started, */
#define PP_I_INSNS_HI           0x11    /*   low and high 32 bits */
#define PP_I_KIPS               0x12    /* last whole second: thousand instructions a
                                           second of running (halted time left out) */
#define PP_I_HALT_PCT           0x13    /* last whole second: per cent halted */
#define PP_I_RUN_MS             0x14    /* total: ms the guest ran */
#define PP_I_HALT_MS            0x15    /* total: ms the guest was halted */
#define PP_I_SECONDS            0x16    /* whole seconds counted for KIPS and HALT_PCT */

/* the drawing (on the board, its other core) */
#define PP_I_PICTURES           0x20    /* total: pictures drawn, changed or not
                                           (on the board one every FRAME_MS) */
#define PP_I_PICTURES_1S        0x21    /* last whole second: pictures drawn */
#define PP_I_DRAW_US            0x22    /* total: us spent drawing them */
#define PP_I_DRAW_AVG_US_1S     0x23    /* last whole second: us a picture, average */
#define PP_I_DRAW_MAX_US_1S     0x24    /* last whole second: the longest picture, us */
#define PP_I_DRAW_MAX_US        0x25    /* the longest picture since the start, us */
#define PP_I_FLUSHES            0x26    /* total: bands of changed rows sent to the
                                           screen (on the board, write-backs from the
                                           cache to the panel's frame buffer) */
#define PP_I_FLUSH_ROWS         0x27    /* total: rows in them */
#define PP_I_VBE_PICTURES       0x28    /* total: VBE pictures drawn from the blocks
                                           of video memory written */
#define PP_I_VBE_WHOLE          0x29    /* total: of those, drawn whole */
#define PP_I_VBE_BLOCKS         0x2A    /* total: 1 KiB blocks found written */
#define PP_I_VBE_ROWS           0x2B    /* total: rows of them converted */
#define PP_I_VBE_BLOCKS_ON      0x2C    /* 1: [esp] vbe_blocks is on; 0: off */
#define PP_I_FRAME_MS           0x2D    /* a picture at most every this many ms */
#define PP_I_CHANGED            0x2E    /* 1.1, total: pictures in which at least
                                           one row went to the screen */
#define PP_I_CHANGED_1S         0x2F    /* 1.1, last whole second: those pictures,
                                           the panel's frames a second */

/* the display mode the guest has set */
#define PP_I_MODE_KIND          0x30    /* PP_MODE_* */
#define PP_I_MODE_WIDTH         0x31    /* pixels (text: characters) */
#define PP_I_MODE_HEIGHT        0x32    /* pixels (text: rows) */
#define PP_I_MODE_BPP           0x33    /* bits a pixel: 2, 4, 8, 15, 16, 24, 32; text 0 */
#define PP_I_MODE_LINE_BYTES    0x34    /* VBE: bytes a line in video memory */
#define PP_I_MODE_START         0x35    /* VBE: the display start, bytes */
#define PP_I_BIOS_MODE          0x36    /* the BIOS's mode byte at 0040:0049 */
#define PP_I_SCREEN_WIDTH       0x37    /* the panel or picture the PC is shown on */
#define PP_I_SCREEN_HEIGHT      0x38

/* memory */
#define PP_I_GUEST_MEM_KB       0x40    /* the guest's memory (mem_size) */
#define PP_I_VGA_MEM_KB         0x41    /* video memory (vga_mem_size) */
#define PP_I_PSRAM_FREE_KB      0x42    /* the board's PSRAM free now */
#define PP_I_PSRAM_LARGEST_KB   0x43    /*   its largest free block */
#define PP_I_IRAM_FREE_KB       0x44    /* the board's internal RAM free now */
#define PP_I_IRAM_LARGEST_KB    0x45    /*   its largest free block */
#define PP_I_IRAM_MIN_FREE_KB   0x46    /*   the least free since the board started */

/* the disks: the hard disk and CD images (floppies only in DISK_ACTIVITY
   and DISK_REFUSED) */
#define PP_I_DISK_READS         0x50    /* total: reads of the images */
#define PP_I_DISK_WRITES        0x51    /* total: writes */
#define PP_I_DISK_READ_SECTORS  0x52    /* total: 512-byte sectors read */
#define PP_I_DISK_WRITE_SECTORS 0x53    /* total: sectors written */
#define PP_I_DISK_BUSY_US       0x54    /* total: us the emulator waited for them */
#define PP_I_DISK_LONGEST_US    0x55    /* the longest wait since the start, us */
#define PP_I_DISK_OVER_TICK     0x56    /* total: waits longer than a timer tick (55 ms) */
#define PP_I_DISK_ERRORS        0x57    /* total: accesses the card refused at first */
#define PP_I_DISK_RECOVERED     0x58    /* total: of those, done after opening again */
#define PP_I_DISK_REFUSED       0x59    /* total: errors the guest was told of */
#define PP_I_DISK_FLUSHES       0x5A    /* total: flushes to the card after 2 s quiet */
#define PP_I_DISK_ACTIVITY      0x5B    /* total: reads and writes of every disk, CD
                                           and floppy (the status LED's disk light) */

/* the guest's timer (channel 0 of the 8254) */
#define PP_I_TIMER_OWED_MAX     0x60    /* the most ticks owed to the guest at once */
#define PP_I_TIMER_DROPPED      0x61    /* total: ticks dropped after a hold-up */
#define PP_I_TIMER_CATCHUP      0x62    /* [esp] timer_catchup */

/* the keyboard and mouse, as the PS/2 side sees them */
#define PP_I_KEY_EVENTS         0x64    /* total: key presses and releases */
#define PP_I_MOUSE_REPORTS      0x65    /* total: mouse reports handed in */
#define PP_I_MOUSE_PACKETS      0x66    /* total: mouse packets sent to the guest */

/* the board */
#define PP_I_LOG_HELD           0x68    /* total: repeated log lines held back */
#define PP_I_LOG_DROPPED        0x69    /* total: lines dropped from the UART's queue */
#define PP_I_STATUS             0x6A    /* the status LED: stage | error << 8 |
                                           the last run's end << 16 (STATUS-LED.md) */
#define PP_I_CPU_MHZ            0x6B    /* the board's processor clock */

/* text: 4 characters an index, the first in the low byte, padded with 0;
   31 characters at most, then a 0 */
#define PP_I_FIRMWARE           0x70    /* 0x70 to 0x77: the firmware's version
                                           (esp/version.txt, "v4.1-perfport") */
#define PP_I_BOARD              0x78    /* 0x78 to 0x7F: the board's name */
#define PP_TEXT_INDEXES         8       /* 32 bytes each */

#define PP_COUNT                0x80    /* PP_I_COUNT in this version; VALID0 to 3
                                           cover every index below it */

/* PP_I_SOURCE */
#define PP_SOURCE_NONE          0       /* no snapshot yet */
#define PP_SOURCE_BOARD         1       /* the ESP32 firmware */
#define PP_SOURCE_HARNESS       2       /* the PC harness (virtual time) */

/* PP_I_MODE_KIND */
#define PP_MODE_BLANK           0
#define PP_MODE_TEXT            1
#define PP_MODE_VGA             2       /* a VGA graphics mode (16 or 256 colours) */
#define PP_MODE_VBE             3       /* a VBE mode (Bochs DISPI) */

/* ==== the emulator's part (perfport.c) ==== */
#ifdef TINY386_PERFPORT
#include <stdint.h>

#define PP_VALID_WORDS          4

/* A guest's in and out at PP_PORT_BASE + offset, size 1, 2 or 4 bytes (pc.c);
   on the emulator's thread.  pc is the PC of pc.h. */
uint32_t perfport_io_read(void *pc, int offset, int size);
void perfport_io_write(void *pc, int offset, uint32_t val, int size);

/* pc_vga_step() (pc.c), on the drawing's thread: the picture is drawn
   through perfport_redraw(), which counts and hands on to the PC's own
   redraw(); then perfport_picture() with when the picture began and how
   long it took, us of get_uticks(). */
void perfport_redraw(void *pc, int x, int y, int w, int h);
void perfport_picture(uint32_t start_us, uint32_t us);

/* The front end's values (the board's speed.c, the PC harness): called on
   the emulator's thread during a snapshot, after the emulator's own, so
   that it may also replace them.  PERFPORT_PUT() sets a value and its
   valid bit. */
typedef void PerfPortFill(void *opaque, uint32_t *val, uint32_t *valid);
void perfport_set_fill(PerfPortFill *fn, void *opaque);
#define PERFPORT_PUT(val, valid, i, x) do { \
        (val)[i] = (uint32_t) (x); \
        (valid)[(i) >> 5] |= 1u << ((i) & 31); \
    } while (0)
/* text at PP_I_FIRMWARE or PP_I_BOARD (first: the index), at most 31
   characters (the 32nd byte is always 0) */
void perfport_put_text(uint32_t *val, uint32_t *valid, int first, const char *s);

/* everything back to its start (host tests) */
void perfport_reset(void);
#endif /* TINY386_PERFPORT */

#endif /* PERFPORT_H */
