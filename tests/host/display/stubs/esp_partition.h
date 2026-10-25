/* Host stand-in for ESP-IDF's esp_partition.h (tests/host/display): only
   an app partition's subtype. */
#ifndef HOST_DISPLAY_ESP_PARTITION_H
#define HOST_DISPLAY_ESP_PARTITION_H
typedef enum {
	ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
	ESP_PARTITION_SUBTYPE_APP_TEST = 0x20,
} esp_partition_subtype_t;
typedef struct {
	esp_partition_subtype_t subtype;
} esp_partition_t;
#endif
