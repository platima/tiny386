/* Host stand-in for esp_err.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_ESP_ERR_H
#define BC_SHIM_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif
