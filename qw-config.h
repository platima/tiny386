/* qw-config.h: which of qw.h's prototypes are on: those kept for v5.2 (QUICKWINS-v52.md) */
#define QW_CF 1
#define QW_CF2 1
#define QW_INC 1
#define QW_NEXTFT 1
#define QW_INSV 1
#define QW_HINT 1
#define QW_NULLBIT 1
#define QW_EA 1
#define QW_SEG 1
#define QW_GRP 1
#define QW_GRP5 1
#define QW_LFBSZ 1
#define QW_PFX 1
#define QW_PIT 1
#define QW_CLK1 1
/* after v5.2 (fastx): fast_exec()'s host instructions on the board's compiler */
#define QW_OPSREG 1
#define QW_OPS0F 1
#define QW_GRPTAB 1
#define QW_NULLI 1
#define QW_JCCV 1
#define QW_STK 2
#define QW_NOFOLD 1
#define QW_EAP 2
#define QW_SEGL 1
#define QW_FOLD2 1
#define QW_DECJ 1
/* v5.3: fast_exec()'s 16-bit paths */
#define QW_EA16 1
#define QW_J16 1
#define QW_INCL 1
#define QW_EA16T 1
#define QW_COND2 1
#define QW_REPMIS 1
#define QW_RMREF 1
/* v5.3: the clock and the timer */
#define QW_UCLK 1
