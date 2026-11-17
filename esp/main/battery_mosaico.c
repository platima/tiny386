#ifdef USE_LCD_QSPI_MOSAICO
/*
 * The ESP-Mosaico's battery: a BQ27220 fuel gauge at 55h on the board's I2C
 * bus (the touch panel's and the codec's), set up as Espressif's BSP does
 * (onboard/battery.c).  Espressif's bq27220 driver checks the profile the
 * gauge holds against the BSP's interim one for the board's cell (fixed
 * EDV, 80 mAh, characterised on two cells at 25 C) and writes it only if
 * the two differ, as they do after the cell has been flat.
 *
 * That and the readings are on a task of their own on core 0: every 2 s
 * the state of charge, voltage, current and time left, kept for the
 * information port.  Its snapshot runs inside the guest's OUT on the
 * emulator's core (speed.c), so it only copies them: no I2C there.
 */
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "bq27220.h"
#include "i2c_bus.h"

#include "../../perfport.h"
#include "console.h"
#include "battery_mosaico.h"

#define GAUGE_ADDR      0x55
#define GAUGE_HZ        400000          /* the BSP's */
#define READ_MS         2000            /* the gauge updates about once a second */

/* its standard commands, two bytes each, low byte first */
#define CMD_VOLTAGE     0x08            /* mV */
#define CMD_CURRENT     0x0C            /* mA, signed: below 0 discharging */
#define CMD_TTE         0x16            /* minutes to empty; 65535 when not discharging */
#define CMD_TTF         0x18            /* minutes to full; 65535 when not charging */
#define CMD_SOC         0x2C            /* state of charge, per cent */

/* Espressif's ESP-Mosaico BSP, onboard/battery.c: "Interim fixed-EDV
   profile characterized on two 80 mAh cells at 25 C." */
static const parameter_cedv_t s_cedv = {
	.full_charge_cap = 80,
	.design_cap = 80,
	.reserve_cap = 0,
	.near_full = 5,
	.self_discharge_rate = 20,
	.EDV0 = 3000,
	.EDV1 = 3410,
	.EDV2 = 3530,
	.EMF = 3670,
	.C0 = 115,
	.R0 = 968,
	.T0 = 4547,
	.R1 = 4764,
	.TC = 11,
	.C1 = 0,
	.DOD0 = 4147,
	.DOD10 = 4002,
	.DOD20 = 3969,
	.DOD30 = 3938,
	.DOD40 = 3880,
	.DOD50 = 3824,
	.DOD60 = 3794,
	.DOD70 = 3753,
	.DOD80 = 3677,
	.DOD90 = 3574,
	.DOD100 = 3490,
};

static const gauging_config_t s_gauging = {
	.CCT = true,
	.SC = true,
	.FCC_LIM = true,
	.FC_FOR_VDQ = true,
	.IGNORE_SD = true,
};

static i2c_master_dev_handle_t s_dev;
/* as last read, PP_BATTERY_UNKNOWN until then (each word stored whole) */
static volatile uint32_t s_pct = PP_BATTERY_UNKNOWN, s_mv = PP_BATTERY_UNKNOWN;
static volatile uint32_t s_ma = PP_BATTERY_UNKNOWN, s_min = PP_BATTERY_UNKNOWN;

static esp_err_t read16(uint8_t cmd, uint16_t *v)
{
	uint8_t b[2];
	esp_err_t err = i2c_master_transmit_receive(s_dev, &cmd, 1, b, 2, 50);
	if (err == ESP_OK)
		*v = (uint16_t) (b[0] | b[1] << 8);
	return err;
}

/* The profile, as the BSP has it; then a device of our own on the bus for
   the readings, since Espressif's driver logs a failed read as an error
   and gives 0 for it.  The i2c_bus wrapper borrows the bus mosaico.c made,
   and is kept: i2c_bus_delete() would take that bus down. */
static bool gauge_setup(i2c_master_bus_handle_t bus)
{
	const i2c_config_t cfg = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = MOSAICO_I2C_SDA,
		.scl_io_num = MOSAICO_I2C_SCL,
		.sda_pullup_en = true,
		.scl_pullup_en = true,
		.master.clk_speed = GAUGE_HZ,
	};
	i2c_bus_handle_t ib = i2c_bus_create(MOSAICO_I2C_PORT, &cfg);
	if (!ib) {
		console_puts(CON_WARN, "The battery gauge could not be given the I2C bus: no battery level.");
		return false;
	}
	const bq27220_config_t gc = {
		.i2c_bus = ib,
		.cfg = &s_gauging,
		.cedv = &s_cedv,
	};
	bq27220_handle_t g = bq27220_create(&gc);
	if (!g) {
		console_puts(CON_WARN, "The battery gauge (BQ27220) did not answer, or its profile "
			     "could not be checked: no battery level.");
		return false;
	}
	/* as the BSP does after its own create */
	bq27220_seal(g);
	bq27220_delete(g);
	const i2c_device_config_t dc = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = GAUGE_ADDR,
		.scl_speed_hz = GAUGE_HZ,
	};
	if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) {
		console_puts(CON_WARN, "No room on the I2C bus for the battery gauge: no battery level.");
		return false;
	}
	return true;
}

static bool read_all(void)
{
	uint16_t soc, mv, cur, tte, ttf;
	if (read16(CMD_SOC, &soc) != ESP_OK || read16(CMD_VOLTAGE, &mv) != ESP_OK ||
	    read16(CMD_CURRENT, &cur) != ESP_OK || read16(CMD_TTE, &tte) != ESP_OK ||
	    read16(CMD_TTF, &ttf) != ESP_OK) {
		s_pct = s_mv = s_ma = s_min = PP_BATTERY_UNKNOWN;
		return false;
	}
	int16_t ma = (int16_t) cur;
	uint16_t left = ma < 0 ? tte : ma > 0 ? ttf : 0xFFFF;
	s_pct = soc <= 100 ? soc : PP_BATTERY_UNKNOWN;
	s_mv = mv;
	s_ma = (uint32_t) (int32_t) ma;
	s_min = left == 0xFFFF ? PP_BATTERY_UNKNOWN : left;
	return true;
}

static void battery_task(void *arg)
{
	if (gauge_setup((i2c_master_bus_handle_t) arg)) {
		bool said = false;
		unsigned fails = 0;
		for (;;) {
			if (read_all()) {
				if (!said && s_pct != PP_BATTERY_UNKNOWN) {
					int32_t ma = (int32_t) s_ma;
					console_printf(CON_INFO, "Battery: %u%%, %u.%02u V, %d mA (%s).",
						       (unsigned) s_pct, (unsigned) s_mv / 1000,
						       (unsigned) s_mv % 1000 / 10, (int) ma,
						       ma > 0 ? "charging" : ma < 0 ? "discharging" : "neither");
					said = true;
				}
				fails = 0;
			} else if (++fails == 5) {
				console_puts(CON_WARN, "The battery gauge stopped answering: the battery "
					     "level is unknown until it answers again.");
			}
			vTaskDelay(pdMS_TO_TICKS(READ_MS));
		}
	}
	vTaskDelete(NULL);
}

void battery_mosaico_start(i2c_master_bus_handle_t bus)
{
	if (xTaskCreatePinnedToCore(battery_task, "battery", 4096, bus, 1, NULL, 0) != pdPASS)
		console_puts(CON_WARN, "No memory for the battery gauge's task: no battery level.");
}

void board_perfport_fill(uint32_t *val, uint32_t *valid)
{
#ifdef TINY386_PERFPORT
	PERFPORT_PUT(val, valid, PP_I_BATTERY_PCT, s_pct);
	PERFPORT_PUT(val, valid, PP_I_BATTERY_MV, s_mv);
	PERFPORT_PUT(val, valid, PP_I_BATTERY_MA, s_ma);
	PERFPORT_PUT(val, valid, PP_I_BATTERY_MIN, s_min);
#else
	(void) val;
	(void) valid;
#endif
}

#endif /* USE_LCD_QSPI_MOSAICO */
