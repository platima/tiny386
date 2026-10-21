/* Host stand-in for ESP-IDF's esp_rom_sys.h: esp_rom_printf() is printf(). */
#pragma once
#include <stdio.h>
#define esp_rom_printf printf
