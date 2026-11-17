#pragma once
#include <stdint.h>
#include <stdbool.h>
#define I2C_MODE_MASTER 1
typedef struct { int mode; int sda_io_num; int scl_io_num; bool sda_pullup_en; bool scl_pullup_en; struct { uint32_t clk_speed; } master; } i2c_config_t;
typedef void *i2c_bus_handle_t;
static inline i2c_bus_handle_t i2c_bus_create(int port, const i2c_config_t *c) { (void) port; (void) c; return (void *) 1; }
