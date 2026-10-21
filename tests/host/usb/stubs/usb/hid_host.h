/* Host stand-in for espressif/usb_host_hid 1.2.0's API as usb_input.c uses
   it.  The close behaviour is modelled in model_test.c on hid_host.c
   lines 1441 to 1492 (two-phase close). */
#ifndef STUB_HID_HOST_H
#define STUB_HID_HOST_H
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#include "esp_err.h"

#define HID_STR_DESC_MAX_LENGTH 32
#define HID_SUBCLASS_BOOT_INTERFACE 1
#define HID_PROTOCOL_NONE 0
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_MOUSE 2
#define HID_REPORT_PROTOCOL_BOOT 0
#define HID_REPORT_TYPE_OUTPUT 2

typedef struct model_iface *hid_host_device_handle_t;

typedef enum {
	HID_HOST_INTERFACE_EVENT_INPUT_REPORT = 0,
	HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR,
	HID_HOST_INTERFACE_EVENT_DISCONNECTED,
} hid_host_interface_event_t;

typedef enum {
	HID_HOST_DRIVER_EVENT_CONNECTED = 0,
} hid_host_driver_event_t;

typedef struct {
	uint8_t addr;
	uint8_t iface_num;
	uint8_t sub_class;
	uint8_t proto;
} hid_host_dev_params_t;

typedef struct {
	uint16_t VID, PID;
	wchar_t iManufacturer[HID_STR_DESC_MAX_LENGTH];
	wchar_t iProduct[HID_STR_DESC_MAX_LENGTH];
	wchar_t iSerialNumber[HID_STR_DESC_MAX_LENGTH];
} hid_host_dev_info_t;

typedef void (*hid_host_interface_event_cb_t)(hid_host_device_handle_t,
					      const hid_host_interface_event_t, void *);
typedef void (*hid_host_driver_event_cb_t)(hid_host_device_handle_t,
					   const hid_host_driver_event_t, void *);

typedef struct {
	hid_host_interface_event_cb_t callback;
	void *callback_arg;
} hid_host_device_config_t;

typedef struct {
	int create_background_task;
	int task_priority;
	int stack_size;
	int core_id;
	hid_host_driver_event_cb_t callback;
	void *callback_arg;
} hid_host_driver_config_t;

esp_err_t hid_host_install(const hid_host_driver_config_t *c);
esp_err_t hid_host_device_open(hid_host_device_handle_t h, const hid_host_device_config_t *c);
esp_err_t hid_host_device_close(hid_host_device_handle_t h);
esp_err_t hid_host_device_start(hid_host_device_handle_t h);
esp_err_t hid_host_device_get_params(hid_host_device_handle_t h, hid_host_dev_params_t *p);
esp_err_t hid_host_get_device_info(hid_host_device_handle_t h, hid_host_dev_info_t *i);
esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t h, uint8_t *d,
						     size_t max, size_t *n);
uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t h, size_t *len);
esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t h, int proto);
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t h, int d, int id);
esp_err_t hid_class_request_set_report(hid_host_device_handle_t h, int type, int id,
				       uint8_t *d, size_t n);
#endif
