/* host stand-in for ESP-IDF's esp_vfs_fat.h: the registration only records
   which FatFs drive a path is (host_glue.c), as the real one's context does */
#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "ff.h"
typedef struct {
	const char *base_path;
	const char *fat_drive;
	size_t max_files;
} esp_vfs_fat_conf_t;
esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *conf, FATFS **out_fs);
esp_err_t esp_vfs_fat_unregister_path(const char *base_path);
