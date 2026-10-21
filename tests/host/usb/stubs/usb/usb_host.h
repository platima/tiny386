#ifndef STUB_USB_HOST_H
#define STUB_USB_HOST_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_INTR_FLAG_LOWMED 0
typedef struct {
	bool skip_phy_setup;
	int intr_flags;
	bool fsls_only;
} usb_host_config_t;
esp_err_t usb_host_install(const usb_host_config_t *c);
esp_err_t usb_host_lib_handle_events(uint32_t timeout, uint32_t *flags);
#endif
