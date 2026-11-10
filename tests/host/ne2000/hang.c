/*
 * ring.c's first hostile case, CURR at PSTOP, on a plain build of ne2000.c:
 * no hooks, no sanitizers, -O2.  run_ne2000.sh runs it under timeout: 30 s
 * for this tree's ne2000.c, which has to return, and 5 s for upstream's at
 * a080c5d, which has to be still in ne2000_receive().
 */
#include <stdio.h>
#include "common.h"

int main(void)
{
    static uint8_t f[1514];

    nic = isa_ne2000_init(0x300, 9, NULL, set_irq);
    card_init(0x46, 0x80);
    set_ring(0x46, 0x80, 0x7f, 0x80);
    memcpy(f, mac, 6);
    f[12] = 0x08;
    printf("hang: a 1514-byte frame, CURR at PSTOP\n");
    fflush(stdout);
    ne2000_receive(nic, f, sizeof(f));
    printf("hang: ne2000_receive() returned\n");
    return 0;
}
