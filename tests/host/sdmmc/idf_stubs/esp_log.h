/* host stand-in: error and warning lines counted and printed, the rest
   dropped (the board's build keeps levels up to INFO) */
#pragma once
#include <stdio.h>
extern int idf_log_errors;
#define ESP_LOGE(tag, fmt, ...) (idf_log_errors++, printf("    | E %s: " fmt "\n", tag, ##__VA_ARGS__))
#define ESP_LOGW(tag, fmt, ...) printf("    | W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) do { } while (0)
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
#define ESP_LOGV(tag, fmt, ...) do { } while (0)
