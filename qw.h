/*
 * qw.h: the v5.2 quick wins, prototypes for counting on the PC (QUICKWINS-v52.md).  Each is on a
 * switch of its own: 0 is v5.1's code, 1 the prototype.  trees made by mktree.sh set them in
 * qw-config.h, which comes first here; with none set the tree compiles as v5.1 does.
 */
#ifndef QW_H
#define QW_H
#include "qw-config.h"
#ifndef QW_CF
#define QW_CF 0         /* get_CF(): the mask's test in line, the switch out of line */
#endif
#ifndef QW_CF2
#define QW_CF2 0        /* get_CF_cc(): ADD, SUB and the logical operations first */
#endif
#ifndef QW_INC
#define QW_INC 0        /* INC and DEC: nothing done for CF unless the lazy flags hold it */
#endif
#ifndef QW_NEXTFT
#define QW_NEXTFT 0     /* NEXT's two tests fall through: their far jumps right after it */
#endif
#ifndef QW_HINT
#define QW_HINT 0       /* likely and unlikely on the hot tests of modrm, sizes, the address */
#endif
#ifndef QW_NULLBIT
#define QW_NULLBIT 0    /* FAST_PL()'s null segment bit by one bext */
#endif
#ifndef QW_PIT
#define QW_PIT 0        /* i8254_update_irq(): no division until a period can have ended */
#endif
#ifndef QW_CLK1
#define QW_CLK1 0       /* one clock read a step on the board */
#endif
#ifndef QW_SEG
#define QW_SEG 0        /* a lean protected-mode segment load */
#endif
#ifndef QW_GRP
#define QW_GRP 0        /* the hot group operations with a NEXT of their own */
#endif
#ifndef QW_GRP5
#define QW_GRP5 0       /* FFh by a table of operations */
#endif
#ifndef QW_EA
#define QW_EA 0         /* fast_ea() shaped for the common forms */
#endif
#ifndef QW_INSV
#define QW_INSV 0       /* ins in memory */
#endif
#ifndef QW_LFBSZ
#define QW_LFBSZ 0      /* the frame buffer's store path by size */
#endif
#ifndef QW_PFX
#define QW_PFX 0        /* prefixes: fewer instructions */
#endif
#endif
