/* The ESP-Mosaico's touch panel as the guest's mouse (touch_mosaico.c). */
#ifndef TOUCH_MOSAICO_H
#define TOUCH_MOSAICO_H

/* i2c_bus: the board's i2c_master_bus_handle_t (mosaico.c) */
void touch_mosaico_start(void *i2c_bus);

#endif /* TOUCH_MOSAICO_H */
