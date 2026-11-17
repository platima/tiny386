/*
 * The ESP-Mosaico's battery (battery_mosaico.c): its BQ27220 fuel gauge,
 * read by a task of the board's own, for the information port (perfport.h,
 * PP_I_BATTERY_*).
 */
#ifndef BATTERY_MOSAICO_H
#define BATTERY_MOSAICO_H

#include <stdint.h>
#include "driver/i2c_master.h"

/* The gauge on the board's I2C bus: its profile checked (and written if it
   holds another), then read every 2 s, all on a task of its own on core 0.
   Returns at once. */
void battery_mosaico_start(i2c_master_bus_handle_t bus);

/* The information port's battery values, as last read (speed.c's snapshot,
   on the emulator's core: copies only, no I2C) */
void board_perfport_fill(uint32_t *val, uint32_t *valid);

#endif /* BATTERY_MOSAICO_H */
