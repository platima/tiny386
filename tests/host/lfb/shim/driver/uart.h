/* Host stand-in for ESP-IDF's driver/uart.h, for misc.c's serial port,
   which test_lfb.c never turns on */
#ifndef LFB_SHIM_DRIVER_UART_H
#define LFB_SHIM_DRIVER_UART_H
#include <stddef.h>
#include "freertos/FreeRTOS.h"
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1
#endif
static inline int uart_read_bytes(int port, void *buf, unsigned len, TickType_t t)
{ (void) port; (void) buf; (void) len; (void) t; return 0; }
static inline esp_err_t uart_get_buffered_data_len(int port, size_t *len)
{ (void) port; *len = 0; return ESP_OK; }
#endif
