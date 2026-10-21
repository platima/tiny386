/* host stand-in for ESP-IDF's esp_compiler.h */
#pragma once
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
