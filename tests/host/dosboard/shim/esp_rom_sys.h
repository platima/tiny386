/* Host stand-in for esp_rom_sys.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_ESP_ROM_SYS_H
#define BC_SHIM_ESP_ROM_SYS_H
#include <stdint.h>
#include <stdio.h>
#define esp_rom_printf printf
void esp_rom_delay_us(uint32_t us);
#endif
