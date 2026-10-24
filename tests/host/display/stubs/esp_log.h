/* Host stand-in for ESP-IDF's esp_log.h (tests/host/display): the error
   lines to stdout, marked as the firmware's. */
#ifndef HOST_DISPLAY_ESP_LOG_H
#define HOST_DISPLAY_ESP_LOG_H
#include <stdio.h>
#define ESP_LOGE(tag, fmt, ...) printf("    | E %s: " fmt "\n", tag, ##__VA_ARGS__)
#endif
