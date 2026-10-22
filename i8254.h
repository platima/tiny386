#ifndef I8254_H
#define I8254_H
#include <stdint.h>

#define PIT_FREQ 1193182

typedef struct PITState PITState;
PITState *i8254_init(int irq, void *pic, void (*set_irq)(void *pic, int irq, int level));
void i8254_update_irq(PITState *pit);
/* The most of channel 0's ticks owed to the guest at once (1: one at a
   time, as a real 8254 and 8259 give them; 0: every missed tick raised
   afterwards, one each step, as tiny386 did before), and what was seen:
   the most owed at once and the ticks dropped (i8254.c). */
extern int i8254_max_owed;
extern uint32_t i8254_owed_max, i8254_dropped;
uint32_t i8254_ioport_read(PITState *pit, uint32_t addr1);
void i8254_ioport_write(PITState *pit, uint32_t addr, uint32_t val);

int pit_get_out(PITState *pit, int channel);
int pit_get_gate(PITState *pit, int channel);
int pit_get_initial_count(PITState *pit, int channel);
int pit_get_mode(PITState *pit, int channel);
void pit_set_gate(PITState *pit, int channel, int val);

#endif /* I8254_H */
