/*
 * The information port (perfport.c, perfport.h) as a guest program sees
 * it: in and out at 0F1F8h to 0F1FFh of each size, as pc.c hands them on,
 * against stand-ins for the emulator's counters.
 *
 * perfport.c is included here, so that its 32-bit instruction count can be
 * tried on a host whose long is 64 bits.  argv[1], if given, is how many
 * PP_I_ lines perfport.h has (run.sh counts them), held against the list
 * below so that none is left out of the checks.
 */
#define TINY386_PERFPORT
#include "../../perfport.c"

#include <pthread.h>
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

/* ==== the emulator's side, pretended ==== */
static volatile uint32_t fake_now;
uint32_t get_uticks() { return fake_now; }
static long fake_cycle;
long cpui386_get_cycle(CPUI386 *cpu) { (void) cpu; return fake_cycle; }
static struct vga_info fake_vi;
void vga_get_info(VGAState *s, struct vga_info *info) { (void) s; *info = fake_vi; }
int vga_dirty_blocks = 1;
int i8254_max_owed = 1;
uint32_t i8254_owed_max, i8254_dropped;
volatile unsigned ps2_key_events, ps2_mouse_reports, ps2_mouse_packets;

static PC pc;
static char guest_mem[0x1000];

static int redraws;
static void *redraw_opaque;
static int redraw_args[4];
static void fake_redraw(void *opaque, int x, int y, int w, int h)
{
    redraws++;
    redraw_opaque = opaque;
    redraw_args[0] = x; redraw_args[1] = y; redraw_args[2] = w; redraw_args[3] = h;
}

/* ==== the guest's in and out, as pc.c hands them on ==== */
static void outb(unsigned port, uint32_t v) { perfport_io_write(&pc, port - PP_PORT_BASE, v & 0xff, 1); }
static void outw(unsigned port, uint32_t v) { perfport_io_write(&pc, port - PP_PORT_BASE, v & 0xffff, 2); }
static void outd(unsigned port, uint32_t v) { perfport_io_write(&pc, port - PP_PORT_BASE, v, 4); }
static uint32_t inb(unsigned port) { return perfport_io_read(&pc, port - PP_PORT_BASE, 1) & 0xff; }
static uint32_t inw(unsigned port) { return perfport_io_read(&pc, port - PP_PORT_BASE, 2) & 0xffff; }
static uint32_t ind(unsigned port) { return perfport_io_read(&pc, port - PP_PORT_BASE, 4); }

static uint32_t rd(unsigned i)
{
    outw(PP_PORT_INDEX, i);
    return ind(PP_PORT_DATA);
}
static void snap(void) { outw(PP_PORT_CMD, PP_CMD_SNAPSHOT); }
static int valid(unsigned i) { return (rd(PP_I_VALID0 + i / 32) >> (i % 32)) & 1; }

static void setup(void)
{
    perfport_reset();
    memset(&pc, 0, sizeof(pc));
    memset(guest_mem, 0, sizeof(guest_mem));
    pc.phys_mem = guest_mem;
    pc.phys_mem_size = 14L << 20;
    pc.vga_mem_size = 768 << 10;
    pc.redraw = fake_redraw;
    pc.redraw_data = &redraws;
    fake_now = 1000;
    fake_cycle = 0;
    memset(&fake_vi, 0, sizeof(fake_vi));
    i8254_owed_max = i8254_dropped = 0;
    i8254_max_owed = 1;
    ps2_key_events = ps2_mouse_reports = ps2_mouse_packets = 0;
}

/* every index perfport.h names, and how many indexes it takes */
static const struct { const char *name; unsigned i, n; } names[] = {
#define N(x) { #x, x, 1 }
    N(PP_I_MAGIC), N(PP_I_VERSION), N(PP_I_COUNT), N(PP_I_SNAPSHOTS),
    N(PP_I_VALID0), N(PP_I_VALID1), N(PP_I_VALID2), N(PP_I_VALID3),
    N(PP_I_SOURCE), N(PP_I_CLOCK_US), N(PP_I_UPTIME_MS), N(PP_I_PC_UPTIME_MS),
    N(PP_I_SNAP_US), N(PP_I_PORT_IO),
    N(PP_I_INSNS_LO), N(PP_I_INSNS_HI), N(PP_I_KIPS), N(PP_I_HALT_PCT),
    N(PP_I_RUN_MS), N(PP_I_HALT_MS), N(PP_I_SECONDS),
    N(PP_I_PICTURES), N(PP_I_PICTURES_1S), N(PP_I_DRAW_US), N(PP_I_DRAW_AVG_US_1S),
    N(PP_I_DRAW_MAX_US_1S), N(PP_I_DRAW_MAX_US), N(PP_I_FLUSHES), N(PP_I_FLUSH_ROWS),
    N(PP_I_VBE_PICTURES), N(PP_I_VBE_WHOLE), N(PP_I_VBE_BLOCKS), N(PP_I_VBE_ROWS),
    N(PP_I_VBE_BLOCKS_ON), N(PP_I_FRAME_MS),
    N(PP_I_MODE_KIND), N(PP_I_MODE_WIDTH), N(PP_I_MODE_HEIGHT), N(PP_I_MODE_BPP),
    N(PP_I_MODE_LINE_BYTES), N(PP_I_MODE_START), N(PP_I_BIOS_MODE),
    N(PP_I_SCREEN_WIDTH), N(PP_I_SCREEN_HEIGHT),
    N(PP_I_GUEST_MEM_KB), N(PP_I_VGA_MEM_KB), N(PP_I_PSRAM_FREE_KB),
    N(PP_I_PSRAM_LARGEST_KB), N(PP_I_IRAM_FREE_KB), N(PP_I_IRAM_LARGEST_KB),
    N(PP_I_IRAM_MIN_FREE_KB),
    N(PP_I_DISK_READS), N(PP_I_DISK_WRITES), N(PP_I_DISK_READ_SECTORS),
    N(PP_I_DISK_WRITE_SECTORS), N(PP_I_DISK_BUSY_US), N(PP_I_DISK_LONGEST_US),
    N(PP_I_DISK_OVER_TICK), N(PP_I_DISK_ERRORS), N(PP_I_DISK_RECOVERED),
    N(PP_I_DISK_REFUSED), N(PP_I_DISK_FLUSHES), N(PP_I_DISK_ACTIVITY),
    N(PP_I_TIMER_OWED_MAX), N(PP_I_TIMER_DROPPED), N(PP_I_TIMER_CATCHUP),
    N(PP_I_KEY_EVENTS), N(PP_I_MOUSE_REPORTS), N(PP_I_MOUSE_PACKETS),
    N(PP_I_LOG_HELD), N(PP_I_LOG_DROPPED), N(PP_I_STATUS), N(PP_I_CPU_MHZ),
#undef N
    { "PP_I_FIRMWARE", PP_I_FIRMWARE, PP_TEXT_INDEXES },
    { "PP_I_BOARD", PP_I_BOARD, PP_TEXT_INDEXES },
};
#define NNAMES (int) (sizeof(names) / sizeof(names[0]))

static void test_table(int lines)
{
    static char used[PP_COUNT];
    memset(used, 0, sizeof(used));
    for (int k = 0; k < NNAMES; k++) {
        for (unsigned j = 0; j < names[k].n; j++) {
            unsigned i = names[k].i + j;
            CHECK(i < PP_COUNT, "%s + %u = %#x is not below PP_COUNT", names[k].name, j, i);
            if (i < PP_COUNT) {
                CHECK(!used[i], "%s + %u = %#x is used twice", names[k].name, j, i);
                used[i] = 1;
            }
        }
    }
    if (lines >= 0)
        CHECK(lines == NNAMES, "perfport.h has %d PP_I_ lines, this test knows %d", lines, NNAMES);
    CHECK(PP_PORT_INDEX == PP_PORT_BASE && PP_PORT_CMD == PP_PORT_BASE + 2 &&
          PP_PORT_DATA == PP_PORT_BASE + 4 && PP_PORT_COUNT == 8, "port layout");
    CHECK(PP_VERSION == ((unsigned long) PP_VERSION_MAJOR << 16 | PP_VERSION_MINOR), "version");
    CHECK(memcmp(&(uint32_t) { PP_MAGIC }, "T386", 4) == 0, "PP_MAGIC is \"T386\" in memory");
}

/* found, before any snapshot, with every width of read */
static void test_detect(void)
{
    setup();
    fake_cycle = 12345;
    CHECK(rd(PP_I_MAGIC) == PP_MAGIC, "magic %#x", rd(PP_I_MAGIC));
    CHECK(rd(PP_I_VERSION) == PP_VERSION, "version");
    CHECK(rd(PP_I_COUNT) == PP_COUNT, "count");
    CHECK(rd(PP_I_SNAPSHOTS) == 0, "no snapshot yet");
    CHECK(rd(PP_I_VALID0) == 0 && rd(PP_I_INSNS_LO) == 0 && rd(PP_I_SOURCE) == 0,
          "before the first snapshot every other value is 0");
    outw(PP_PORT_INDEX, PP_I_MAGIC);
    CHECK((inw(PP_PORT_DATA) | inw(PP_PORT_DATA + 2) << 16) == PP_MAGIC, "magic in halves");
    uint32_t m = 0;
    for (int k = 0; k < 4; k++)
        m |= inb(PP_PORT_DATA + k) << (8 * k);
    CHECK(m == PP_MAGIC, "magic in bytes");
    /* INDEX reads back */
    outw(PP_PORT_INDEX, 0x1234);
    CHECK(inw(PP_PORT_INDEX) == 0x1234 && inb(PP_PORT_INDEX) == 0x34 &&
          inb(PP_PORT_INDEX + 1) == 0x12, "index read back");
    outb(PP_PORT_INDEX, 0x56);
    CHECK(inw(PP_PORT_INDEX) == 0x56, "an 8-bit write sets the whole index: %#x",
          inw(PP_PORT_INDEX));
    CHECK(rd(0xffff) == 0 && rd(PP_COUNT) == 0, "an index past the table reads 0");
    /* a 32-bit read at 0F1FEh: DATA's high half, then 0FFh past the end */
    outw(PP_PORT_INDEX, PP_I_MAGIC);
    CHECK(ind(PP_PORT_DATA + 2) == ((PP_MAGIC >> 16) | 0xffff0000u),
          "32 bits at 0F1FEh: %#x", ind(PP_PORT_DATA + 2));
    /* 32 bits at 0F1F8h: INDEX and the snapshots */
    CHECK(ind(PP_PORT_INDEX) == PP_I_MAGIC, "INDEX and snapshots in one read");
    CHECK(pp_snaps == 0, "reads take no snapshot");
}

static void put_all(void)
{
    fake_now = 5000000;
    fake_cycle = 0x123456789L;
    guest_mem[0x449] = 0x12;
    fake_vi = (struct vga_info) { .kind = 3, .width = 800, .height = 480, .bpp = 16,
        .line_bytes = 1600, .start = 3200, .screen_w = 800, .screen_h = 480,
        .vbe_pictures = 11, .vbe_whole = 2, .vbe_blocks = 333, .vbe_rows = 444 };
    vga_dirty_blocks = 1;
    i8254_owed_max = 3;
    i8254_dropped = 77;
    i8254_max_owed = 1;
    ps2_key_events = 5;
    ps2_mouse_reports = 6;
    ps2_mouse_packets = 7;
}

/* a snapshot: the emulator's own values, and only those valid */
static void test_snapshot(void)
{
    setup();
    put_all();
    snap();
    CHECK(rd(PP_I_SNAPSHOTS) == 1 && inw(PP_PORT_CMD) == 1, "one snapshot");
    CHECK(rd(PP_I_CLOCK_US) == 5000000, "clock");
    if (sizeof(long) >= 8)
        CHECK(rd(PP_I_INSNS_LO) == 0x23456789 && rd(PP_I_INSNS_HI) == 1,
              "instructions %#x %#x", rd(PP_I_INSNS_LO), rd(PP_I_INSNS_HI));
    CHECK(rd(PP_I_MODE_KIND) == 3 && rd(PP_I_MODE_WIDTH) == 800 && rd(PP_I_MODE_HEIGHT) == 480 &&
          rd(PP_I_MODE_BPP) == 16 && rd(PP_I_MODE_LINE_BYTES) == 1600 &&
          rd(PP_I_MODE_START) == 3200, "mode");
    CHECK(rd(PP_I_SCREEN_WIDTH) == 800 && rd(PP_I_SCREEN_HEIGHT) == 480, "screen");
    CHECK(rd(PP_I_VBE_PICTURES) == 11 && rd(PP_I_VBE_WHOLE) == 2 && rd(PP_I_VBE_BLOCKS) == 333 &&
          rd(PP_I_VBE_ROWS) == 444 && rd(PP_I_VBE_BLOCKS_ON) == 1, "VBE counts");
    CHECK(rd(PP_I_BIOS_MODE) == 0x12, "BIOS mode");
    CHECK(rd(PP_I_GUEST_MEM_KB) == 14336 && rd(PP_I_VGA_MEM_KB) == 768, "memory sizes");
    CHECK(rd(PP_I_TIMER_OWED_MAX) == 3 && rd(PP_I_TIMER_DROPPED) == 77 &&
          rd(PP_I_TIMER_CATCHUP) == 1, "timer");
    CHECK(rd(PP_I_KEY_EVENTS) == 5 && rd(PP_I_MOUSE_REPORTS) == 6 &&
          rd(PP_I_MOUSE_PACKETS) == 7, "keyboard and mouse");
    /* valid: the emulator's own, and the identity; not the front end's */
    unsigned mine[] = { PP_I_MAGIC, PP_I_VERSION, PP_I_COUNT, PP_I_SNAPSHOTS, PP_I_VALID0,
        PP_I_VALID3, PP_I_CLOCK_US, PP_I_SNAP_US, PP_I_PORT_IO, PP_I_INSNS_LO, PP_I_INSNS_HI,
        PP_I_PICTURES, PP_I_PICTURES_1S, PP_I_DRAW_US, PP_I_DRAW_MAX_US, PP_I_FLUSHES,
        PP_I_MODE_KIND, PP_I_BIOS_MODE, PP_I_GUEST_MEM_KB, PP_I_TIMER_DROPPED,
        PP_I_MOUSE_PACKETS, PP_I_VBE_BLOCKS_ON };
    for (unsigned k = 0; k < sizeof(mine) / sizeof(mine[0]); k++)
        CHECK(valid(mine[k]), "index %#x should be valid", mine[k]);
    unsigned theirs[] = { PP_I_SOURCE, PP_I_UPTIME_MS, PP_I_KIPS, PP_I_HALT_PCT,
        PP_I_PSRAM_FREE_KB, PP_I_DISK_READS, PP_I_STATUS, PP_I_FIRMWARE, PP_I_BOARD + 7,
        PP_I_FRAME_MS, PP_I_LOG_HELD };
    for (unsigned k = 0; k < sizeof(theirs) / sizeof(theirs[0]); k++)
        CHECK(!valid(theirs[k]) && rd(theirs[k]) == 0,
              "index %#x has no value without a front end", theirs[k]);
    CHECK(!valid(0x6f) && !valid(0x17) && !valid(0x0e), "nothing valid at an unused index");

    /* the snapshot holds while the counters move; the next one has them */
    fake_now = 6000000;
    ps2_key_events = 50;
    i8254_dropped = 78;
    fake_vi.width = 640;
    CHECK(rd(PP_I_KEY_EVENTS) == 5 && rd(PP_I_TIMER_DROPPED) == 77 &&
          rd(PP_I_MODE_WIDTH) == 800 && rd(PP_I_CLOCK_US) == 5000000, "the snapshot holds");
    snap();
    CHECK(rd(PP_I_KEY_EVENTS) == 50 && rd(PP_I_TIMER_DROPPED) == 78 &&
          rd(PP_I_MODE_WIDTH) == 640 && rd(PP_I_CLOCK_US) == 6000000, "the next snapshot");

    /* 32 bits at 0F1F8h: the index, then the command in the high half */
    outd(PP_PORT_INDEX, (uint32_t) PP_CMD_SNAPSHOT << 16 | PP_I_SNAPSHOTS);
    CHECK(inw(PP_PORT_INDEX) == PP_I_SNAPSHOTS && ind(PP_PORT_DATA) == 3,
          "index and snapshot in one 32-bit out");
    outd(PP_PORT_INDEX, PP_I_CLOCK_US);         /* command 0 */
    outb(PP_PORT_CMD, PP_CMD_NONE);
    outw(PP_PORT_CMD, 2);
    outw(PP_PORT_CMD, 0xffff);
    outd(PP_PORT_DATA, 0xdeadbeef);
    outw(PP_PORT_DATA + 2, 0xbeef);
    CHECK(rd(PP_I_SNAPSHOTS) == 3 && inw(PP_PORT_INDEX) == PP_I_SNAPSHOTS,
          "commands 0, 2 and 0FFFFh and writes to DATA do nothing");
    outb(PP_PORT_CMD, PP_CMD_SNAPSHOT);
    CHECK(rd(PP_I_SNAPSHOTS) == 4, "an 8-bit command");
    outw(PP_PORT_INDEX + 1, 0x0100);            /* at 0F1F9h: nothing */
    CHECK(rd(PP_I_SNAPSHOTS) == 4, "a write at 0F1F9h does nothing");

    /* the port's own reads and writes, as far as the snapshot */
    uint32_t before = pp_io;
    snap();
    CHECK(rd(PP_I_PORT_IO) == before + 1, "port I/O %u, %u before", rd(PP_I_PORT_IO), before);
}

/* the front end's part */
static int fill_calls;
static void fill(void *opaque, uint32_t *v, uint32_t *ok)
{
    CHECK(opaque == &fill_calls, "the fill's opaque");
    fill_calls++;
    PERFPORT_PUT(v, ok, PP_I_SOURCE, PP_SOURCE_BOARD);
    PERFPORT_PUT(v, ok, PP_I_PSRAM_FREE_KB, 1234);
    PERFPORT_PUT(v, ok, PP_I_INSNS_LO, 42);     /* over the emulator's */
    PERFPORT_PUT(v, ok, PP_I_MAGIC, 0);         /* not allowed: put back */
    PERFPORT_PUT(v, ok, PP_I_VALID0, 0);
    perfport_put_text(v, ok, PP_I_FIRMWARE, "v4.1");
    perfport_put_text(v, ok, PP_I_BOARD, "a name of more than thirty-one characters");
    fake_now += 7;                              /* the fill takes 7 us */
}

static void test_fill(void)
{
    setup();
    put_all();
    fill_calls = 0;
    perfport_set_fill(fill, &fill_calls);
    snap();
    CHECK(fill_calls == 1, "filled once");
    CHECK(rd(PP_I_SOURCE) == PP_SOURCE_BOARD && valid(PP_I_SOURCE), "source");
    CHECK(rd(PP_I_PSRAM_FREE_KB) == 1234 && valid(PP_I_PSRAM_FREE_KB), "a front end value");
    CHECK(rd(PP_I_INSNS_LO) == 42, "the front end's value over the emulator's");
    CHECK(rd(PP_I_MAGIC) == PP_MAGIC, "the magic stays");
    CHECK(valid(PP_I_SOURCE) && valid(PP_I_VALID0) && valid(PP_I_MAGIC), "valid bits kept");
    CHECK(rd(PP_I_SNAP_US) == 7, "the snapshot's own time: %u us", rd(PP_I_SNAP_US));
    CHECK(rd(PP_I_FIRMWARE) == 0x312e3476 && rd(PP_I_FIRMWARE + 1) == 0 &&
          valid(PP_I_FIRMWARE + 7), "text: %#x", rd(PP_I_FIRMWARE));
    char b[33];
    for (int k = 0; k < 8; k++) {
        uint32_t w = rd(PP_I_BOARD + k);
        memcpy(b + 4 * k, &w, 4);
    }
    b[32] = 'X';
    CHECK(b[31] == 0 && !memcmp(b, "a name of more than thirty-one c", 31),
          "a long text cut at 31 characters, 0 after: %.31s", b);
    perfport_set_fill(NULL, NULL);
    snap();
    CHECK(fill_calls == 1 && rd(PP_I_SOURCE) == 0 && !valid(PP_I_SOURCE), "no fill, no value");
}

/* the drawing: totals, the last whole second, a stale second */
static void test_drawing(void)
{
    setup();
    perfport_redraw(&pc, 1, 2, 3, 4);
    CHECK(redraws == 1 && redraw_opaque == &redraws && redraw_args[0] == 1 &&
          redraw_args[1] == 2 && redraw_args[2] == 3 && redraw_args[3] == 4,
          "perfport_redraw() hands on to the PC's redraw()");
    perfport_redraw(&pc, 0, 100, 800, 16);
    /* a picture every 30 ms for 1.2 s: 2000 us each, one of 9000 us */
    uint32_t t = 10000000;
    int n = 0;
    for (; t < 10000000 + 1200000; t += 30000, n++)
        perfport_picture(t, n == 5 ? 9000 : 2000);
    fake_now = t;
    snap();
    /* the window closes at the first picture that ends 1 s or more after
       the first began: the 35th, which began at 1020 ms */
    CHECK(rd(PP_I_PICTURES) == (uint32_t) n, "pictures %u of %d", rd(PP_I_PICTURES), n);
    CHECK(rd(PP_I_PICTURES_1S) == 35, "pictures in the last second: %u", rd(PP_I_PICTURES_1S));
    CHECK(rd(PP_I_DRAW_US) == (uint32_t) (n - 1) * 2000 + 9000, "draw us %u", rd(PP_I_DRAW_US));
    CHECK(rd(PP_I_DRAW_MAX_US) == 9000 && rd(PP_I_DRAW_MAX_US_1S) == 9000, "longest");
    CHECK(rd(PP_I_DRAW_AVG_US_1S) == (34 * 2000 + 9000) / 35, "average %u",
          rd(PP_I_DRAW_AVG_US_1S));
    CHECK(rd(PP_I_FLUSHES) == 2 && rd(PP_I_FLUSH_ROWS) == 20, "flushes");
    /* the next second without the long one */
    for (; t < 10000000 + 2400000; t += 30000, n++)
        perfport_picture(t, 2000);
    fake_now = t;
    snap();
    CHECK(rd(PP_I_DRAW_MAX_US_1S) == 2000 && rd(PP_I_DRAW_MAX_US) == 9000 &&
          rd(PP_I_DRAW_AVG_US_1S) == 2000, "the last second without the long picture");
    /* the drawing's clock a little ahead of the snapshot's: still fresh */
    fake_now = pp_draw.win_end - 5;
    snap();
    CHECK(rd(PP_I_PICTURES_1S) > 0, "fresh when the drawing is just ahead");
    /* nothing drawn for 2 s (the console has the screen): no last second */
    fake_now = pp_draw.win_end + 2000000;
    snap();
    CHECK(rd(PP_I_PICTURES_1S) == 0 && rd(PP_I_DRAW_AVG_US_1S) == 0 &&
          rd(PP_I_DRAW_MAX_US_1S) == 0 && rd(PP_I_PICTURES) == (uint32_t) n,
          "a stale second reads 0, the totals stay");
    /* the clock's wrap half way through the second */
    setup();
    for (t = 0xfff80000u, n = 0; n < 40; t += 30000, n++)
        perfport_picture(t, 1000);
    fake_now = t;
    snap();
    CHECK(rd(PP_I_PICTURES_1S) == 35, "across the clock's wrap: %u", rd(PP_I_PICTURES_1S));
}

/* the 32-bit count of the board, made 64 bits */
static void test_extend(void)
{
    setup();
    CHECK(extend_insns(100) == 100, "first");
    CHECK(extend_insns(0xfffffff0u) == 0xfffffff0u, "near the top");
    CHECK(extend_insns(0x10) == 0x100000010ull, "across the wrap");
    CHECK(extend_insns(0x20) == 0x100000020ull, "on");
}

/* the drawing on one thread, snapshots on another: the totals never go
   back and end at what was drawn */
static volatile int stop;
static void *drawer(void *arg)
{
    (void) arg;
    uint32_t t = 0;
    for (int k = 0; k < 200000; k++, t += 1000) {
        perfport_picture(t, 10);
        perfport_redraw(&pc, 0, 0, 800, 1);
    }
    stop = 1;
    return NULL;
}

static void test_threads(void)
{
    setup();
    pthread_t th;
    pthread_create(&th, NULL, drawer, NULL);
    uint32_t last = 0, lastf = 0;
    int back = 0, snaps = 0;
    while (!stop) {
        snap();
        snaps++;
        uint32_t p = rd(PP_I_PICTURES), f = rd(PP_I_FLUSHES);
        if (p < last || f < lastf)
            back++;
        last = p;
        lastf = f;
    }
    pthread_join(th, NULL);
    snap();
    CHECK(back == 0, "totals went back %d times in %d snapshots", back, snaps);
    CHECK(rd(PP_I_PICTURES) == 200000 && rd(PP_I_FLUSHES) == 200000 &&
          rd(PP_I_DRAW_US) == 2000000, "totals at the end");
}

int main(int argc, char **argv)
{
    test_table(argc > 1 ? atoi(argv[1]) : -1);
    test_detect();
    test_snapshot();
    test_fill();
    test_drawing();
    test_extend();
    test_threads();
    printf("perfport: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
