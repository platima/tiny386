/* Host stand-in for ESP-IDF's driver/gpio.h, for board_check_test.c
   (shim/README): the types and calls dosboard.c uses, with the same field
   names and the same meaning for the mode bits. */
#ifndef BC_SHIM_GPIO_H
#define BC_SHIM_GPIO_H
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_MODE_DEF_INPUT  (1 << 0)
#define GPIO_MODE_DEF_OUTPUT (1 << 1)
#define GPIO_MODE_DEF_OD     (1 << 2)
typedef enum {
	GPIO_MODE_DISABLE = 0,
	GPIO_MODE_INPUT = GPIO_MODE_DEF_INPUT,
	GPIO_MODE_OUTPUT = GPIO_MODE_DEF_OUTPUT,
	GPIO_MODE_OUTPUT_OD = GPIO_MODE_DEF_OUTPUT | GPIO_MODE_DEF_OD,
	GPIO_MODE_INPUT_OUTPUT_OD = GPIO_MODE_DEF_INPUT | GPIO_MODE_DEF_OUTPUT | GPIO_MODE_DEF_OD,
	GPIO_MODE_INPUT_OUTPUT = GPIO_MODE_DEF_INPUT | GPIO_MODE_DEF_OUTPUT,
} gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE = 0 } gpio_int_type_t;
typedef struct {
	uint64_t pin_bit_mask;
	gpio_mode_t mode;
	gpio_pullup_t pull_up_en;
	gpio_pulldown_t pull_down_en;
	gpio_int_type_t intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_reset_pin(gpio_num_t pin);
#endif
