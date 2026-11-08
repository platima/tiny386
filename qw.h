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
/* after v5.2: fast_exec()'s host instructions, measured on the board's compiler (fastx) */
#ifndef QW_OPSREG
#define QW_OPSREG 0     /* the dispatch table's address in a register of its own (RV32) */
#endif
#ifndef QW_STK
#define QW_STK 0        /* the hot stack handlers with the stack's size as a constant */
#endif
#ifndef QW_NULLI
#define QW_NULLI 0      /* a constant segment's null bit by one bexti */
#endif
#ifndef QW_EAP
#define QW_EAP 0        /* the hot handlers' common memory form in one test of the prefixes (2: fewer) */
#endif
#ifndef QW_JCCV
#define QW_JCCV 0       /* Jcc near with a handler for each condition */
#endif
#ifndef QW_OPS0F
#define QW_OPS0F 0      /* ops0f[] right after ops[], at a constant offset from it */
#endif
#ifndef QW_EAP16
#define QW_EAP16 0      /* with QW_EAP: 16-bit code's common memory form in one test too */
#endif
#ifndef QW_EAS
#define QW_EAS 0        /* with QW_EAP: the lookup in two copies, DS and SS as constants */
#endif
#ifndef QW_SEGL
#define QW_SEGL 0       /* a memory operand's segment a variable of each handler's block */
#endif
#ifndef QW_FOLD2
#define QW_FOLD2 0      /* a JZ or JNZ after CMP or TEST that is not fused done without its dispatch */
#endif
#ifndef QW_NOFOLD
#define QW_NOFOLD 0     /* the fusion rule's instruction kept as a value of steps */
#endif
#ifndef QW_GRPTAB
#define QW_GRPTAB 0     /* the group tables just before ops[] (with QW_OPS0F and QW_GRP5) */
#endif
#endif
