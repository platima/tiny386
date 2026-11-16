#ifdef USE_LCD_QSPI_MOSAICO
/*
 * The ESP-Mosaico's own start-up (esp_main.c's board hooks):
 *   board_early_init(), before the panel: its supply on (VCC_3V3_CTRL low,
 *   which also feeds the headers' 3.3 V and 5 V), the buttons as inputs;
 *   board_console_ready(), with the console up: the board's I2C bus (touch
 *   and codec), USB disk mode if it is wanted (usb_disk.c), the view key
 *   and button, the touch panel as the mouse.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "console.h"
#include "kbd_input.h"
#include "lcd_mosaico.h"
#include "touch_mosaico.h"
#include "usb_disk.h"

#define HID_KEY_F7 0x40

static i2c_master_bus_handle_t s_i2c;

void board_early_init(void)
{
	/* ESP-IDF's own drivers at warnings and errors only: their info lines
	   (Wi-Fi's above all) go out on the UART at 115200 as well as to the
	   boot console, and slow the start.  tiny386's own lines are not
	   ESP-IDF's log and stay. */
	esp_log_level_set("*", ESP_LOG_WARN);
	/* the Wi-Fi driver logs its transmit power for every rate as a
	   warning, "(phy)rate:...", about 30 lines 150 ms apart, which also
	   set off the boot console's 10 s hold: its errors only */
	esp_log_level_set("wifi", ESP_LOG_ERROR);
	const gpio_config_t pwr = {
		.pin_bit_mask = 1ULL << MOSAICO_PWR_GPIO,
		.mode = GPIO_MODE_OUTPUT,
	};
	gpio_set_level(MOSAICO_PWR_GPIO, 0);
	gpio_config(&pwr);
	gpio_set_level(MOSAICO_PWR_GPIO, 0);
	/* the power-off request stays an input until it is wanted */
	const gpio_config_t in = {
		.pin_bit_mask = (1ULL << MOSAICO_AI_BUTTON) | (1ULL << MOSAICO_BOOT_BUTTON),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};
	gpio_config(&in);
	/* the panel's supply settles before lcd_init() resets it */
	vTaskDelay(pdMS_TO_TICKS(20));
}

/* the AI button while the PC runs: a short press changes the view */
static void button_task(void *arg)
{
	(void) arg;
	int held = 0;
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(30));
		if (gpio_get_level(MOSAICO_AI_BUTTON) == 0) {
			held++;
			continue;
		}
		if (held >= 2 && held < 30)
			lcd_mosaico_next_view();
		held = 0;
	}
}

void board_console_ready(void)
{
	const i2c_master_bus_config_t bus = {
		.i2c_port = MOSAICO_I2C_PORT,
		.sda_io_num = MOSAICO_I2C_SDA,
		.scl_io_num = MOSAICO_I2C_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	esp_err_t err = i2c_new_master_bus(&bus, &s_i2c);
	if (err != ESP_OK) {
		console_printf(CON_WARN, "The board's I2C bus would not start (%s): no "
			       "touch and no sound.", esp_err_to_name(err));
		s_i2c = NULL;
	}

	if (usb_disk_wanted())
		usb_disk_mode();

	kbd_input_set_hotkey(HID_KEY_F7, lcd_mosaico_next_view);
	if (xTaskCreatePinnedToCore(button_task, "ai_button", 2048, NULL, 2, NULL, 0) != pdPASS)
		console_puts(CON_WARN, "No memory for the AI button's task.");
	if (s_i2c)
		touch_mosaico_start(s_i2c);
	console_puts(CON_INFO, "Ctrl+Alt+F7 or the AI button: the whole picture at 3/4, "
		     "or 480 of its 640 columns following the mouse. Windows at 480x480 "
		     "fills the panel.");
}

#endif /* USE_LCD_QSPI_MOSAICO */
