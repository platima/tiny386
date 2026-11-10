/* host stand-in: the ROM's busy-wait, on idf_card.c's pretend clock */
#pragma once
#include <stdint.h>
void esp_rom_delay_us(uint32_t us);
