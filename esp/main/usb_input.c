/*
 * USB keyboards and mice, into the emulated PC's PS/2 keyboard and mouse
 * through kbd_input.c.
 *
 * Every HID interface of every device is looked at: a wireless receiver
 * gives its keyboard and its mouse as two interfaces of one device, each
 * handled on its own.  A boot keyboard or mouse is put in boot protocol and
 * read as such; any other interface has its report descriptor read
 * (hid_parse.c), and is used if a keyboard or a mouse is in it.  Each one
 * found is listed on the console (interface, subclass, protocol, VID:PID,
 * product), so that one run says what a device offers.
 *
 * The host runs full and low speed only (fsls_only), so that a USB 2.0
 * hub, which would otherwise attach at high speed and need a transaction
 * translator that this host stack does not have, attaches at full speed
 * and passes keyboards and mice through.
 *
 * Tasks, all on core 0: usb_events (the host library), the HID driver's
 * own task (reports arrive there), usb_task (connects, disconnects, the
 * keyboard lights), and kbd_input.c's kbd_repeat (typematic repeat).
 *
 * Based on Espressif's HID host example (Unlicense OR CC0-1.0).
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"

#include "kbd_input.h"
#include "hid_parse.h"
#include "console.h"

#define MAX_IFS 8

enum if_kind {
	IF_FREE,
	IF_KBD_BOOT,            /* boot protocol keyboard */
	IF_MOUSE_BOOT,          /* boot protocol mouse */
	IF_REPORT,              /* report protocol, read with hid_parse.c */
};

struct hid_if {
	enum if_kind kind;
	hid_host_device_handle_t handle;
	hid_host_dev_params_t params;
	int kbd;                /* kbd_input slot, -1 if not a keyboard */
	uint32_t errors;
	struct hid_layout layout;
};

static struct hid_if s_ifs[MAX_IFS];

enum usb_ev {
	EV_CONNECTED,           /* a new HID interface */
	EV_GONE,                /* an interface's device was unplugged */
	EV_LEDS,                /* the guest changed the keyboard lights */
};

struct usb_event {
	enum usb_ev ev;
	hid_host_device_handle_t handle;
	struct hid_if *hif;
};

static QueueHandle_t s_queue;
static volatile esp_err_t s_install_err = ESP_FAIL;
static volatile bool s_fsls_only;
static bool s_started;

static void post(enum usb_ev ev, hid_host_device_handle_t h, struct hid_if *hif)
{
	struct usb_event e = { .ev = ev, .handle = h, .hif = hif };
	if (s_queue)
		xQueueSend(s_queue, &e, 0);
}

/* ==== reports: the HID driver's task ==== */

static void handle_report(struct hid_if *hif, const uint8_t *d, size_t n)
{
	uint8_t boot[8];
	int dx, dy, wheel, buttons;

	switch (hif->kind) {
	case IF_KBD_BOOT:
		if (n >= 8)
			kbd_input_report(hif->kbd, d);
		break;
	case IF_MOUSE_BOOT:
		/* buttons, X, Y, and on most mice the wheel in a fourth byte */
		if (n >= 3)
			kbd_input_mouse((int8_t) d[1], (int8_t) d[2],
					n >= 4 ? (int8_t) d[3] : 0, d[0]);
		break;
	case IF_REPORT:
		if (hid_report_keyboard(&hif->layout, d, n, boot))
			kbd_input_report(hif->kbd, boot);
		if (hid_report_mouse(&hif->layout, d, n, &dx, &dy, &wheel, &buttons))
			kbd_input_mouse(dx, dy, wheel, buttons);
		break;
	default:
		break;
	}
}

static void if_callback(hid_host_device_handle_t h,
			const hid_host_interface_event_t event, void *arg)
{
	struct hid_if *hif = arg;
	uint8_t data[64];
	size_t n = 0;

	switch (event) {
	case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
		if (hid_host_device_get_raw_input_report_data(h, data, sizeof(data),
							      &n) == ESP_OK)
			handle_report(hif, data, n);
		break;
	case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
		/* closed by usb_task, which may be sending it the lights */
		post(EV_GONE, h, hif);
		break;
	case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
		hif->errors++;
		break;
	default:
		break;
	}
}

static void driver_callback(hid_host_device_handle_t h,
			    const hid_host_driver_event_t event, void *arg)
{
	if (event == HID_HOST_DRIVER_EVENT_CONNECTED)
		post(EV_CONNECTED, h, NULL);
}

/* ==== connects, disconnects, lights: usb_task ==== */

static void set_lights(struct hid_if *hif, uint8_t leds)
{
	/* a boot keyboard's output report: one byte, Num, Caps, Scroll */
	uint8_t v = leds;
	if (hif->kind == IF_KBD_BOOT)
		hid_class_request_set_report(hif->handle, HID_REPORT_TYPE_OUTPUT, 0,
					     &v, 1);
}

static void ascii_of(const wchar_t *w, char *out, int size)
{
	int o = 0;
	for (int i = 0; w[i] && i < HID_STR_DESC_MAX_LENGTH && o < size - 1; i++)
		out[o++] = (w[i] >= 0x20 && w[i] < 0x7f) ? (char) w[i] : '?';
	out[o] = 0;
}

static void on_connected(hid_host_device_handle_t h)
{
	hid_host_dev_params_t p;
	if (hid_host_device_get_params(h, &p) != ESP_OK)
		return;
	hid_host_dev_info_t info;
	char product[HID_STR_DESC_MAX_LENGTH + 1] = "";
	uint16_t vid = 0, pid = 0;
	if (hid_host_get_device_info(h, &info) == ESP_OK) {
		vid = info.VID;
		pid = info.PID;
		ascii_of(info.iProduct, product, sizeof(product));
	}

	struct hid_if *hif = NULL;
	for (int i = 0; i < MAX_IFS; i++) {
		if (s_ifs[i].kind == IF_FREE && !s_ifs[i].handle) {
			hif = &s_ifs[i];
			break;
		}
	}
	if (!hif) {
		console_printf(CON_WARN, "USB: more than %d HID interfaces; interface %d "
			       "of %04x:%04x is left alone.", MAX_IFS, p.iface_num, vid, pid);
		return;
	}
	memset(hif, 0, sizeof(*hif));
	hif->handle = h;
	hif->params = p;
	hif->kbd = -1;

	const hid_host_device_config_t cfg = {
		.callback = if_callback,
		.callback_arg = hif,
	};
	esp_err_t err = hid_host_device_open(h, &cfg);
	if (err != ESP_OK) {
		console_printf(CON_WARN, "USB: interface %d of %04x:%04x could not be "
			       "opened (%s).", p.iface_num, vid, pid, esp_err_to_name(err));
		hif->handle = NULL;
		return;
	}

	enum if_kind kind = IF_FREE;
	const char *how = "";
	if (p.sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
	    (p.proto == HID_PROTOCOL_KEYBOARD || p.proto == HID_PROTOCOL_MOUSE)) {
		if (hid_class_request_set_protocol(h, HID_REPORT_PROTOCOL_BOOT) == ESP_OK) {
			kind = p.proto == HID_PROTOCOL_KEYBOARD ? IF_KBD_BOOT : IF_MOUSE_BOOT;
			how = "boot protocol";
			if (kind == IF_KBD_BOOT)
				/* a report only when something changes; some
				   keyboards refuse, which does no harm */
				hid_class_request_set_idle(h, 0, 0);
		}
	}
	if (kind == IF_FREE) {
		size_t len = 0;
		uint8_t *desc = hid_host_get_report_descriptor(h, &len);
		if (desc && hid_parse(desc, len, &hif->layout)) {
			kind = IF_REPORT;
			how = hif->layout.uses_ids ? "report protocol, report IDs" :
				"report protocol";
		}
	}
	if (kind == IF_FREE) {
		console_printf(CON_INFO, "USB: %04x:%04x %s, interface %d (subclass %d, "
			       "protocol %d): not a keyboard or mouse, left alone.",
			       vid, pid, product, p.iface_num, p.sub_class, p.proto);
		hid_host_device_close(h);
		hif->handle = NULL;
		return;
	}
	if (kind == IF_KBD_BOOT || (kind == IF_REPORT && hif->layout.kbd)) {
		hif->kbd = kbd_input_open();
		if (hif->kbd < 0)
			console_printf(CON_WARN, "USB: more than %d keyboards; this one "
				       "is not used.", KBD_INPUT_DEVS);
	}
	hif->kind = kind;
	err = hid_host_device_start(h);
	if (err != ESP_OK) {
		console_printf(CON_WARN, "USB: interface %d of %04x:%04x could not be "
			       "started (%s).", p.iface_num, vid, pid, esp_err_to_name(err));
		kbd_input_close(hif->kbd);
		hid_host_device_close(h);
		memset(hif, 0, sizeof(*hif));
		return;
	}
	if (kind == IF_KBD_BOOT)
		set_lights(hif, kbd_input_leds());

	const char *what = kind == IF_KBD_BOOT ? "keyboard" :
		kind == IF_MOUSE_BOOT ? "mouse" : hid_layout_what(&hif->layout);
	console_printf(CON_GOOD, "USB: %s: %04x:%04x %s, interface %d (subclass %d, "
		       "protocol %d), %s.", what, vid, pid, product, p.iface_num,
		       p.sub_class, p.proto, how);
}

static void on_gone(struct hid_if *hif)
{
	if (!hif || !hif->handle)
		return;
	const char *what = hif->kind == IF_KBD_BOOT ? "keyboard" :
		hif->kind == IF_MOUSE_BOOT ? "mouse" : hid_layout_what(&hif->layout);
	int iface = hif->params.iface_num;
	uint32_t errors = hif->errors;
	/* every key it still held goes up first */
	kbd_input_close(hif->kbd);
	hid_host_device_close(hif->handle);
	memset(hif, 0, sizeof(*hif));
	console_printf(CON_INFO, "USB: %s on interface %d unplugged (%u transfer "
		       "errors).", what, iface, (unsigned) errors);
}

static void usb_task(void *arg)
{
	struct usb_event e;
	for (;;) {
		if (!xQueueReceive(s_queue, &e, portMAX_DELAY))
			continue;
		switch (e.ev) {
		case EV_CONNECTED:
			on_connected(e.handle);
			break;
		case EV_GONE:
			on_gone(e.hif);
			break;
		case EV_LEDS: {
			uint8_t leds = kbd_input_leds();
			for (int i = 0; i < MAX_IFS; i++)
				if (s_ifs[i].kind == IF_KBD_BOOT)
					set_lights(&s_ifs[i], leds);
			break;
		}
		}
	}
}

/* from the emulator's core: only noted, sent by usb_task */
static void leds_changed(uint8_t usb_leds)
{
	(void) usb_leds;
	post(EV_LEDS, NULL, NULL);
}

/* ==== the host ==== */

static void usb_lib_task(void *arg)
{
	usb_host_config_t host_config = {
		.skip_phy_setup = false,
		.intr_flags = ESP_INTR_FLAG_LOWMED,
		.fsls_only = true,
	};
	esp_err_t err = usb_host_install(&host_config);
	if (err == ESP_ERR_NOT_SUPPORTED) {
		/* this IDF cannot: high speed allowed, hubs then pass only high
		   speed devices */
		host_config.fsls_only = false;
		err = usb_host_install(&host_config);
	} else {
		s_fsls_only = err == ESP_OK;
	}
	s_install_err = err;
	xTaskNotifyGive(arg);
	if (err != ESP_OK) {
		vTaskDelete(NULL);
		return;
	}
	for (;;) {
		uint32_t event_flags;
		usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
	}
}

/* Starts the USB host, once; the keyboard and mouse go nowhere until the
   PC exists (kbd_input_set_target()), or to the boot menu. */
void usb_setup(void)
{
	if (s_started)
		return;
	s_started = true;

	s_queue = xQueueCreate(16, sizeof(struct usb_event));
	xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 3072,
				xTaskGetCurrentTaskHandle(), 2, NULL, 0);
	ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
	if (s_install_err != ESP_OK) {
		console_printf(CON_ERROR, "USB: the host could not be started (%s): no "
			       "keyboard or mouse.", esp_err_to_name(s_install_err));
		return;
	}

	const hid_host_driver_config_t hid_config = {
		.create_background_task = true,
		.task_priority = 5,
		.stack_size = 4096,
		.core_id = 0,
		.callback = driver_callback,
		.callback_arg = NULL,
	};
	esp_err_t err = hid_host_install(&hid_config);
	if (err != ESP_OK) {
		console_printf(CON_ERROR, "USB: the HID driver could not be started (%s): "
			       "no keyboard or mouse.", esp_err_to_name(err));
		return;
	}

	kbd_input_set_leds_fn(leds_changed);
	kbd_input_start();              /* typematic repeat */
	xTaskCreatePinnedToCore(usb_task, "usb_task", 4096, NULL, 2, NULL, 0);

	console_printf(CON_INFO, "USB host started (%s, hubs allowed): plug in a "
		       "keyboard and a mouse, or a receiver for both.",
		       s_fsls_only ? "full and low speed" : "all speeds; a hub passes "
		       "only high speed devices");
}
