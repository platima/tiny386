/*
 * ide.c with a command's blocks gathered (IDE_ENABLE_WRITE_GATHER, from
 * upstream's PR 70) and read ahead (IDE_ENABLE_READ_AHEAD), against ide.c
 * without them: what the guest sees at the IDE ports, compared.
 *
 * Side A is the reference and side B the one tested, each its own copy of
 * ide.c (ide_side.c), each with its own copy of two disks.  A pretend guest
 * drives both with the same port traffic: random sequences of commands on
 * two drives, by CHS and by LBA, counts of 1 to 256 (0 in the register),
 * READ and WRITE SECTORS, READ and WRITE MULTIPLE after SET MULTIPLE,
 * IDENTIFY and the commands that move no data, at sectors near and past
 * the end of a disk and on sectors that refuse to be read or written.  In
 * the middle of commands it reads and writes the registers, selects the
 * other drive, resets, sets nIEN, leaves a command before its end, and
 * moves data by words, double words and strings of any length.  Every
 * value read from a port and every change of the interrupt line must be
 * the same on both sides, and so must both disks after every command.
 * (v5.3) Between commands and in the middle of them the front end flushes
 * (ide_write_pending(), esp_main.c's disk_flush()): then both disks must be
 * the same at once, a write command left part way included.
 *
 * The one place the two may differ is a write command that fails while B
 * gathers (PR 70's own behaviour): there each side runs the command alone
 * and is checked against a model of what it does (write_failure() below),
 * and then the two are made the same again.
 *
 *   test_ide_gather SEED SEQUENCES STEPS
 *
 * Exit status 0 when every sequence agrees, 1 when one does not.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ide_side.h"

extern struct ide_side ide_side_a, ide_side_b;

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_SEEK 0x10
#define ST_READY 0x40
#define ST_BUSY 0x80
#define ABRT 0x04
#define HEADS 16                /* ide_hddrive_init()'s geometry */
#define SPT 63

/* ==== a little random ==== */

static uint64_t rng;
static uint32_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t) ((rng * 2685821657736338717ull) >> 32);
}

static int below(int n)
{
    return n > 0 ? (int) (rnd() % (unsigned) n) : 0;
}

static bool chance(int percent)
{
    return below(100) < percent;
}

/* ==== the disks ==== */

#define BAD_R 1
#define BAD_W 2
#define MAX_BAD 6

struct ide_model {
    uint8_t *img;
    uint8_t *bad;               /* BAD_R, BAD_W for each sector */
    int64_t nb;
    /* a read or write that meets a bad sector has done the ones before it,
       as a card's does chunk by chunk (disk_image.c, sd_part.c); 0: none */
    int partial;
    unsigned calls[2], sectors[2];
};

static int debug;               /* IDE_TEST_DEBUG */

int64_t ide_model_sectors(struct ide_model *m)
{
    return m->nb;
}

int ide_model_rw(struct ide_model *m, uint64_t sector, uint8_t *buf, int n, int write)
{
    /* nothing past the end: refused whole, as bf_read_async() and
       bf_write_async() refuse it on the board */
    if (n < 0 || sector > (uint64_t) m->nb || (uint64_t) n > (uint64_t) m->nb - sector)
        return -1;
    m->calls[write]++;
    m->sectors[write] += n;
    int good = n;
    for (int i = 0; i < n; i++)
        if (m->bad[sector + i] & (write ? BAD_W : BAD_R)) {
            good = i;
            break;
        }
    int done = good < n && !m->partial ? 0 : good;
    if (debug > 1)
        printf("    [model %p: %s %llu x %d: %s]\n", (void *) m, write ? "write" : "read",
               (unsigned long long) sector, n, good < n ? "fails" : "ok");
    if (write)
        memcpy(m->img + sector * 512, buf, (size_t) done * 512);
    else
        memcpy(buf, m->img + sector * 512, (size_t) done * 512);
    return good < n ? -1 : 0;
}

/* ==== the two sides ==== */

struct side {
    struct ide_side *ops;
    void *ifs;
    struct ide_model d[2];
    int irq[64];                /* the interrupt line's changes since the last look */
    int nirq;
    int raised;                 /* of them, how many set it */
};
static struct side side[2];
static int mask = 3;            /* the sides the guest talks to: 1 A, 2 B */
#define EACH(i) for (int i = 0; i < 2; i++) if (mask & (1 << i))

static void irq_cb(void *pic, int irq, int level)
{
    struct side *s = pic;
    if (s->nirq < 64)
        s->irq[s->nirq] = level;
    s->nirq++;
    if (level)
        s->raised++;
}

/* ==== the guest's view: every port access to the sides it talks to ==== */

static unsigned long long seed0;
static long seq_no, step_no;
static const char *doing = "";
static bool seq_bad;
static int fails;

static void mismatch(const char *fmt, ...)
{
    if (seq_bad)
        return;
    seq_bad = true;
    fails++;
    if (fails > 12)
        return;
    va_list ap;
    va_start(ap, fmt);
    printf("  FAIL seed %llu, sequence %ld, step %ld (%s): ", seed0, seq_no, step_no, doing);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static void irq_check(const char *what)
{
    if (mask != 3) {
        side[0].nirq = side[1].nirq = 0;
        return;
    }
    struct side *a = &side[0], *b = &side[1];
    int n = a->nirq < 64 ? a->nirq : 64;
    if (a->nirq != b->nirq || memcmp(a->irq, b->irq, sizeof(int) * n))
        mismatch("after %s, the interrupt line changed %d times on A, %d on B", what,
                 a->nirq, b->nirq);
    a->nirq = b->nirq = 0;
}

static const char *pname(int port)
{
    static const char *n[] = { "data", "error", "count", "sector", "cylinder low",
                               "cylinder high", "drive/head", "status", "alternate status" };
    return n[port];
}

/* 1 to 7 the task file (7 the status, which lowers the interrupt), 8 the
   alternate status */
static uint32_t in(int port)
{
    uint32_t v[2] = { 0, 0 };
    EACH(i) v[i] = port == 8 ? side[i].ops->status_read(side[i].ifs) :
                               side[i].ops->ioport_read(side[i].ifs, port);
    if (mask == 3 && v[0] != v[1])
        mismatch("%s read %02X on A, %02X on B", pname(port), v[0], v[1]);
    irq_check(pname(port));
    return v[mask & 1 ? 0 : 1];
}

static int cur_drv;             /* the drive the guest has selected, -1 none */
/* A write command the guest left before its end: B may still hold blocks
   of it, which it writes at the drive's next command, a reset, or a write
   to its registers, the guest's first chance to see them.  The disks are
   compared from then on. */
static bool pend[2];
static uint32_t devctl;

/* 1 to 7 the task file (7 the command), 8 the device control */
static void out(int port, uint32_t val)
{
    if (port == 6)
        cur_drv = (val >> 4) & 1;
    if (port >= 2 && port <= 7 && cur_drv >= 0)
        pend[cur_drv] = false;
    if (port == 8) {
        if ((devctl ^ val) & 0x04)
            pend[0] = pend[1] = false;
        devctl = val;
    }
    EACH(i) {
        if (port == 8)
            side[i].ops->cmd_write(side[i].ifs, val);
        else
            side[i].ops->ioport_write(side[i].ifs, port, val);
    }
    irq_check(port == 8 ? "device control" : pname(port));
}

static uint32_t inw(void)
{
    uint32_t v[2] = { 0, 0 };
    EACH(i) v[i] = side[i].ops->data_readw(side[i].ifs);
    if (mask == 3 && v[0] != v[1])
        mismatch("data word read %04X on A, %04X on B", v[0], v[1]);
    irq_check("data word read");
    return v[mask & 1 ? 0 : 1];
}

static uint32_t inl(void)
{
    uint32_t v[2] = { 0, 0 };
    EACH(i) v[i] = side[i].ops->data_readl(side[i].ifs);
    if (mask == 3 && v[0] != v[1])
        mismatch("data double word read %08X on A, %08X on B", v[0], v[1]);
    irq_check("data double word read");
    return v[mask & 1 ? 0 : 1];
}

static void outw(uint32_t v)
{
    EACH(i) side[i].ops->data_writew(side[i].ifs, v);
    irq_check("data word write");
}

static void outl(uint32_t v)
{
    EACH(i) side[i].ops->data_writel(side[i].ifs, v);
    irq_check("data double word write");
}

/* REP INSW/INSD and OUTSW/OUTSD: count units of size bytes, at most 4 KiB */
static int ins(int size, int count)
{
    static uint8_t b[2][4096];
    int r[2] = { 0, 0 };
    EACH(i) r[i] = side[i].ops->data_read_string(side[i].ifs, b[i], size, count);
    if (mask == 3 && r[0] != r[1])
        mismatch("string read of %d x %d: %d units on A, %d on B", count, size, r[0], r[1]);
    else if (mask == 3 && memcmp(b[0], b[1], (size_t) r[0] * size))
        mismatch("string read of %d x %d: other bytes on B", count, size);
    irq_check("string read");
    return r[mask & 1 ? 0 : 1];
}

static int outs(const uint8_t *buf, int size, int count)
{
    static uint8_t b[4096];
    int r[2] = { 0, 0 };
    EACH(i) {
        memcpy(b, buf, (size_t) size * count);
        r[i] = side[i].ops->data_write_string(side[i].ifs, b, size, count);
    }
    if (mask == 3 && r[0] != r[1])
        mismatch("string write of %d x %d: %d units on A, %d on B", count, size, r[0], r[1]);
    irq_check("string write");
    return r[mask & 1 ? 0 : 1];
}

/* bytes (even) of data in or out, by words, double words and strings
   mixed; a string may ask for more than is left, which ide.c clips at the
   block's end (and a string that moves nothing means no transfer is on) */
static void xfer(bool write, const uint8_t *data, int bytes)
{
    int done = 0;
    for (int guard = 0; done < bytes && guard < 4096; guard++) {
        int left = bytes - done;
        int how = below(10);
        if (how < 3) {
            if (write)
                outw(data[done] | data[done + 1] << 8);
            else
                inw();
            done += 2;
        } else if (how < 5 && left >= 4) {
            if (write)
                outl(data[done] | data[done + 1] << 8 | data[done + 2] << 16 |
                     (uint32_t) data[done + 3] << 24);
            else
                inl();
            done += 4;
        } else {
            int size = left >= 4 && chance(50) ? 4 : 2;
            int count = 1 + below(left / size + (chance(10) ? 3 : 0));
            if (count * size > 4096)
                count = 4096 / size;
            int r = write ? outs(data + done, size, count) : ins(size, count);
            done += (r > 0 ? r : count) * size;
        }
    }
}

/* ==== the registers, as ide.c reads them ==== */

struct regs {
    uint8_t nsector, sector, lcyl, hcyl, select;
};

static int64_t t_get_sector(const struct regs *r)
{
    if (r->select & 0x40)
        return ((r->select & 0x0f) << 24) | (r->hcyl << 16) | (r->lcyl << 8) | r->sector;
    return ((r->hcyl << 8) | r->lcyl) * HEADS * SPT + (r->select & 0x0f) * SPT +
           (r->sector - 1);
}

/* ide_set_sector() */
static void t_set_sector(struct regs *r, int64_t sector_num)
{
    unsigned int cyl, rr;
    if (r->select & 0x40) {
        r->select = (r->select & 0xf0) | ((sector_num >> 24) & 0x0f);
        r->hcyl = (sector_num >> 16) & 0xff;
        r->lcyl = (sector_num >> 8) & 0xff;
        r->sector = sector_num & 0xff;
    } else {
        cyl = sector_num / (HEADS * SPT);
        rr = sector_num % (HEADS * SPT);
        r->hcyl = (cyl >> 8) & 0xff;
        r->lcyl = cyl & 0xff;
        r->select = (r->select & 0xf0) | ((rr / SPT) & 0x0f);
        r->sector = (rr % SPT) + 1;
    }
}

static void read_regs(struct regs *r)
{
    r->nsector = in(2);
    r->sector = in(3);
    r->lcyl = in(4);
    r->hcyl = in(5);
    r->select = in(6);
}

/* ==== the guest ==== */

static struct {
    int nien;                   /* nIEN, as last written */
    int mult[2];                /* each drive's block for MULTIPLE, as the guest knows it */
    int64_t hot[2][8];          /* sectors each drive was near lately */
    int64_t bad[2][MAX_BAD];
    int nbad[2];
} g;

static struct {
    long cmds, reads, writes, mults, failed_reads, failed_writes, divergent, abandoned,
        pokes, resets, other, patterns, sectors, nested, flushes;
    /* the writes that failed with B gathering: the error at a later block
       on B, in a gather after the command's first, other registers after
       it, another disk after it */
    long told_later, later_gather, other_regs, other_disk;
} stats;
static long steps_per;
static bool nested;             /* in a command on the other drive, in the middle of one */
static unsigned long long totals[2][2];         /* side, read 0 / write 1: calls */

static void hot(int d, int64_t s)
{
    g.hot[d][below(8)] = s;
}

static void select_drive(int d, int low)
{
    out(6, 0xA0 | d << 4 | (low & 0x4f));
}

static bool range_fails(int d, int64_t s, int c, bool write)
{
    struct ide_model *m = &side[0].d[d];
    if (s < 0 || s + c > m->nb)
        return true;
    for (int64_t k = s; k < s + c; k++)
        if (m->bad[k] & (write ? BAD_W : BAD_R))
            return true;
    return false;
}

static int pick_count(void)
{
    switch (below(10)) {
    case 0: case 1: case 2: case 3: return 1 + below(8);
    case 4: case 5: case 6: return 9 + below(56);
    case 7: return 256;
    default: return 65 + below(192);
    }
}

static int64_t pick_start(int d, int count)
{
    struct ide_model *m = &side[0].d[d];
    int64_t s;
    switch (below(12)) {
    case 0: case 1: s = below((int) m->nb); break;
    case 2: case 3: s = m->nb - below(count + 40); break;         /* near the end */
    case 4: s = m->nb + below(40); break;                          /* past it */
    case 5:
        s = g.nbad[d] ? g.bad[d][below(g.nbad[d])] - below(count + 2) : below(64);
        break;
    case 6: case 7: case 8: case 9: s = g.hot[d][below(8)]; break;
    default: s = below(64); break;
    }
    return s < 0 ? 0 : s;
}

/* the registers for a command at sector s (by LBA, or by CHS, now and
   then not in the usual form), written as a driver writes them: the
   drive first */
static void put_regs(int d, bool lba, int64_t s, int count, struct regs *r)
{
    r->nsector = count & 0xff;
    if (lba) {
        r->select = 0xE0 | d << 4 | ((s >> 24) & 0x0f);
        r->sector = s & 0xff;
        r->lcyl = (s >> 8) & 0xff;
        r->hcyl = (s >> 16) & 0xff;
    } else {
        int64_t c = s / (HEADS * SPT), rem = s % (HEADS * SPT);
        r->select = 0xA0 | d << 4 | (int) (rem / SPT);
        r->sector = rem % SPT + 1;
        r->lcyl = c & 0xff;
        r->hcyl = (c >> 8) & 0xff;
        if (chance(5)) {
            switch (below(3)) {
            case 0: r->sector = 0; break;                       /* sector 0 */
            case 1: r->sector = 64 + below(192); break;         /* past the track */
            default: r->select = (r->select & 0xf0) | below(16); break;
            }
        }
    }
    out(6, r->select);
    if (chance(30))
        out(1, below(256));
    int order[4] = { 2, 3, 4, 5 };
    for (int i = 3; i > 0; i--) {
        int j = below(i + 1), t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (int i = 0; i < 4; i++) {
        int p = order[i];
        out(p, p == 2 ? r->nsector : p == 3 ? r->sector : p == 4 ? r->lcyl : r->hcyl);
    }
    if (chance(10))
        out(6, r->select);
}

static void check_disks(void)
{
    for (int d = 0; d < 2; d++)
        if (!pend[d] &&
            memcmp(side[0].d[d].img, side[1].d[d].img, (size_t) side[0].d[d].nb * 512)) {
            int64_t k = 0;
            while (!memcmp(side[0].d[d].img + k * 512, side[1].d[d].img + k * 512, 512))
                k++;
            mismatch("drive %d's disk differs, first at sector %lld", d, (long long) k);
        }
}

/* the registers as a driver looks at them after a command */
static void look(void)
{
    for (int p = 1; p <= 8; p++)
        in(p);
}

/* in the middle of a command: the registers changed.  For a read, any
   value; for a write, a place where the rest of it (up to 256 sectors)
   can be written, so that the reference does not fail there */
static void poke(int d, bool write)
{
    stats.pokes++;
    if (!write) {
        int p = 2 + below(5);
        out(p, p == 6 ? (0xA0 | d << 4 | (below(256) & 0x4f)) : below(256));
        return;
    }
    struct ide_model *m = &side[0].d[d];
    for (int tries = 0; tries < 8; tries++) {
        int64_t t = below((int) m->nb);
        if (range_fails(d, t, 256, true))
            continue;
        struct regs r;
        r.select = in(6);
        r.nsector = 0;
        bool lba = r.select & 0x40;
        if (lba) {
            r.select = (r.select & 0xf0) | ((t >> 24) & 0x0f);
            r.sector = t & 0xff;
            r.lcyl = (t >> 8) & 0xff;
            r.hcyl = (t >> 16) & 0xff;
        } else {
            r.select = (r.select & 0xf0) | (int) (t % (HEADS * SPT) / SPT);
            r.sector = t % SPT + 1;
            r.lcyl = (t / (HEADS * SPT)) & 0xff;
            r.hcyl = (t / (HEADS * SPT)) >> 8;
        }
        out(3, r.sector);
        out(4, r.lcyl);
        out(5, r.hcyl);
        out(6, r.select);
        return;
    }
}

/* v5.3: the front end's flush (esp_main.c's disk_flush()), between commands
   or in the middle of one: what B gathered is on its disks then, a write
   command left part way included, so both sides' disks are the same */
static void flush(void)
{
    stats.flushes++;
    for (int s = 0; s < 2; s++)
        if (side[s].ops->write_pending)
            side[s].ops->write_pending(side[s].ifs);
    pend[0] = pend[1] = false;
    check_disks();
}

static void reset(void)
{
    stats.resets++;
    out(8, 0x04 | (g.nien ? 0x02 : 0));
    if (chance(50))
        in(8);
    out(8, g.nien ? 0x02 : 0);
}

/* A write command that fails while B gathers.  Each side runs it alone,
   the guest writing each block while the status shows DRQ and stopping at
   ERR.  A must do what ide.c without gathering does: each block written
   when it is given, and the error at the first that fails, the registers
   where that block starts.  B must do what PR 70 documents: the blocks
   gathered and written together when the command ends or 128 sectors are
   gathered (WRITE_GATHER_SECTORS), the guest given DRQ and an interrupt
   for every block of a gather; if a gather's write fails, the error at its
   last block, with the registers at the gather's first sector (written by
   ide_set_sector()) and the count from there.  On the disk, each write is
   done as the disk does it (ide_model_rw()), so that a write refused whole
   leaves all of a gather unwritten.  Then B's disk and registers are made
   A's again. */
static void write_failure(int d, int op, const struct regs *r0, int64_t S, int C, int blk)
{
    static uint8_t data[257 * 512];
    struct ide_model *ma = &side[0].d[d], *mb = &side[1].d[d];
    size_t bytes = (size_t) ma->nb * 512;
    uint8_t *pre = malloc(bytes), *want = malloc(bytes);
    memcpy(pre, ma->img, bytes);
    for (int i = 0; i < C * 512; i++)
        data[i] = rnd();
    stats.divergent++;
    int fb[2] = { -1, -1 };
    int gs = 0;
    struct regs gr[2];

    for (int sd = 0; sd < 2; sd++) {
        struct side *si = &side[sd];
        struct ide_model *m = &si->d[d];
        mask = 1 << sd;
        si->raised = 0;
        out(7, op);
        int blocks = 0, o = 0;
        uint32_t st = 0;
        for (int guard = 0; guard < 300; guard++) {
            st = in(7);
            if ((st & ST_ERR) || !(st & ST_DRQ))
                break;
            int n = C - o < blk ? C - o : blk;
            xfer(true, data + (size_t) o * 512, n * 512);
            o += n;
            blocks++;
        }
        struct regs got;
        read_regs(&got);
        uint32_t err = in(1);
        int irqs = si->raised;

        /* the model of this side: after each block that does not fail, the
           registers at the next one (ide_sector_write_cb2()) */
        memcpy(want, pre, bytes);
        struct ide_model sim = *m;
        sim.img = want;
        struct regs exp = *r0;
        int fail_block = -1, gstart = 0, gcount = 0;
        o = 0;
        for (int k = 0; o < C; k++) {
            int n = C - o < blk ? C - o : blk;
            if (sd == 0 || !si->ops->gather) {
                if (ide_model_rw(&sim, (uint64_t) (S + o), data + (size_t) o * 512, n, 1)) {
                    fail_block = k;
                    break;
                }
            } else {
                if (!gcount)
                    gstart = o;
                gcount += n;
                if (!(o + n < C && gcount + blk <= si->ops->gather)) {
                    int fl = gcount;
                    gcount = 0;
                    if (ide_model_rw(&sim, (uint64_t) (S + gstart),
                                     data + (size_t) gstart * 512, fl, 1)) {
                        fail_block = k;
                        t_set_sector(&exp, S + gstart);
                        exp.nsector = (C - gstart) & 0xff;
                        break;
                    }
                }
            }
            o += n;
            t_set_sector(&exp, S + o);
            exp.nsector = (C - o) & 0xff;
        }
        int want_blocks = fail_block < 0 ? -1 : fail_block + 1;
        int want_irqs = g.nien ? 0 : want_blocks;
        const char *who = sd ? "B" : "A";
        if (debug) {
            int64_t f = -1;
            for (int64_t k = S; k < S + C && f < 0; k++)
                if (k < 0 || k >= m->nb || (m->bad[k] & BAD_W))
                    f = k;
            printf("  [write failure on %s: drive %d, sectors %lld to %lld, blocks of %d, disk "
                   "%lld, partial %d, first failing sector %lld; %d blocks]\n", who, d,
                   (long long) S, (long long) (S + C - 1), blk, (long long) m->nb, m->partial,
                   (long long) f, blocks);
        }
        if (fail_block < 0)
            mismatch("write failure: the model of %s has no failure (test fault)", who);
        else if (blocks != want_blocks || st != (ST_READY | ST_ERR) || err != ABRT)
            mismatch("write failure on %s: %d blocks written, status %02X error %02X; the "
                     "model says %d blocks, then status 41 error 04", who, blocks, st, err,
                     want_blocks);
        else if (irqs != want_irqs)
            mismatch("write failure on %s: %d interrupts, the model says %d", who, irqs,
                     want_irqs);
        else if (memcmp(&got, &exp, sizeof(got)))
            mismatch("write failure on %s: count %02X sector %02X cylinder %02X%02X drive/head "
                     "%02X; the model says %02X %02X %02X%02X %02X", who, got.nsector,
                     got.sector, got.hcyl, got.lcyl, got.select, exp.nsector, exp.sector,
                     exp.hcyl, exp.lcyl, exp.select);
        else if (memcmp(m->img, want, bytes))
            mismatch("write failure on %s: the disk is not as the model has it", who);
        fb[sd] = fail_block;
        gr[sd] = got;
        if (sd)
            gs = gstart;
    }
    /* how the two differed, for the summary */
    if (fb[1] > fb[0])
        stats.told_later++;
    if (gs > 0)
        stats.later_gather++;
    if (memcmp(&gr[0], &gr[1], sizeof(gr[0])))
        stats.other_regs++;
    if (memcmp(ma->img, mb->img, bytes))
        stats.other_disk++;
    mask = 3;
    side[0].nirq = side[1].nirq = 0;
    /* the same again: B's disk is A's, and the registers written afresh */
    memcpy(mb->img, ma->img, bytes);
    struct regs r;
    put_regs(d, true, below((int) ma->nb), 1 + below(8), &r);
    look();
    free(pre);
    free(want);
}

/* READ or WRITE SECTORS, or MULTIPLE: start, count and leave_at (sectors
   after which the guest leaves the command; -1 never) given or picked; the
   sectors moved */
static int rw(int d, bool write, bool multiple, int64_t start, int count, int leave_at)
{
    static uint8_t data[264 * 512];
    stats.cmds++;
    if (write)
        stats.writes++;
    else
        stats.reads++;
    if (multiple)
        stats.mults++;
    if (count <= 0)
        count = pick_count();
    if (start < 0)
        start = pick_start(d, count);
    bool lba = chance(60) || start >= 1024 * HEADS * SPT;
    struct regs r;
    put_regs(d, lba, start, count, &r);
    int64_t S = t_get_sector(&r);
    int blk = multiple ? g.mult[d] : 1;
    int op = multiple ? (write ? 0xC5 : 0xC4) : write ? (chance(50) ? 0x30 : 0x31)
                                                      : (chance(50) ? 0x20 : 0x21);
    bool fails = range_fails(d, S, count, write);
    if (fails)
        write ? stats.failed_writes++ : stats.failed_reads++;
    if (write && fails && blk > 0 && side[1].ops->gather) {
        write_failure(d, op, &r, S, count, blk);
        return 0;
    }
    if (write)
        for (size_t i = 0; i < sizeof(data); i++)
            data[i] = rnd();
    out(7, op);
    int done = 0;
    bool left = false, finished = false;
    for (int guard = 0; guard < 600 && !seq_bad; guard++) {
        uint32_t st = chance(15) ? in(8) : in(7);
        if (st & ST_ERR) {
            finished = true;
            break;
        }
        if (!(st & ST_DRQ)) {
            if (chance(5))
                xfer(write, data, 16);          /* past the end: nothing */
            finished = true;
            break;
        }
        if (leave_at >= 0 && done >= leave_at) {
            if (chance(40))
                xfer(write, data + (size_t) done * 512, 2 * below(256));
            left = true;
            break;
        }
        if (chance(3))
            poke(d, write);
        if (chance(2))
            flush();
        if (chance(2)) {
            /* a look at the other drive, and back */
            uint32_t sel = in(6);
            out(6, 0xA0 | (d ^ 1) << 4);
            in(7);
            in(2);
            out(6, sel);
        }
        if (chance(1) && !nested) {
            /* a read command on the other drive, and back: each drive has
               its own buffer */
            uint32_t sel = in(6);
            nested = true;
            stats.nested++;
            rw(d ^ 1, false, chance(30), -1, 0, chance(20) ? below(16) : -1);
            nested = false;
            out(6, sel);
        }
        if (chance(2) && !nested) {
            /* (not in a command on the other drive in the middle of one:
               a reset is both drives')
               After a reset ide.c goes on with the command if the guest
               does, at sector 0 or 1 with the count at 1 (which the
               block's size then takes past 0): a read may, a write would
               go where it may fail */
            reset();
            if (write || chance(70)) {
                left = true;
                break;
            }
        }
        int n = blk > 0 ? blk : 1;
        if (count - done < n && count - done > 0)
            n = count - done;
        if (chance(4))
            n = 1 + below(4);                   /* not the block's size */
        xfer(write, data + (size_t) (done % 256) * 512, n * 512);
        done += n;
    }
    if (left)
        stats.abandoned++;
    if (debug)
        printf("  [step %ld: drive %d command %02X at %lld, %d sectors, blocks of %d: %d moved%s%s]\n",
               step_no, d, op, (long long) S, count, blk, done, left ? ", left" : "",
               finished ? ", finished" : "");
    /* a write left before its end */
    if (write && !finished)
        pend[d] = true;
    stats.sectors += done;
    look();
    hot(d, S + done);
    hot(d, S + count);
    hot(d, S);
    return done;
}

/* read, leave it, write where it was left, read from there again: what was
   read ahead and not given must not be given after the write */
static void pattern(int d)
{
    stats.patterns++;
    struct ide_model *m = &side[0].d[d];
    int64_t s = below((int) m->nb);
    int c = 8 + below(120);
    int done = rw(d, false, chance(30), s, c, 1 + below(c - 1));
    int64_t t = s + done;
    if (t >= m->nb || range_fails(d, t, 4, true))
        return;
    rw(d, true, chance(30), t, 1 + below(4), -1);
    rw(d, false, chance(30), t, 1 + below(c), -1);
}

static void other_command(int d)
{
    stats.other++;
    static const int ops[] = { 0xEC, 0xEC, 0xF8, 0xE7, 0xEA, 0x10, 0x91, 0x00, 0x40, 0x24,
                               0x50, 0xFF, 0xC6, 0xC6, 0xC6, 0xC6 };
    int op = ops[below(sizeof(ops) / sizeof(ops[0]))];
    if (cur_drv != d)
        select_drive(d, below(256));
    if (op == 0xC6) {
        static const int counts[] = { 0, 1, 2, 4, 4, 3, 8, 16 };
        int c = counts[below(8)];
        out(2, c);
        out(7, op);
        uint32_t st = in(7);
        if (!(st & ST_ERR))
            g.mult[d] = c;
        if (debug)
            printf("  [step %ld: drive %d (selected %d) SET MULTIPLE %d: status %02X]\n",
                   step_no, d, cur_drv, c, st);
        look();
        return;
    }
    if (chance(50))
        out(2, below(256));
    out(7, op);
    uint32_t st = in(7);
    if (st & ST_DRQ)
        xfer(false, NULL, chance(90) ? 512 : below(600));
    look();
}

static void step(void)
{
    int d = chance(75) ? 0 : 1;
    int k = below(100);
    if (k < 30) {
        doing = "READ";
        rw(d, false, false, -1, 0, chance(12) ? below(64) : -1);
    } else if (k < 55) {
        doing = "WRITE";
        rw(d, true, false, -1, 0, chance(12) ? below(64) : -1);
    } else if (k < 65) {
        doing = "READ MULTIPLE";
        rw(d, false, true, -1, 0, chance(12) ? below(64) : -1);
    } else if (k < 75) {
        doing = "WRITE MULTIPLE";
        rw(d, true, true, -1, 0, chance(12) ? below(64) : -1);
    } else if (k < 82) {
        doing = "read, write, read";
        pattern(d);
    } else if (k < 90) {
        doing = "another command";
        other_command(d);
    } else if (k < 93) {
        doing = "reset";
        reset();
        look();
    } else if (k < 95) {
        doing = "nIEN";
        g.nien = chance(30);
        out(8, g.nien ? 0x02 : 0);
    } else if (k < 97) {
        doing = "drive select";
        select_drive(d, below(256));
        look();
    } else if (k < 98) {
        doing = "the front end's flush";
        flush();
    } else {
        /* (no data: that could end the block of a write command the guest
           left, at a place where the reference's write fails and the
           gathering's does not yet, which write_failure() does not model) */
        doing = "registers";
        if (chance(50))
            out(1 + below(6), below(256));
        else
            in(1 + below(8));
    }
}

static void make_disk(struct ide_model *m, int64_t nb, bool partial, int d)
{
    m->nb = nb;
    m->img = malloc((size_t) nb * 512);
    m->bad = calloc((size_t) nb, 1);
    m->partial = partial;
    m->calls[0] = m->calls[1] = m->sectors[0] = m->sectors[1] = 0;
    (void) d;
}

static void sequence(void)
{
    int64_t nb[2] = { 700 + below(4000), 100 + below(1500) };
    bool partial = chance(50);
    memset(&g, 0, sizeof(g));
    cur_drv = -1;           /* ide_allocate(): none until the guest selects one */
    for (int s = 0; s < 2; s++) {
        side[s].ops = s ? &ide_side_b : &ide_side_a;
        for (int d = 0; d < 2; d++)
            make_disk(&side[s].d[d], nb[d], partial, d);
    }
    for (int d = 0; d < 2; d++) {
        for (int64_t k = 0; k < nb[d] * 512; k++)
            side[0].d[d].img[k] = rnd();
        memcpy(side[1].d[d].img, side[0].d[d].img, (size_t) nb[d] * 512);
        g.nbad[d] = below(MAX_BAD + 1);
        for (int i = 0; i < g.nbad[d]; i++) {
            int64_t b = below((int) nb[d]);
            int how = 1 + below(3);
            side[0].d[d].bad[b] = side[1].d[d].bad[b] = how;
            g.bad[d][i] = b;
        }
        g.mult[d] = 4;          /* ide_hddrive_init(): MAX_MULT_SECTORS */
        for (int i = 0; i < 8; i++)
            g.hot[d][i] = below((int) nb[d]);
    }
    for (int s = 0; s < 2; s++) {
        side[s].nirq = side[s].raised = 0;
        side[s].ifs = side[s].ops->create(&side[s].d[0], &side[s].d[1], &side[s], irq_cb);
    }
    seq_bad = false;
    mask = 3;
    if (debug)
        printf("  [sequence %ld: disks of %lld and %lld sectors]\n", seq_no, (long long) nb[0],
               (long long) nb[1]);
    pend[0] = pend[1] = false;
    devctl = 0;
    look();
    for (step_no = 0; step_no < steps_per && !seq_bad; step_no++) {
        step();
        if (!seq_bad)
            check_disks();
    }
    /* the end: a reset, and the disks once more */
    if (!seq_bad) {
        doing = "the reset at the end";
        reset();
        look();
        check_disks();
    }
    for (int s = 0; s < 2; s++) {
        side[s].ops->destroy(side[s].ifs);
        for (int d = 0; d < 2; d++) {
            totals[s][0] += side[s].d[d].calls[0];
            totals[s][1] += side[s].d[d].calls[1];
            free(side[s].d[d].img);
            free(side[s].d[d].bad);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: test_ide_gather SEED SEQUENCES STEPS\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("IDE_TEST_DEBUG"))
        debug = atoi(getenv("IDE_TEST_DEBUG"));
    seed0 = strtoull(argv[1], NULL, 0);
    long seqs = atol(argv[2]);
    steps_per = atol(argv[3]);
    printf("A: %s\nB: %s (gathers %d sectors, reads ahead %d)\n", ide_side_a.what,
           ide_side_b.what, ide_side_b.gather, ide_side_b.read_ahead);
    int bad_seqs = 0;
    for (seq_no = 0; seq_no < seqs; seq_no++) {
        rng = seed0 * 0x9E3779B97F4A7C15ull + (uint64_t) seq_no * 0xBF58476D1CE4E5B9ull + 1;
        for (int i = 0; i < 8; i++)
            rnd();
        sequence();
        if (seq_bad)
            bad_seqs++;
    }
    printf("%ld sequences of %ld steps: %ld commands moving data (%ld reads, %ld writes, %ld of "
           "them MULTIPLE), %ld sectors moved; %ld other commands; %ld read, write, read; %ld "
           "reads and %ld writes on a sector that fails or past the end, %ld of the writes run "
           "on each side alone; %ld commands left before their end, %ld register changes in "
           "the middle of one, %ld reads of the other drive in the middle of one, %ld resets, "
           "%ld flushes\n",
           seqs, steps_per, stats.cmds, stats.reads, stats.writes, stats.mults, stats.sectors,
           stats.other, stats.patterns, stats.failed_reads, stats.failed_writes,
           stats.divergent, stats.abandoned, stats.pokes, stats.nested, stats.resets,
           stats.flushes);
    if (stats.divergent)
        printf("of the %ld writes that failed with B gathering, as PR 70 has it: on B the "
               "error at a later block %ld times, in a gather after the command's first %ld, "
               "other registers after it %ld, another disk after it %ld (each as its model "
               "said)\n", stats.divergent, stats.told_later, stats.later_gather,
               stats.other_regs, stats.other_disk);
    printf("block device calls: A %llu reads, %llu writes; B %llu reads, %llu writes\n",
           totals[0][0], totals[0][1], totals[1][0], totals[1][1]);
    printf("test_ide_gather: %d of %ld sequences differ\n", bad_seqs, seqs);
    return bad_seqs ? 1 : 0;
}
