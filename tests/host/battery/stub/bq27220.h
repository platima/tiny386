#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "i2c_bus.h"
typedef struct { uint16_t full_charge_cap, design_cap, reserve_cap, near_full, self_discharge_rate, EDV0, EDV1, EDV2, EMF, C0, R0, T0, R1, TC, C1, DOD0, DOD10, DOD20, DOD30, DOD40, DOD50, DOD60, DOD70, DOD80, DOD90, DOD100; } parameter_cedv_t;
typedef struct { bool CCT, SC, FCC_LIM, FC_FOR_VDQ, IGNORE_SD; } gauging_config_t;
typedef struct { i2c_bus_handle_t i2c_bus; const gauging_config_t *cfg; const parameter_cedv_t *cedv; } bq27220_config_t;
typedef void *bq27220_handle_t;
static inline bq27220_handle_t bq27220_create(const bq27220_config_t *c) { (void) c; return (void *) 1; }
static inline esp_err_t bq27220_seal(bq27220_handle_t h) { (void) h; return ESP_OK; }
static inline esp_err_t bq27220_delete(bq27220_handle_t h) { (void) h; return ESP_OK; }
