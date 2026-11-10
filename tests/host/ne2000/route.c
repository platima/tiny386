/*
 * The ESP32 builds' frame filter, through wlanif_l2_input_hook(): which
 * frames from the Wi-Fi go to the guest's NE2000, which to the board's own
 * lwIP, and which to both.  ne2000.c is compiled with BUILD_ESP32
 * (run_ne2000.sh).
 *
 * The rules, from ne2000.c's filter(): IPv4 TCP to port 9999 is the board's,
 * other TCP the guest's; UDP to port 68 (DHCP replies) goes to both, other
 * UDP to the guest; other IPv4 to the guest; ARP to both; anything else, and
 * any frame under 38 bytes, to the board.  The port is the one after the IP
 * header, however long its options, and only a first fragment has one.
 *
 *   route
 *
 * Exit status 0 when every frame goes where it should.
 */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "stubs.h"

int wlanif_l2_input_hook(uint8_t *buf, int size);

enum { BOARD, GUEST, BOTH, DROPPED };
static const char *where[] = {"board", "guest", "both", "dropped"};

static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t board[6] = {0x02, 0x00, 0x5e, 0x10, 0x00, 0x01};
static const uint8_t other[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

static int failures;

static int route(uint8_t *f, int size)
{
    card_init(0x46, 0x80);
    int c0 = curr();
    int r = wlanif_l2_input_hook(f, size);
    int took = curr() != c0;
    return took ? (r ? GUEST : BOTH) : (r ? DROPPED : BOARD);
}

static void check(const char *name, int want, uint8_t *f, int size)
{
    int got = route(f, size);
    if (got != want)
        failures++;
    printf("  %-60s %-5s %s%s\n", name, where[got], got == want ? "ok" : "WRONG, should be ",
           got == want ? "" : where[want]);
}

static int eth(uint8_t *f, const uint8_t *dst, int type)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, other, 6);
    f[12] = type >> 8;
    f[13] = type;
    return 14;
}

/* an IPv4 header after the Ethernet header, with nopts bytes of options
   (a multiple of 4) and the fragment field frag, then n bytes of l4 */
static int ipv4(uint8_t *f, int proto, int frag, const uint8_t *opts, int nopts,
                const uint8_t *l4, int n)
{
    uint8_t *ip = f + 14;
    int ihl = 20 + nopts, tot = ihl + n;
    static const uint8_t addrs[8] = {192, 168, 4, 2, 192, 168, 4, 10};
    ip[0] = 0x40 | ihl / 4;
    ip[1] = 0;
    ip[2] = tot >> 8;
    ip[3] = tot;
    ip[4] = 0x12;
    ip[5] = 0x34;
    ip[6] = frag >> 8;
    ip[7] = frag;
    ip[8] = 64;
    ip[9] = proto;
    ip[10] = ip[11] = 0;
    memcpy(ip + 12, addrs, 8);
    if (nopts)
        memcpy(ip + 20, opts, nopts);
    uint32_t sum = 0;
    for (int i = 0; i < ihl; i += 2)
        sum += ip[i] << 8 | ip[i + 1];
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    ip[10] = ~sum >> 8;
    ip[11] = ~sum;
    memcpy(ip + ihl, l4, n);
    return 14 + tot;
}

static int udp(uint8_t *l4, int sport, int dport, int n)
{
    l4[0] = sport >> 8;
    l4[1] = sport;
    l4[2] = dport >> 8;
    l4[3] = dport;
    l4[4] = (8 + n) >> 8;
    l4[5] = 8 + n;
    l4[6] = l4[7] = 0;
    for (int i = 0; i < n; i++)
        l4[8 + i] = i * 7;
    return 8 + n;
}

static int tcp(uint8_t *l4, int sport, int dport)
{
    memset(l4, 0, 20);
    l4[0] = sport >> 8;
    l4[1] = sport;
    l4[2] = dport >> 8;
    l4[3] = dport;
    l4[12] = 0x50;
    l4[13] = 0x02;              /* SYN */
    l4[14] = 0x20;
    return 20;
}

#define MF 0x2000

int main(void)
{
    static uint8_t f[1514], l4[1514];
    static const uint8_t ra[4] = {0x94, 0x04, 0x00, 0x00};       /* Router Alert */
    static const uint8_t ra_0044[4] = {0x94, 0x04, 0x00, 0x44};  /* its value 0044 */
    static const uint8_t ra_270f[4] = {0x94, 0x04, 0x27, 0x0f};
    uint8_t nops[40];
    int n;

    memset(nops, 0x01, sizeof(nops));
    memcpy_limit = 1L << 40;    /* stubs.c's count is for ring.c */
    rcr = 0x14;                /* broadcasts and promiscuous: the card takes all */
    nic = isa_ne2000_init(0x300, 9, NULL, set_irq);
    printf("route, the BUILD_ESP32 build's filter: where each frame from the Wi-Fi goes\n");

    eth(f, bcast, 0x0806);
    memset(f + 14, 0, 28);
    f[15] = 1; f[16] = 8; f[18] = 6; f[19] = 4; f[21] = 1;
    check("ARP request", BOTH, f, 42);

    eth(f, bcast, 0x0800);
    n = udp(l4, 67, 68, 300);
    check("UDP 67 to 68 (DHCP), no IP options", BOTH, f, ipv4(f, 17, 0, NULL, 0, l4, n));
    check("UDP 67 to 68 (DHCP), IP option Router Alert (IHL 6)", BOTH, f, ipv4(f, 17, 0, ra, 4, l4, n));
    check("UDP 67 to 68 (DHCP), 40 bytes of IP options (IHL 15)", BOTH, f, ipv4(f, 17, 0, nops, 40, l4, n));
    check("UDP 67 to 68 (DHCP), first fragment (MF, offset 0)", BOTH, f, ipv4(f, 17, MF, NULL, 0, l4, n));
    eth(f, board, 0x0800);
    check("UDP 68 to 67 (another client's DHCP request)", GUEST, f, ipv4(f, 17, 0, NULL, 0, l4, udp(l4, 68, 67, 300)));
    check("UDP to 53", GUEST, f, ipv4(f, 17, 0, NULL, 0, l4, udp(l4, 1024, 53, 40)));
    n = udp(l4, 1024, 5000, 100);
    check("UDP to 5000, IP option with bytes 36-37 00 44", GUEST, f, ipv4(f, 17, 0, ra_0044, 4, l4, n));
    memset(l4, 0, 200);
    l4[2] = 0x00; l4[3] = 0x44;  /* frame bytes 36-37 */
    check("UDP, a later fragment (offset 1480), bytes 36-37 00 44", GUEST, f, ipv4(f, 17, 185, NULL, 0, l4, 200));
    check("UDP, a middle fragment (MF, offset 1480), bytes 36-37 00 44", GUEST, f, ipv4(f, 17, MF | 185, NULL, 0, l4, 200));

    n = tcp(l4, 40000, 9999);
    check("TCP to 9999, no IP options", BOARD, f, ipv4(f, 6, 0, NULL, 0, l4, n));
    check("TCP to 9999, IP option Router Alert (IHL 6)", BOARD, f, ipv4(f, 6, 0, ra, 4, l4, n));
    check("TCP to 9999, 40 bytes of IP options (IHL 15)", BOARD, f, ipv4(f, 6, 0, nops, 40, l4, n));
    check("TCP from 9999 to 80", GUEST, f, ipv4(f, 6, 0, NULL, 0, l4, tcp(l4, 9999, 80)));
    n = tcp(l4, 40000, 80);
    check("TCP to 80", GUEST, f, ipv4(f, 6, 0, NULL, 0, l4, n));
    check("TCP to 80, IP option with bytes 36-37 27 0f", GUEST, f, ipv4(f, 6, 0, ra_270f, 4, l4, n));
    memset(l4, 0, 200);
    l4[2] = 0x27; l4[3] = 0x0f;
    check("TCP, a later fragment (offset 1480), bytes 36-37 27 0f", GUEST, f, ipv4(f, 6, 185, NULL, 0, l4, 200));

    memset(l4, 0, 64);
    l4[0] = 8;                  /* echo request */
    check("ICMP echo request", GUEST, f, ipv4(f, 1, 0, NULL, 0, l4, 64));
    n = ipv4(f, 6, 0, NULL, 0, l4, tcp(l4, 40000, 9999));
    check("IPv4 cut to 34 bytes (under 38)", BOARD, f, 34);
    eth(f, board, 0x86dd);
    memset(f + 14, 0, 60);
    f[14] = 0x60;
    check("IPv6", BOARD, f, 74);

    printf("route: %s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
