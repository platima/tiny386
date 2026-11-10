/* ESP-IDF's esp_mac.h, as much of it as ne2000.c's BUILD_ESP32 part uses */
#ifndef NE2000TEST_ESP_MAC_H
#define NE2000TEST_ESP_MAC_H
#include <stdint.h>
typedef enum { ESP_MAC_WIFI_STA } esp_mac_type_t;
int esp_read_mac(uint8_t *mac, esp_mac_type_t type);
#endif
