/*
 * Host test of esp/main/usb_input.c, compiled as it is, with a stand-in for
 * espressif/usb_host_hid 1.2.0 whose hid_host_device_close() follows the
 * component's two-step close (hid_host.c lines 1441 to 1492: the first call
 * on an interface with a user callback sets WAIT_USER_DELETION and calls
 * that callback with DISCONNECTED, in the caller's task; only a second call
 * removes it), and whose unplug closes each interface once and then frees
 * the device (hid_host.c 536 to 565, 1242 to 1268).  The interface
 * CONNECTED events of one device come in interface order (hid_host.c 399).
 *
 * From the model the concurrency review wrote for its finding F1 (v3 closed
 * the wrong interface).  usb_task() runs until its queue is empty, then the
 * state of every interface is checked:
 *   A  [vendor, boot keyboard]: the vendor interface removed, the keyboard on
 *   B  [boot keyboard, vendor], then the keyboard unplugged
 *   C  [boot keyboard, vendor] and a boot mouse queued behind them
 *   D  a light change queued ahead of an unplug: no SET_REPORT to the freed
 *      device (review F2)
 *   E  an interface that will not start: removed, and the next one kept
 *
 *   test_usb_input            exit status 0 when every check passes
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include USB_INPUT_C

/* ==== FreeRTOS stand-ins ==== */

struct stub_queue {
	int len, item, head, count;
	unsigned char *buf;
};
struct stub_sem {
	int given;
};

static jmp_buf s_stop;

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item)
{
	struct stub_queue *q = calloc(1, sizeof(*q));
	q->len = len;
	q->item = item;
	q->buf = calloc(len, item);
	return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
	(void) wait;
	if (q->count == q->len)
		return pdFALSE;
	memcpy(q->buf + ((q->head + q->count) % q->len) * q->item, item, q->item);
	q->count++;
	return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
	(void) wait;
	if (!q->count)
		longjmp(s_stop, 1);     /* usb_task would block: the run is over */
	memcpy(item, q->buf + q->head * q->item, q->item);
	q->head = (q->head + 1) % q->len;
	q->count--;
	return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	return calloc(1, sizeof(struct stub_sem));
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
	s->given = 1;
	return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
	(void) wait;
	if (!s->given)
		return pdFALSE;
	s->given = 0;
	return pdTRUE;
}

static int s_tasks;
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
				   void *arg, UBaseType_t prio, TaskHandle_t *out,
				   BaseType_t core)
{
	(void) name; (void) stack; (void) prio; (void) core;
	if (out)
		*out = NULL;
	s_tasks++;
	/* usb_lib_task installs the host and then waits for events for ever:
	   run only the install part here, as far as the semaphore */
	if (fn == usb_lib_task) {
		s_install_err = ESP_OK;
		s_fsls_only = true;
		xSemaphoreGive(s_installed);
	}
	(void) arg;
	return pdTRUE;
}
void vTaskDelete(TaskHandle_t t) { (void) t; }
esp_err_t usb_host_install(const usb_host_config_t *c) { (void) c; return ESP_OK; }
esp_err_t usb_host_lib_handle_events(uint32_t t, uint32_t *f) { (void) t; (void) f; return ESP_OK; }
esp_err_t hid_host_install(const hid_host_driver_config_t *c) { (void) c; return ESP_OK; }

/* ==== the HID driver stand-in ==== */

enum mstate { M_NONE, M_READY, M_ACTIVE, M_WAIT, M_REMOVED };
static const char *const mname[] = { "none", "ready", "ACTIVE", "WAIT_USER_DELETION", "removed" };

struct model_iface {
	const char *what;
	int num;
	uint8_t sub_class, proto;
	const uint8_t *desc;
	size_t desc_len;
	int start_fails;
	enum mstate st;
	hid_host_interface_event_cb_t cb;
	void *arg;
	int dev_freed;          /* its device freed by the driver (unplugged) */
	int set_reports;
};

static int s_bad_set_reports;

esp_err_t hid_host_device_open(hid_host_device_handle_t h, const hid_host_device_config_t *c)
{
	h->st = M_READY;
	h->cb = c->callback;
	h->arg = c->callback_arg;
	return ESP_OK;
}

esp_err_t hid_host_device_start(hid_host_device_handle_t h)
{
	if (h->start_fails)
		return ESP_FAIL;
	h->st = M_ACTIVE;
	return ESP_OK;
}

/* hid_host.c 1441-1492 */
esp_err_t hid_host_device_close(hid_host_device_handle_t h)
{
	if (h->st == M_REMOVED || h->st == M_NONE)
		return ESP_ERR_INVALID_ARG;     /* get_iface_by_handle() fails */
	if (h->st == M_ACTIVE)
		h->st = M_READY;                /* hid_host_disable_interface() */
	if (h->cb && h->st != M_WAIT) {
		h->st = M_WAIT;
		h->cb(h, HID_HOST_INTERFACE_EVENT_DISCONNECTED, h->arg);
	} else {
		h->cb = NULL;
		h->st = M_REMOVED;
	}
	return ESP_OK;
}

esp_err_t hid_host_device_get_params(hid_host_device_handle_t h, hid_host_dev_params_t *p)
{
	memset(p, 0, sizeof(*p));
	p->iface_num = h->num;
	p->sub_class = h->sub_class;
	p->proto = h->proto;
	return ESP_OK;
}

esp_err_t hid_host_get_device_info(hid_host_device_handle_t h, hid_host_dev_info_t *i)
{
	memset(i, 0, sizeof(*i));
	i->VID = 0x1234;
	i->PID = 0x5600 + h->num;
	return ESP_OK;
}

esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t h, uint8_t *d,
						     size_t max, size_t *n)
{
	(void) h; (void) d; (void) max;
	*n = 0;
	return ESP_OK;
}

uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t h, size_t *len)
{
	*len = h->desc_len;
	return (uint8_t *) h->desc;
}

esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t h, int p) { (void) h; (void) p; return ESP_OK; }
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t h, int d, int id) { (void) h; (void) d; (void) id; return ESP_OK; }
esp_err_t hid_class_request_set_report(hid_host_device_handle_t h, int t, int id, uint8_t *d, size_t n)
{
	(void) t; (void) id; (void) d; (void) n;
	if (h->st == M_REMOVED || h->dev_freed) {
		printf("    !! SET_REPORT to %s, whose device is freed\n", h->what);
		s_bad_set_reports++;
	}
	h->set_reports++;
	return ESP_OK;
}

/* the driver telling the user a device is gone, then freeing it:
   hid_host.c 536-565 */
static void model_unplug(struct model_iface *i)
{
	hid_host_device_close(i);
	i->dev_freed = 1;
}

/* ==== kbd_input.c and console stand-ins ==== */

static int s_slot_used[KBD_INPUT_DEVS];
int kbd_input_open(void)
{
	for (int i = 0; i < KBD_INPUT_DEVS; i++)
		if (!s_slot_used[i]) {
			s_slot_used[i] = 1;
			return i;
		}
	return -1;
}
void kbd_input_close(int dev) { if (dev >= 0 && dev < KBD_INPUT_DEVS) s_slot_used[dev] = 0; }
void kbd_input_report(int dev, const uint8_t r[8]) { (void) dev; (void) r; }
void kbd_input_mouse(int dx, int dy, int w, int b) { (void) dx; (void) dy; (void) w; (void) b; }
uint8_t kbd_input_leds(void) { return 2; }
void kbd_input_set_leds_fn(kbd_input_leds_fn *fn) { (void) fn; }
void kbd_input_start(void) {}

void console_printf(int level, const char *fmt, ...)
{
	va_list ap;
	(void) level;
	printf("    console: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void console_fatal(const char *fmt, ...)
{
	va_list ap;
	printf("    console_fatal: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	exit(2);
}

/* ==== the runs ==== */

static const uint8_t vendor_desc[] = {
	0x06, 0x00, 0xFF,       /* Usage Page (Vendor 0xFF00) */
	0x09, 0x01,             /* Usage (1) */
	0xA1, 0x01,             /* Collection (Application) */
	0x15, 0x00, 0x26, 0xFF, 0x00,
	0x75, 0x08, 0x95, 0x40, /* 64 bytes */
	0x09, 0x01, 0x81, 0x02, /* Input (Data, Var, Abs) */
	0xC0,
};

static void run_usb_task(void)
{
	if (!setjmp(s_stop))
		usb_task(NULL);
}

static void reset_all(void)
{
	memset(s_ifs, 0, sizeof(s_ifs));
	memset(s_slot_used, 0, sizeof(s_slot_used));
	s_queue = xQueueCreate(16, sizeof(struct usb_event));
	s_bad_set_reports = 0;
}

static int slot_of(struct model_iface *i)
{
	for (int k = 0; k < MAX_IFS; k++)
		if (s_ifs[k].handle == i)
			return k;
	return -1;
}

static int report(const char *title, struct model_iface **ifs, int n, const enum mstate *want)
{
	int bad = 0;
	printf("  after the run:\n");
	for (int k = 0; k < n; k++) {
		int ok = ifs[k]->st == want[k] &&
			((want[k] == M_ACTIVE) == (slot_of(ifs[k]) >= 0));
		printf("    %-28s driver state %-20s slot %2d   %s (want %s)\n", ifs[k]->what,
		       mname[ifs[k]->st], slot_of(ifs[k]), ok ? "ok" : "WRONG", mname[want[k]]);
		bad += !ok;
	}
	if (s_bad_set_reports) {
		printf("    %d SET_REPORT to a freed device\n", s_bad_set_reports);
		bad++;
	}
	printf("  %s: %s\n\n", title, bad ? "FAILS" : "passes");
	return bad;
}

int main(void)
{
	int fails = 0;

	/* the setup path once: task creation and the install semaphore */
	usb_setup();
	if (!s_queue || s_tasks != 2) {
		printf("usb_setup: queue %p, %d tasks started (want 2)\n", (void *) s_queue, s_tasks);
		fails++;
	}

	printf("A. one device: interface 0 vendor-defined (left alone), interface 1 boot keyboard\n");
	{
		reset_all();
		struct model_iface v = { "if0 vendor", 0, 0, 0, vendor_desc, sizeof(vendor_desc) };
		struct model_iface k = { "if1 boot keyboard", 1, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_KEYBOARD };
		driver_callback(&v, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		driver_callback(&k, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		run_usb_task();
		struct model_iface *ifs[] = { &v, &k };
		const enum mstate want[] = { M_REMOVED, M_ACTIVE };
		fails += report("A", ifs, 2, want);
	}

	printf("B. one device: interface 0 boot keyboard, interface 1 vendor-defined\n");
	{
		reset_all();
		struct model_iface k = { "if0 boot keyboard", 0, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_KEYBOARD };
		struct model_iface v = { "if1 vendor", 1, 0, 0, vendor_desc, sizeof(vendor_desc) };
		driver_callback(&k, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		driver_callback(&v, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		run_usb_task();
		struct model_iface *ifs[] = { &k, &v };
		const enum mstate want[] = { M_ACTIVE, M_REMOVED };
		fails += report("B", ifs, 2, want);

		printf("B2. then the keyboard is unplugged\n");
		model_unplug(&k);
		run_usb_task();
		struct model_iface *ifs2[] = { &k };
		const enum mstate want2[] = { M_REMOVED };
		fails += report("B2", ifs2, 1, want2);
	}

	printf("C. keyboard [boot keyboard, vendor] and a boot mouse, the mouse's CONNECTED queued\n"
	       "   before usb_task reaches the keyboard's second interface\n");
	{
		reset_all();
		struct model_iface k = { "kbd if0 boot keyboard", 0, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_KEYBOARD };
		struct model_iface v = { "kbd if1 vendor", 1, 0, 0, vendor_desc, sizeof(vendor_desc) };
		struct model_iface m = { "mouse if0 boot mouse", 0, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_MOUSE };
		driver_callback(&k, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		driver_callback(&v, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		driver_callback(&m, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		run_usb_task();
		struct model_iface *ifs[] = { &k, &v, &m };
		const enum mstate want[] = { M_ACTIVE, M_REMOVED, M_ACTIVE };
		fails += report("C", ifs, 3, want);
	}

	printf("D. the guest changes the lights just before the keyboard is unplugged\n");
	{
		reset_all();
		struct model_iface k = { "if0 boot keyboard", 0, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_KEYBOARD };
		driver_callback(&k, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		run_usb_task();
		int before = k.set_reports;
		leds_changed(2);                /* EV_LEDS queued first */
		model_unplug(&k);               /* EV_GONE behind it, the device freed */
		run_usb_task();
		struct model_iface *ifs[] = { &k };
		const enum mstate want[] = { M_REMOVED };
		fails += report("D", ifs, 1, want);
		printf("  SET_REPORTs: %d when it came, %d after the unplug\n\n", before,
		       k.set_reports - before);
	}

	printf("E. one device: interface 0 a boot mouse that will not start, interface 1 boot keyboard\n");
	{
		reset_all();
		struct model_iface m = { "if0 boot mouse, start fails", 0,
					 HID_SUBCLASS_BOOT_INTERFACE, HID_PROTOCOL_MOUSE };
		m.start_fails = 1;
		struct model_iface k = { "if1 boot keyboard", 1, HID_SUBCLASS_BOOT_INTERFACE,
					 HID_PROTOCOL_KEYBOARD };
		driver_callback(&m, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		driver_callback(&k, HID_HOST_DRIVER_EVENT_CONNECTED, NULL);
		run_usb_task();
		struct model_iface *ifs[] = { &m, &k };
		const enum mstate want[] = { M_REMOVED, M_ACTIVE };
		fails += report("E", ifs, 2, want);
	}

	printf("test_usb_input: %d check%s wrong\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
