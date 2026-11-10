/*
 * ne2000.c's receive ring, driven through the card's ports (common.h).
 *
 *   ring LOGDIR
 *
 * 1. Sane rings: frames of 14 to 1514 bytes, unicast and broadcast, fed to
 *    ne2000_receive() in bursts as the network sends them.  Either the guest
 *    keeps up (bursts of up to 4 frames, the ring read out after each) or it
 *    falls behind (bursts of up to 12, sometimes only a few frames read), so
 *    that the ring fills.  Every frame the card takes must come out whole, in
 *    order and with the right header; the card may take a frame only with
 *    room for a full-sized one, and drop one only without room for a
 *    full-sized one and a page more.  A hash of everything the guest read,
 *    and of which frames were dropped, is printed so that trees can be
 *    compared.  Then the ring filled to the page before BNRY, and a frame
 *    more.
 * 2. The ring registers at and past the ends of the ring and of the card's
 *    memory, set while the card runs: named cases, then every mix of a set
 *    of values for PSTART, PSTOP, BNRY and CURR.  Each gets a 1514-byte and a
 *    60-byte frame (the named cases a 1000-byte one too), then the card is
 *    set up again sanely and must deliver frames whole.
 * 3. The guest writing a ring register while a frame is being copied in, as
 *    it can from the other core in the ESP32 builds.
 *
 * Each case runs in a child process.  An endless copy loop stops it at once
 * (stubs.c), a timer after 5 s whatever happens, and a write outside the
 * card's memory stops it in AddressSanitizer (stubs.c's guard).  Exit
 * status 0 when every case is ok.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "stubs.h"

#ifdef BUILD_ESP32
#define FLAVOUR "BUILD_ESP32"
#else
#define FLAVOUR "host"
#endif

static const char *logdir = ".";

struct shared {
    char info[512];
};
static struct shared *sh;

/* ---- frames ---- */

static uint32_t seed = 1;

static uint32_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static uint32_t hash;

static void hash_bytes(const void *v, int n)
{
    const uint8_t *p = v;
    for (int i = 0; i < n; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
}

#define MAXP 256
static uint8_t pend[MAXP][1514];
static int pend_len[MAXP], pend_st[MAXP], ph, pt, nread;

static char why[256];          /* what got() or drain() found wrong */

static int got(const uint8_t *hdr, const uint8_t *data, int n)
{
    hash_bytes(hdr, 4);
    hash_bytes(data, n);
    if (ph == pt) {
        snprintf(why, sizeof(why), "read a frame that was not sent");
        return 1;
    }
    if (n != pend_len[ph] || hdr[0] != pend_st[ph] || memcmp(data, pend[ph], n)) {
        snprintf(why, sizeof(why), "frame %d read as %d bytes, status %02x; sent %d bytes, status %02x%s",
                 nread, n, hdr[0], pend_len[ph], pend_st[ph],
                 n == pend_len[ph] && memcmp(data, pend[ph], n) ? ", data differs" : "");
        return 1;
    }
    ph = (ph + 1) % MAXP;
    nread++;
    return 0;
}

/* reads the ring out; 0 if every frame sent came out whole */
static int read_all(int start, int stop, int max)
{
    why[0] = 0;
    int r = drain(start, stop, max, got);
    if (r < 0 && !why[0])
        snprintf(why, sizeof(why), "frame %d's header cannot be", nread);
    if (r >= 0 && max >= 1000 && ph != pt)
        snprintf(why, sizeof(why), "%d frames read, %d lost", nread, (pt - ph + MAXP) % MAXP);
    return why[0] != 0;
}

/* adds text to the case's line */
static void note(const char *fmt, const char *s)
{
    size_t n = strlen(sh->info);
    snprintf(sh->info + n, sizeof(sh->info) - n, fmt, s);
}

/* one frame to our address (or a broadcast) from the network; 1 if the card
   took it */
static int feed(int size, int bcast)
{
    static const uint8_t src[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    static uint8_t f[1514];

    if (bcast)
        memset(f, 0xff, 6);
    else
        memcpy(f, mac, 6);
    memcpy(f + 6, src, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    for (int i = 14; i < size; i++)
        f[i] = rnd();
    int c0 = curr();
    memcpy_calls = 0;
    ne2000_receive(nic, f, size);
    int took = curr() != c0;
    if (took) {
        memset(pend[pt], 0, 60);
        memcpy(pend[pt], f, size);
        pend_len[pt] = size < 60 ? 60 : size;
        pend_st[pt] = 0x01 | (bcast ? 0x20 : 0);
        pt = (pt + 1) % MAXP;
    }
    return took;
}

/* a sane set-up and 40 frames, all of which must come out whole */
static int works(void)
{
    card_init(0x46, 0x80);
    ph = pt = nread = 0;
    for (int i = 0; i < 40; i++) {
        if (!feed(60 + (i * 397) % 1455, i % 7 == 0)) {
            note("%s", "; after a sane set-up a frame was dropped");
            return EXIT_BAD;
        }
        if ((i % 5 == 4 || i == 39) && read_all(0x46, 0x80, 1000)) {
            note("; after a sane set-up, %s", why);
            return EXIT_BAD;
        }
    }
    return guard_dirty() ? EXIT_GUARD : 0;
}

/* ---- child processes ---- */

enum { RES_OK, RES_HANG, RES_TIMER, RES_GUARD, RES_BAD, RES_ASAN, RES_UBSAN, RES_CRASH, NRES };
static const char *res_name[NRES] = {
    "ok", "endless copy loop", "still running after 5 s",
    "wrote past the card's memory (guard)", "frames wrong",
    "AddressSanitizer", "UBSan", "crashed",
};

static pid_t spawn(int (*fn)(const void *), const void *arg, const char *log)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(log ? log : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 2);
        close(fd);
        alarm(5);
        int st = fn(arg);
        fflush(stdout);
        _exit(st);
    }
    return pid;
}

static int classify(int st)
{
    if (WIFSIGNALED(st))
        return WTERMSIG(st) == SIGALRM ? RES_TIMER : RES_CRASH;
    switch (WEXITSTATUS(st)) {
    case 0: return RES_OK;
    case EXIT_HANG: return RES_HANG;
    case EXIT_GUARD: return RES_GUARD;
    case EXIT_BAD: return RES_BAD;
    case EXIT_ASAN: return RES_ASAN;
    case EXIT_UBSAN: return RES_UBSAN;
    default: return RES_CRASH;
    }
}

static int failures;

/* runs one case, prints its line */
static void one(const char *name, int (*fn)(const void *), const void *arg)
{
    char log[600], line[300] = "";
    snprintf(log, sizeof(log), "%s/%s.log", logdir, name);
    for (char *p = log + strlen(logdir) + 1; *p; p++)
        if (*p == ' ' || *p == ',' || *p == '(' || *p == ')' || *p == '=')
            *p = '_';
    sh->info[0] = 0;
    int st;
    waitpid(spawn(fn, arg, log), &st, 0);
    int r = classify(st);
    if (r == RES_ASAN || r == RES_UBSAN) {
        FILE *f = fopen(log, "r");
        char buf[400];
        while (f && fgets(buf, sizeof(buf), f)) {
            char *e = strchr(buf, '\n');
            if (e)
                *e = 0;
            if (!strncmp(buf, "BADACCESS ", 10) || (!line[0] && strstr(buf, "runtime error")))
                snprintf(line, sizeof(line), "%s", buf + (buf[0] == 'B' ? 10 : 0));
        }
        if (f)
            fclose(f);
    }
    if (r != RES_OK)
        failures++;
    printf("  %-44s %s%s%s%s%s\n", name, res_name[r], line[0] ? ": " : "", line,
           sh->info[0] ? "; " : "", sh->info);
}

/* ---- 1. sane rings ---- */

struct sane { const char *name; int start, stop, frames, light; };

static int sane_case(const void *arg)
{
    const struct sane *c = arg;
    int start = c->start, stop = c->stop;
    int taken = 0, dropped = 0, sent = 0;

    seed = 1 + start * 256 + stop;
    hash = 2166136261u;
    card_init(start, stop);
    ph = pt = nread = 0;
    while (sent < c->frames) {
        int burst = 1 + rnd() % (c->light ? 4 : 12);
        for (int i = 0; i < burst && sent < c->frames; i++, sent++) {
            int r = rnd() % 8;
            int size = r == 0 ? 14 + rnd() % 46 : r == 1 ? 60 : r == 2 ? 1514 : 61 + rnd() % 1453;
            int bc = rnd() % 5 == 0;
            int cb = curr() << 8, bb = in(0x03) << 8;
            int room = cb < bb ? bb - cb : (stop - start) * 256 - (cb - bb);
            int t = feed(size, bc);
            uint8_t tb = t;
            hash_bytes(&tb, 1);
            /* taken only with room for a full-sized frame; dropped only
               without room for one and a page more */
            if ((t && room < 1518) || (!t && room >= 1792)) {
                snprintf(sh->info, sizeof(sh->info), "frame %d, %d bytes, %s with %d bytes of room",
                         sent, size, t ? "taken" : "dropped", room);
                return EXIT_BAD;
            }
            if (t)
                taken++;
            else
                dropped++;
        }
        int max = c->light || rnd() % 3 ? 1000 : 1 + rnd() % 4;
        if (read_all(start, stop, max)) {
            snprintf(sh->info, sizeof(sh->info), "after %d frames sent: %s", sent, why);
            return EXIT_BAD;
        }
    }
    if (read_all(start, stop, 1000)) {
        snprintf(sh->info, sizeof(sh->info), "at the end: %s", why);
        return EXIT_BAD;
    }
    if (guard_dirty())
        return EXIT_GUARD;
    snprintf(sh->info, sizeof(sh->info), "%d taken and read whole, %d dropped with the ring full, hash %08x",
             taken, dropped, hash);
    return 0;
}

static const struct sane sanes[] = {
    {"keeps up, PSTART 46 PSTOP 80 (Linux's)", 0x46, 0x80, 4000, 1},
    {"keeps up, PSTART 40 PSTOP c0 (all 32 KiB)", 0x40, 0xc0, 4000, 1},
    {"keeps up, PSTART 4c PSTOP 80", 0x4c, 0x80, 4000, 1},
    {"keeps up, PSTART 60 PSTOP 80", 0x60, 0x80, 4000, 1},
    {"falls behind, PSTART 46 PSTOP 80", 0x46, 0x80, 4000, 0},
    {"falls behind, PSTART 40 PSTOP c0", 0x40, 0xc0, 4000, 0},
    {"falls behind, PSTART 4c PSTOP 60", 0x4c, 0x60, 4000, 0},
    {"falls behind, PSTART 40 PSTOP 48 (8 pages)", 0x40, 0x48, 2000, 0},
    {"falls behind, PSTART bf PSTOP c0 (1 page)", 0xbf, 0xc0, 200, 0},
};

/* The ring filled to the page before BNRY with frames the guest has not
   read, then one frame more.  Linux's set-up, BNRY 7f: eight 1514-byte
   frames (48 pages) and a 600-byte one (3) take CURR from 46 to 79, which
   leaves 6 pages; a 1514-byte frame needs 6. */
static int fill_case(const void *arg)
{
    static const int sizes[] = {1514, 1514, 1514, 1514, 1514, 1514, 1514, 1514, 600, 1514, 60};
    char took[16] = "";

    card_init(0x46, 0x80);
    ph = pt = nread = 0;
    for (int i = 0; i < 11; i++)
        took[i] = feed(sizes[i], 0) ? 'y' : 'n';
    snprintf(sh->info, sizeof(sh->info), "frames taken %s, CURR %02x BNRY %02x", took, curr(), in(0x03));
    if (read_all(0x46, 0x80, 1000)) {
        note(", then %s", why);
        return EXIT_BAD;
    }
    size_t n = strlen(sh->info);
    snprintf(sh->info + n, sizeof(sh->info) - n, ", all %d read whole", nread);
    return guard_dirty() ? EXIT_GUARD : 0;
}

/* ---- 2. the ring registers at and past the ends ---- */

struct hostile { const char *name; int start, stop, bnry, cur; };

static const struct hostile hostiles[] = {
    {"CURR at PSTOP", 0x46, 0x80, 0x7f, 0x80},
    {"CURR past PSTOP", 0x46, 0x80, 0x7f, 0x90},
    {"CURR below PSTART", 0x46, 0x80, 0x60, 0x40},
    {"PSTART = PSTOP", 0x60, 0x60, 0x70, 0x5f},
    {"PSTART past PSTOP", 0x80, 0x46, 0x50, 0x46},
    {"PSTOP past the memory (ff)", 0x46, 0xff, 0x46, 0xbf},
    {"PSTART and PSTOP past the memory", 0xc8, 0xff, 0xf0, 0xfe},
    {"CURR at the end of the memory (c0)", 0x46, 0x80, 0xd0, 0xc0},
    {"ring 00 to ff, CURR ff", 0x00, 0xff, 0x10, 0xff},
    {"BNRY past PSTOP", 0x46, 0x80, 0xa0, 0x50},
    {"BNRY ff", 0x46, 0x80, 0xff, 0x50},
    {"BNRY = CURR", 0x46, 0x80, 0x50, 0x50},
};

static int hostile_case(const void *arg)
{
    const struct hostile *h = arg;
    static const int sizes[3] = {1514, 60, 1000};
    char took[4] = "---";

    card_init(0x46, 0x80);
    set_ring(h->start, h->stop, h->bnry, h->cur);
    int ps = page_reg(2, 0x01), pe = page_reg(2, 0x02), b = in(0x03), c = curr();
    snprintf(sh->info, sizeof(sh->info), "card holds PSTART %02x PSTOP %02x BNRY %02x CURR %02x",
             ps, pe, b, c);
    size_t n = strlen(sh->info);
    for (int i = 0; i < 3; i++) {
        took[i] = feed(sizes[i], 0) ? 'y' : 'n';
        snprintf(sh->info + n, sizeof(sh->info) - n, ", frames taken %s", took);
    }
    if (guard_dirty())
        return EXIT_GUARD;
    return works();
}

static const int vals[] = {0x00, 0x01, 0x40, 0x46, 0x7f, 0x80, 0xbf, 0xc0, 0xc1, 0xfe, 0xff};
#define NV ((int)(sizeof(vals) / sizeof(vals[0])))

static int sweep_case(const void *arg)
{
    const int *v = arg;
    card_init(0x46, 0x80);
    set_ring(v[0], v[1], v[2], v[3]);
    feed(1514, 0);
    feed(60, 0);
    if (guard_dirty())
        return EXIT_GUARD;
    return works();
}

static void sweep(void)
{
    enum { PAR = 16 };
    static int combo[NV * NV * NV * NV][4];
    int total = NV * NV * NV * NV, count[NRES] = {0}, nex[NRES] = {0};
    char ex[NRES][3][48];
    pid_t pid[PAR];
    int idx[PAR], running = 0;

    for (int k = 0; k < total; k++) {
        combo[k][0] = vals[k / (NV * NV * NV)];
        combo[k][1] = vals[k / (NV * NV) % NV];
        combo[k][2] = vals[k / NV % NV];
        combo[k][3] = vals[k % NV];
    }
    for (int k = 0; k <= total; k++) {
        while (running == PAR || (k == total && running > 0)) {
            int st;
            pid_t p = wait(&st);
            int s;
            for (s = 0; s < running && pid[s] != p; s++)
                ;
            if (s == running)
                continue;
            int r = classify(st), *c = combo[idx[s]];
            count[r]++;
            if (nex[r] < 3)
                snprintf(ex[r][nex[r]++], 48, "%02x %02x %02x %02x", c[0], c[1], c[2], c[3]);
            pid[s] = pid[running - 1];
            idx[s] = idx[running - 1];
            running--;
        }
        if (k == total)
            break;
        idx[running] = k;
        pid[running++] = spawn(sweep_case, combo[k], NULL);
    }
    printf("  every mix of PSTART, PSTOP, BNRY and CURR from {");
    for (int i = 0; i < NV; i++)
        printf("%s%02x", i ? " " : "", vals[i]);
    printf("}: %d cases\n", total);
    for (int r = 0; r < NRES; r++) {
        if (!count[r])
            continue;
        printf("    %-38s %5d", res_name[r], count[r]);
        if (r != RES_OK) {
            failures++;
            printf("  (PSTART PSTOP BNRY CURR:");
            for (int i = 0; i < nex[r]; i++)
                printf(" %s%s", ex[r][i], i + 1 < nex[r] ? ";" : "");
            printf(")");
        }
        printf("\n");
    }
}

/* ---- 3. a register written while a frame is copied in ---- */

struct race { const char *name; int start, stop, cur, size; void (*act)(void); };

static void act_start_is_stop(void) { out(0x01, 0x80); }
static void act_start_past_stop(void) { out(0x01, 0x90); }
static void act_stop_down(void) { out(0x02, 0x50); }
static void act_stop_up(void) { out(0x02, 0xff); }

static const struct race races[] = {
    {"PSTART set to PSTOP (80) mid-frame", 0x46, 0x80, 0x7e, 1000, act_start_is_stop},
    {"PSTART set past PSTOP (90) mid-frame", 0x46, 0x80, 0x7e, 1000, act_start_past_stop},
    {"PSTOP lowered to 50 mid-frame", 0x46, 0x80, 0x7e, 1000, act_stop_down},
    {"PSTOP raised to ff mid-frame, ring 40-c0", 0x40, 0xc0, 0xbf, 1000, act_stop_up},
};

static int race_case(const void *arg)
{
    const struct race *r = arg;
    card_init(r->start, r->stop);
    set_ring(r->start, r->stop, r->cur - 1, r->cur);
    memcpy_after = r->act;
    int t = feed(r->size, 0);
    memcpy_after = NULL;
    snprintf(sh->info, sizeof(sh->info), "frame %s, then PSTART %02x PSTOP %02x CURR %02x",
             t ? "taken" : "dropped", page_reg(2, 0x01), page_reg(2, 0x02), curr());
    if (guard_dirty())
        return EXIT_GUARD;
    return works();
}

int main(int argc, char **argv)
{
    if (argc > 1)
        logdir = argv[1];
    sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    nic = isa_ne2000_init(0x300, 9, NULL, set_irq);

    printf("ring, the %s build\n", FLAVOUR);
    printf(" 1. sane rings\n");
    for (size_t i = 0; i < sizeof(sanes) / sizeof(sanes[0]); i++)
        one(sanes[i].name, sane_case, &sanes[i]);
    one("filled to the page before BNRY, then more", fill_case, NULL);
    printf(" 2. ring registers at and past the ends, frames of 1514, 60 and 1000 bytes\n");
    for (size_t i = 0; i < sizeof(hostiles) / sizeof(hostiles[0]); i++)
        one(hostiles[i].name, hostile_case, &hostiles[i]);
    sweep();
    printf(" 3. a ring register written while a 1000-byte frame is copied in\n");
    for (size_t i = 0; i < sizeof(races) / sizeof(races[0]); i++)
        one(races[i].name, race_case, &races[i]);
    printf("ring, the %s build: %s\n", FLAVOUR, failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
