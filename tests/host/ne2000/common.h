/*
 * A guest's NE2000 driver, through the card's ports, for ring.c, route.c and
 * hang.c.  tiny386 puts the card at 0x300 (pc.c): the 8390's registers at
 * 0x300 to 0x30f, the data port at 0x310 and the reset port at 0x31f.  The
 * set-up follows Linux's 8390 driver (NS8390_init): PSTART, PSTOP, BNRY at
 * PSTOP less one and CURR at PSTART, word transfers; frames are read with
 * remote DMA, BNRY one page behind the next frame to read.
 */
#ifndef NE2000TEST_COMMON_H
#define NE2000TEST_COMMON_H

#include <stdint.h>
#include <string.h>
#include "ne2000.h"

void ne2000_receive(void *opaque, const uint8_t *buf, int size);

static NE2000State *nic;
static int irq_level;
static uint8_t mac[6];
static int rcr = 0x04;          /* receive config: broadcasts and our address */

static void set_irq(void *pic, int irq, int level)
{
    irq_level = level;
}

static void out(int reg, int val)
{
    if (reg == 0x10)
        ne2000_asic_ioport_write(nic, 0x310, val);
    else if (reg == 0x1f)
        ne2000_reset_ioport_write(nic, 0x31f, val);
    else
        ne2000_ioport_write(nic, 0x300 + reg, val);
}

static int in(int reg)
{
    if (reg == 0x10)
        return ne2000_asic_ioport_read(nic, 0x310);
    if (reg == 0x1f)
        return ne2000_reset_ioport_read(nic, 0x31f);
    return ne2000_ioport_read(nic, 0x300 + reg);
}

/* n bytes at the card's address addr, a word at a time */
static void remote_read(unsigned addr, uint8_t *buf, int n)
{
    out(0x0a, n & 0xff);
    out(0x0b, n >> 8);
    out(0x08, addr & 0xff);
    out(0x09, addr >> 8);
    out(0x00, 0x0a);            /* remote read, start, page 0 */
    for (int i = 0; i < n; i += 2) {
        int w = in(0x10);
        buf[i] = w;
        if (i + 1 < n)
            buf[i + 1] = w >> 8;
    }
    out(0x07, 0x40);            /* clear remote DMA complete */
}

/* a register of page 1 or 2, then back to page 0 */
static int page_reg(int page, int reg)
{
    out(0, 0x22 | (page << 6));
    int v = in(reg);
    out(0, 0x22);
    return v;
}

static int curr(void)
{
    return page_reg(1, 0x07);
}

static void card_init(int start, int stop)
{
    uint8_t prom[32];

    in(0x1f);                   /* reset */
    out(0x1f, 0);
    out(0, 0x21);               /* stop, no DMA, page 0 */
    out(0x0e, 0x49);            /* word transfers */
    out(0x0a, 0);
    out(0x0b, 0);
    remote_read(0, prom, 32);   /* the station address PROM, each byte twice */
    for (int i = 0; i < 6; i++)
        mac[i] = prom[2 * i];
    out(0, 0x21);
    out(0x0c, rcr);
    out(0x0d, 0x02);            /* loopback while setting up */
    out(0x01, start);
    out(0x02, stop);
    out(0x03, stop - 1);
    out(0x07, 0xff);
    out(0x0f, 0x00);            /* no interrupts: the tests poll */
    out(0, 0x61);               /* page 1 */
    for (int i = 0; i < 6; i++)
        out(0x01 + i, mac[i]);
    for (int i = 0; i < 8; i++)
        out(0x08 + i, 0xff);
    out(0x07, start);           /* CURR */
    out(0, 0x22);               /* start, page 0 */
    out(0x0d, 0x00);
}

/* the ring registers as a broken or hostile driver might leave them, the
   card running */
static void set_ring(int start, int stop, int bnry, int cur)
{
    out(0, 0x22);
    out(0x01, start);
    out(0x02, stop);
    out(0x03, bnry);
    out(0, 0x62);
    out(0x07, cur);
    out(0, 0x22);
}

/* Reads up to max frames out of the ring, calling got() for each with the
   4-byte header and the data.  Returns the number read, or -1 when a header
   is impossible or got() returns nonzero. */
static int drain(int start, int stop, int max,
                 int (*got)(const uint8_t *hdr, const uint8_t *data, int n))
{
    static uint8_t data[2048];
    int count = 0;

    while (count < max) {
        int c = curr();
        int b = in(0x03);
        int p = b + 1;
        if (p >= stop)
            p = start;
        if (p == c)
            break;
        uint8_t hdr[4];
        remote_read(p << 8, hdr, 4);
        int len = hdr[2] | hdr[3] << 8;
        if (hdr[1] < start || hdr[1] >= stop || len < 64 || len > 1518)
            return -1;
        remote_read((p << 8) + 4, data, len - 4);
        if (got && got(hdr, data, len - 4))
            return -1;
        int nb = hdr[1] - 1;
        if (nb < start)
            nb = stop - 1;
        out(0x03, nb);
        count++;
    }
    out(0x07, 0x01);            /* clear received */
    return count;
}

#endif
