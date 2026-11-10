/* Simple WiFi Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include <sys/socket.h>

#include "common.h"
#include "../../i8042.h"

/* v5.2 net: the ESP32-S31 boards (the Korvo-1's network build, the only
   S31 one with Wi-Fi).  They build with ESP-IDF as it is, without
   esp-idf-6.0.patch, so wlanif.c never calls wlanif_l2_input_hook(): the
   station's frames come here first instead (sta_rx()).  They also write
   nothing to flash (the Korvo-1's dual-boot table has Linux's nvs and
   phy_init partitions: sdkconfig.korvo1-wifi turns Wi-Fi's NVS and the PHY's
   stored calibration off), wait for the access point a few seconds at most,
   and say on the boot console how it went.  The other boards are as
   before. */
#if CONFIG_IDF_TARGET_ESP32S31
#define WIFI_S31 1
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "console.h"
/* how long the boot waits for an address before the PC is built */
#define WIFI_WAIT_MS 10000
#else
#define WIFI_S31 0
#endif

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_MAX_STA_CONN       CONFIG_MAX_STA_CONN

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#if WIFI_S31
/* hunting and pecking, and hash to element: an access point that takes
   only one of them (a WPA3 phone hotspot may want H2E) */
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#else
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#endif
#define EXAMPLE_H2E_IDENTIFIER ""

static const char *TAG = "simple wifi";

void (*_Atomic esp32_send_packet)(uint8_t *buf, int size);

#ifndef ESP_IF_WIFI_STA
#define ESP_IF_WIFI_STA WIFI_IF_STA
#endif
static void send_packet(uint8_t *buf, int size)
{
	esp_wifi_internal_tx(ESP_IF_WIFI_STA, buf, size);
}

#if WIFI_S31
static esp_netif_t *s_sta_netif;
static char s_ssid[33];
/* the station is associated, and has lost the access point since (each
   said once on the console, not at every retry) */
static bool s_associated, s_lost;

/* ne2000.c: the guest's share of a frame (ARP and DHCP to both, other IPv4
   to the guest, TCP to port 9999 to the firmware); nonzero when the
   firmware's stack is not to see it */
int wlanif_l2_input_hook(uint8_t *buf, int size);

/* The station's receive callback, in the Wi-Fi task: the guest's frames to
   the NE2000, the rest to lwIP through esp_netif_receive(), which is what
   ESP-IDF registers here itself (wifi_netif.c's wifi_sta_receive()).  The
   NE2000 copies what it takes, so the driver's buffer is freed at once. */
static esp_err_t sta_rx(void *buffer, uint16_t len, void *eb)
{
	if (buffer && wlanif_l2_input_hook(buffer, len)) {
		esp_wifi_internal_free_rx_buffer(eb);
		return ESP_OK;
	}
	return esp_netif_receive(s_sta_netif, buffer, len, eb);
}

/* Registered for WIFI_EVENT_STA_CONNECTED after esp_netif's default
   handlers, so it runs after the one that registers wifi_sta_receive() at
   each connection (handlers for one event run in the order registered),
   and replaces it with sta_rx(). */
static void sta_connected_rx(void *arg, esp_event_base_t event_base,
			     int32_t event_id, void *event_data)
{
	esp_err_t err = esp_wifi_internal_reg_rxcb(WIFI_IF_STA, sta_rx);
	if (err != ESP_OK)
		console_printf(CON_ERROR, "Wi-Fi: the guest's receive path could not "
			       "be set up (%s): the guest will not receive.",
			       esp_err_to_name(err));
}

/* v5.3: lwIP's tcpip task with its stack (3 KiB) in PSRAM rather than
   internal RAM.  The network build wraps sys_thread_new()
   (esp/main/CMakeLists.txt), which tcpip_init() calls for it.  The task
   runs lwIP for the board's own address (ARP, DHCP, the remote
   keyboard's port); the guest's frames do not pass through it.  Nothing
   it runs writes flash, so it never runs with the cache off.  Its TCB
   stays in internal RAM, and without room in PSRAM the task is made as
   before. */
sys_thread_t __real_sys_thread_new(const char *name, lwip_thread_fn thread,
				   void *arg, int stacksize, int prio);
sys_thread_t __wrap_sys_thread_new(const char *name, lwip_thread_fn thread,
				   void *arg, int stacksize, int prio)
{
	TaskHandle_t t;

	if (strcmp(name, TCPIP_THREAD_NAME) == 0 &&
	    xTaskCreatePinnedToCoreWithCaps(thread, name, stacksize, arg, prio, &t,
					    CONFIG_LWIP_TCPIP_TASK_AFFINITY,
					    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
		return t;
	return __real_sys_thread_new(name, thread, arg, stacksize, prio);
}
#endif

static void event_handler(void* arg, esp_event_base_t event_base,
			  int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
#if WIFI_S31
		wifi_event_sta_disconnected_t *d = event_data;
		if (s_associated && !s_lost) {
			console_printf(CON_WARN, "Wi-Fi: lost \"%s\" (reason %d); trying "
				       "again.", s_ssid, d ? d->reason : -1);
			s_lost = true;
		}
		s_associated = false;
#endif
		esp_wifi_connect();
		ESP_LOGI(TAG, "retry to connect to the AP");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
#if WIFI_S31
		/* with the least internal RAM free so far, which by now has
		   Wi-Fi's start and the joining in it */
		unsigned least = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
		if (s_lost) {
			console_printf(CON_GOOD, "Wi-Fi: back on \"%s\", address " IPSTR
				       ", gateway " IPSTR ". Internal RAM least free since "
				       "start %u KiB.", s_ssid, IP2STR(&event->ip_info.ip),
				       IP2STR(&event->ip_info.gw), least);
			s_lost = false;
		} else {
			console_printf(CON_GOOD, "Wi-Fi: address " IPSTR ", netmask " IPSTR
				       ", gateway " IPSTR " (from the access point's DHCP). "
				       "Internal RAM least free since start %u KiB.",
				       IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask),
				       IP2STR(&event->ip_info.gw), least);
		}
#endif
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		atomic_store_explicit(&esp32_send_packet, NULL,
				      memory_order_relaxed);
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
#if WIFI_S31
		s_associated = true;
#endif
		atomic_store_explicit(&esp32_send_packet, send_packet,
				      memory_order_relaxed);
	}
}

#ifdef USE_HOSTED_WIFI
#include "esp_hosted.h"
#endif

/* On the S31 boards a step that fails is said on the console, and the PC
   then starts without a network; elsewhere it stops the firmware, as
   before. */
#if WIFI_S31
#define WIFI_TRY(x) do {						\
		esp_err_t err_ = (x);					\
		if (err_ != ESP_OK) {					\
			console_printf(CON_ERROR, "Wi-Fi could not start: %s "	\
				       "gave %s. The PC starts without a "	\
				       "network.", #x, esp_err_to_name(err_)); \
			return false;					\
		}							\
	} while (0)
#else
#define WIFI_TRY(x) ESP_ERROR_CHECK(x)
#endif

/* true: Wi-Fi is started (connected or still trying) */
bool wifi_init_sta(const char *ssid, const char *pass)
{
	s_wifi_event_group = xEventGroupCreate();

	WIFI_TRY(esp_netif_init());

#ifdef USE_HOSTED_WIFI
	ESP_LOGI(TAG, "init ESP-Hosted");
	esp_hosted_init();
	esp_hosted_connect_to_slave();
#endif

	WIFI_TRY(esp_event_loop_create_default());
#if WIFI_S31
	s_sta_netif = esp_netif_create_default_wifi_sta();
	if (!s_sta_netif) {
		console_puts(CON_ERROR, "Wi-Fi could not start: no memory for its "
			     "network interface. The PC starts without a network.");
		return false;
	}
#else
	esp_netif_create_default_wifi_sta();
#endif

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	WIFI_TRY(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	WIFI_TRY(esp_event_handler_instance_register(WIFI_EVENT,
						     ESP_EVENT_ANY_ID,
						     &event_handler,
						     NULL,
						     &instance_any_id));
	WIFI_TRY(esp_event_handler_instance_register(IP_EVENT,
						     IP_EVENT_STA_GOT_IP,
						     &event_handler,
						     NULL,
						     &instance_got_ip));
#if WIFI_S31
	esp_event_handler_instance_t instance_rx;
	WIFI_TRY(esp_event_handler_instance_register(WIFI_EVENT,
						     WIFI_EVENT_STA_CONNECTED,
						     &sta_connected_rx,
						     NULL,
						     &instance_rx));
#endif

	wifi_config_t wifi_config = {
		.sta = {
			/* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
			 * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
			 * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
			 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
			 */
			.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
			.sae_pwe_h2e = ESP_WIFI_SAE_MODE,
			.sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
		},
	};
	if (strlen(ssid) < 32 && strlen(pass) < 64) {
		strcpy((char *) wifi_config.sta.ssid, ssid);
		strcpy((char *) wifi_config.sta.password, pass);
	} else {
		ESP_LOGE(TAG, "ssid or password is too long");
		return false;
	}
	/* no password: an open access point (the threshold above would
	   refuse one, and no other could be joined without a password) */
	if (!pass[0])
		wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
	WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_STA) );
	WIFI_TRY(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
#if WIFI_S31
	/* the radio stays awake: without this the station sleeps between the
	   access point's beacons, and every frame to the guest can wait a
	   beacon interval or more */
	esp_wifi_set_ps(WIFI_PS_NONE);
	strcpy(s_ssid, ssid);
	console_printf(CON_INFO, "Wi-Fi: joining \"%s\" (2.4 GHz). Waiting up to "
		       "%d s for an address.", ssid, WIFI_WAIT_MS / 1000);
#endif
	WIFI_TRY(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
					       WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
					       pdFALSE,
					       pdFALSE,
#if WIFI_S31
					       pdMS_TO_TICKS(WIFI_WAIT_MS)
#else
					       portMAX_DELAY
#endif
					       );

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s", ssid);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s", ssid);
	} else {
#if WIFI_S31
		console_printf(CON_WARN, "Wi-Fi: no address from \"%s\" in %d s (out of "
			       "range, a 5 GHz-only hotspot, or the wrong password?). The "
			       "PC starts anyway, and Wi-Fi keeps trying.",
			       ssid, WIFI_WAIT_MS / 1000);
#else
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
#endif
	}
#if WIFI_S31
	uint8_t mac[6];
	if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
		console_printf(CON_INFO, "Wi-Fi: the guest's NE2000 (I/O 300h, IRQ 9) "
			       "has this board's MAC, %02x:%02x:%02x:%02x:%02x:%02x, "
			       "and gets the same address from DHCP.",
			       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
	return true;
}

static void __attribute__((noreturn)) task_fatal_error()
{
	ESP_LOGE(TAG, "Exiting task due to fatal error...");
	(void)vTaskDelete(NULL);

	while (1) {
		;
	}
}

static void server_thread(void *pvParameter)
{
	struct sockaddr_in sock_info;

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		ESP_LOGE(TAG, "Create socket failed!");
		/* a FreeRTOS task must not return */
		task_fatal_error();
	}

	memset(&sock_info, 0, sizeof(struct sockaddr_in));
	sock_info.sin_family = AF_INET;
	sock_info.sin_addr.s_addr = htonl(INADDR_ANY);
	sock_info.sin_port = htons(9999);

	if(bind(sfd, (struct sockaddr *)&sock_info, sizeof(sock_info)) < 0)
		task_fatal_error();

	if(listen(sfd, 10) < 0)
		task_fatal_error();

	for (;;) {
		static int state;
		static int8_t mouse[4];

		int socket_id = accept(sfd, NULL, 0);
		state = 0;
		xEventGroupWaitBits(global_event_group,
				    BIT0,
				    pdFALSE,
				    pdFALSE,
				    portMAX_DELAY);

		bool flag = true;
		char text[16];
		while (flag) {
			int buff_len = recv(socket_id, text, 16, 0);
			if (buff_len < 0) { /*receive error*/
				ESP_LOGE(TAG, "Error: receive data error! errno=%d", errno);
				flag = false;
				close(socket_id);
			} else if (buff_len > 0) { /*deal with response body*/
				for (int i = 0; i < buff_len; i++) {
					if (state == 0) {
						if (text[i] == 0) {
							state = 4;
						} else {
							int is_down = !(text[i] >> 7);
							int keycode = text[i] & 0x7f;
							ps2_put_keycode(globals.kbd, is_down, keycode);
							vTaskDelay(5 / portTICK_PERIOD_MS);
						}
					} else {
						mouse[4 - state] = text[i];
						if (state == 1) {
							state = 0;
							ps2_mouse_event(globals.mouse,
									mouse[0], mouse[1], mouse[2], mouse[3]);
							vTaskDelay(5 / portTICK_PERIOD_MS);
						} else {
							state--;
						}
					}
				}
			} else if (buff_len == 0) {  /*packet over*/
				ESP_LOGI(TAG, "Connection closed, all packets received");
				flag = false;
				close(socket_id);
			}
		}
	}
}

void wifi_main(const char *ssid, const char *pass)
{
	/* without Wi-Fi's NVS (the S31 boards) nothing here reads or writes
	   flash */
#if CONFIG_ESP_WIFI_NVS_ENABLED
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
#endif
	if (!wifi_init_sta(ssid, pass))
		return;
#if WIFI_S31
	/* the remote keyboard's task with its stack in PSRAM, so that it takes
	   4 KiB less internal RAM: it never touches flash, so it never runs
	   with the cache off */
	if (xTaskCreatePinnedToCoreWithCaps(server_thread, "wifi", 4096, NULL, 1,
					    NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS)
		return;
#endif
	xTaskCreatePinnedToCore(server_thread, "wifi", 4096, NULL, 1, NULL, 0);
}
