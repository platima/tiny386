#ifndef I386_H
#define I386_H

#ifndef I386_IN_AMD64
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef u32 uword;
typedef s32 sword;
#else
#define CPU_CB CPUI386_CB
#define uword u32
#define sword s32
#endif

typedef struct CPUI386 CPUI386;

typedef struct {
	void *pic;
	int (*pic_read_irq)(void *);

	void *io;
	u8 (*io_read8)(void *, int);
	void (*io_write8)(void *, int, u8);
	u16 (*io_read16)(void *, int);
	void (*io_write16)(void *, int, u16);
	u32 (*io_read32)(void *, int);
	void (*io_write32)(void *, int, u32);
	int (*io_read_string)(void *, int, uint8_t *, int, int);
	int (*io_write_string)(void *, int, uint8_t *, int, int);

	void *iomem;
	u8 (*iomem_read8)(void *, uword);
	void (*iomem_write8)(void *, uword, u8);
	u16 (*iomem_read16)(void *, uword);
	void (*iomem_write16)(void *, uword, u16);
	u32 (*iomem_read32)(void *, uword);
	void (*iomem_write32)(void *, uword, u32);
	bool (*iomem_write_string)(void *, uword, uint8_t *, int);

	/* v5.0: the VBE linear frame buffer, which the CPU loads from and
	   stores into itself, without the iomem calls above (pc.c sets it with
	   cpu_cb_set_lfb() below).  Physical bytes lfb_base onwards are
	   lfb_mem[0] onwards; an access of 1, 2 or 4 bytes at off = addr -
	   lfb_base is all inside when off < lfb_lim1, lfb_lim2 or lfb_lim4
	   (0: no frame buffer, the iomem calls take everything).  Each store
	   sets *lfb_wrote; and while *lfb_marks_on, after a release fence,
	   sets lfb_map[off >> lfb_shift] for every byte off it stored, as
	   vga.h's vga_dirty_mark() does.  Looked at only from 0xE0000000 up,
	   where in_iomem() in i386.c sends an access to the calls above, and
	   for REP STOS and REP MOVS there; the VGA window, an access that
	   crosses a page and anything not all inside go to the calls as
	   before. */
	uword lfb_base, lfb_lim1, lfb_lim2, lfb_lim4;
	u8 *lfb_mem;
	u8 *lfb_map;
	int lfb_shift;
	const int *lfb_marks_on;
	u8 *lfb_wrote;
	/* counts cpu_cb_set_lfb()'s calls: the fast interpreter's TLB entries
	   for the frame buffer (i386_fast.h) go when it moves */
	unsigned lfb_gen;
} CPU_CB;

/* Bytes base to base + size - 1 are mem[0] to mem[size - 1]; size 0 for
   none.  The marks and the flag stay as they were set. */
static inline void cpu_cb_set_lfb(CPU_CB *cb, uword base, uword size, u8 *mem)
{
	cb->lfb_gen++;
	cb->lfb_base = base;
	cb->lfb_mem = mem;
	cb->lfb_lim1 = size;
	cb->lfb_lim2 = size >= 2 ? size - 1 : 0;
	cb->lfb_lim4 = size >= 4 ? size - 3 : 0;
}

/* The marks for the stores above: one byte for each (1 << shift) bytes of
   the frame buffer, set while *on (map NULL: none), and the flag that each
   store sets.  Set before cpu_cb_set_lfb() makes a frame buffer. */
static inline void cpu_cb_set_lfb_marks(CPU_CB *cb, u8 *map, int shift,
					const int *on, u8 *wrote)
{
	static const int never;
	cb->lfb_map = map;
	cb->lfb_shift = shift;
	cb->lfb_marks_on = map ? on : &never;
	cb->lfb_wrote = wrote;
}

typedef struct {
	u32 gpr[8];
	u32 seg[8];
	u32 ip;
	u32 flags;
	u32 gdt_base, gdt_limit;
	u32 idt_base, idt_limit;
	u32 cr0, cr2, cr3;
	uint64_t efer;
	// TODO: fpu state...
} CPUI386_State;

void cpui386_get_state(CPUI386 *cpu, CPUI386_State *state);
int cpui386_get_excno(CPUI386 *cpu);

CPUI386 *cpui386_new(int gen, char *phys_mem, long phys_mem_size, CPU_CB **cb);
void cpui386_delete(CPUI386 *cpu);
void cpui386_enable_fpu(CPUI386 *cpu);
void cpui386_reset(CPUI386 *cpu);
void cpui386_reset_pm(CPUI386 *cpu, uint32_t start_addr);
void cpui386_step(CPUI386 *cpu, int stepcount);
void cpui386_raise_irq(CPUI386 *cpu);
void cpui386_set_gpr(CPUI386 *cpu, int i, u32 val);
long cpui386_get_cycle(CPUI386 *cpu);
/* v5.0: the fast interpreter (i386_fast.h, with I386_FAST) on or off for
   this CPU; without I386_FAST, set does nothing and get says 0 */
void cpui386_set_fast(CPUI386 *cpu, int on);
int cpui386_get_fast(CPUI386 *cpu);

#ifndef I386_IN_AMD64
bool cpu_load8(CPUI386 *cpu, int seg, uword addr, u8 *res);
bool cpu_store8(CPUI386 *cpu, int seg, uword addr, u8 val);
bool cpu_load16(CPUI386 *cpu, int seg, uword addr, u16 *res);
bool cpu_store16(CPUI386 *cpu, int seg, uword addr, u16 val);
bool cpu_load32(CPUI386 *cpu, int seg, uword addr, u32 *res);
bool cpu_store32(CPUI386 *cpu, int seg, uword addr, u32 val);
void cpu_setax(CPUI386 *cpu, u16 ax);
u16 cpu_getax(CPUI386 *cpu);
void cpu_setexc(CPUI386 *cpu, int excno, uword excerr);
void cpu_setflags(CPUI386 *cpu, uword set_mask, uword clear_mask);
uword cpu_getflags(CPUI386 *cpu);
void cpu_abort(CPUI386 *cpu, int code);
/* called first thing in cpu_abort(), if set: a front end's own lines for
   the log */
extern void (*cpu_abort_note)(void);
#else
#undef CPU_CB
#undef uword
#undef sword
#endif

#endif /* I386_H */
