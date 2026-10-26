/* Host stand-in for ESP-IDF's esp_mac.h, for ne2000.c */
#ifndef LFB_SHIM_ESP_MAC_H
#define LFB_SHIM_ESP_MAC_H
#include <stdint.h>
#include <string.h>
#define ESP_MAC_WIFI_STA 0
static inline int esp_read_mac(uint8_t *mac, int type)
{
	static const uint8_t m[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
	(void) type;
	memcpy(mac, m, 6);
	return 0;
}
#endif
