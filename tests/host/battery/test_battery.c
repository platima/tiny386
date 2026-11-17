/*
 * The ESP-Mosaico's battery readings (esp/main/battery_mosaico.c) against a
 * fake BQ27220: the information port's four values, their signs, and
 * PP_BATTERY_UNKNOWN before a reading, for what does not apply, and after
 * a read that failed.  ESP-IDF's and the gauge driver's headers are
 * stand-ins (stub/); run by run_battery.sh.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint16_t g_reg[256];
static int g_fail_cmd = -1;
static int g_reads;

#include "battery_mosaico.c"

void console_puts(int level, const char *str)
{
	printf("console %d: %s\n", level, str);
}

void console_printf(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("console %d: ", level);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn,
				      uint8_t *r, size_t rn, int ms)
{
	(void) d; (void) wn; (void) ms;
	g_reads++;
	if (*w == g_fail_cmd)
		return ESP_FAIL;
	if (rn != 2)
		return ESP_FAIL;
	r[0] = g_reg[*w] & 0xFF;
	r[1] = g_reg[*w] >> 8;
	return ESP_OK;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
	else printf("  ok: " __VA_ARGS__), printf("\n"); } while (0)

static void fill(uint32_t *v, uint32_t *ok)
{
	memset(v, 0, 4 * PP_COUNT);
	memset(ok, 0, 4 * PP_VALID_WORDS);
	board_perfport_fill(v, ok);
}

int main(void)
{
	uint32_t v[PP_COUNT], ok[PP_VALID_WORDS];

	/* before any reading */
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_PCT] == PP_BATTERY_UNKNOWN && v[PP_I_BATTERY_MV] == PP_BATTERY_UNKNOWN &&
	      v[PP_I_BATTERY_MA] == PP_BATTERY_UNKNOWN && v[PP_I_BATTERY_MIN] == PP_BATTERY_UNKNOWN,
	      "before the first reading, all four unknown");
	CHECK(((ok[PP_I_BATTERY_PCT >> 5] >> (PP_I_BATTERY_PCT & 31)) & 15) == 15,
	      "and their valid bits set (the build has a gauge)");

	/* discharging: 300 mA out, 25 min to empty */
	g_reg[CMD_SOC] = 83;
	g_reg[CMD_VOLTAGE] = 3951;
	g_reg[CMD_CURRENT] = (uint16_t) -300;
	g_reg[CMD_TTE] = 25;
	g_reg[CMD_TTF] = 65535;
	CHECK(read_all(), "a reading while discharging");
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_PCT] == 83, "per cent 83 (%lu)", (unsigned long) v[PP_I_BATTERY_PCT]);
	CHECK(v[PP_I_BATTERY_MV] == 3951, "3951 mV (%lu)", (unsigned long) v[PP_I_BATTERY_MV]);
	CHECK((int32_t) v[PP_I_BATTERY_MA] == -300, "-300 mA (%ld)", (long) (int32_t) v[PP_I_BATTERY_MA]);
	CHECK(v[PP_I_BATTERY_MA] == 0xFFFFFED4u, "as 32 bits FFFFFED4h (%08lx)", (unsigned long) v[PP_I_BATTERY_MA]);
	CHECK(v[PP_I_BATTERY_MIN] == 25, "25 minutes to empty (%lu)", (unsigned long) v[PP_I_BATTERY_MIN]);

	/* charging: 120 mA in, 38 min to full */
	g_reg[CMD_CURRENT] = 120;
	g_reg[CMD_TTE] = 65535;
	g_reg[CMD_TTF] = 38;
	CHECK(read_all(), "a reading while charging");
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_MA] == 120 && v[PP_I_BATTERY_MIN] == 38, "120 mA, 38 minutes to full");

	/* the gauge's 65535 where it does not apply, and no current */
	g_reg[CMD_TTF] = 65535;
	read_all();
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_MIN] == PP_BATTERY_UNKNOWN, "65535 minutes to full: unknown");
	g_reg[CMD_CURRENT] = 0;
	g_reg[CMD_TTE] = 12;
	read_all();
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_MA] == 0 && v[PP_I_BATTERY_MIN] == PP_BATTERY_UNKNOWN,
	      "0 mA: minutes unknown (neither way)");
	g_reg[CMD_CURRENT] = (uint16_t) -1;
	read_all();
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_MA] == 0xFFFFFFFFu && v[PP_I_BATTERY_MA] != PP_BATTERY_UNKNOWN,
	      "-1 mA is FFFFFFFFh, not the unknown mark");
	g_reg[CMD_CURRENT] = 0x8000;
	read_all();
	fill(v, ok);
	CHECK((int32_t) v[PP_I_BATTERY_MA] == -32768 && v[PP_I_BATTERY_MA] != PP_BATTERY_UNKNOWN,
	      "-32768 mA, the gauge's lowest, is not the unknown mark");

	/* a state of charge out of range */
	g_reg[CMD_SOC] = 101;
	read_all();
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_PCT] == PP_BATTERY_UNKNOWN && v[PP_I_BATTERY_MV] == 3951,
	      "101 per cent: unknown, the rest kept");

	/* a read that fails: everything unknown, until it answers again */
	g_reg[CMD_SOC] = 50;
	g_fail_cmd = CMD_CURRENT;
	CHECK(!read_all(), "a failed read is a failed reading");
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_PCT] == PP_BATTERY_UNKNOWN && v[PP_I_BATTERY_MV] == PP_BATTERY_UNKNOWN &&
	      v[PP_I_BATTERY_MA] == PP_BATTERY_UNKNOWN && v[PP_I_BATTERY_MIN] == PP_BATTERY_UNKNOWN,
	      "after it, all four unknown");
	g_fail_cmd = -1;
	CHECK(read_all(), "and it answers again");
	fill(v, ok);
	CHECK(v[PP_I_BATTERY_PCT] == 50, "per cent 50 again");

	/* only the four values, and their bits */
	fill(v, ok);
	int others = 0;
	for (int i = 0; i < PP_COUNT; i++)
		if ((i < PP_I_BATTERY_PCT || i > PP_I_BATTERY_MIN) &&
		    ((ok[i >> 5] >> (i & 31)) & 1))
			others++;
	CHECK(others == 0, "no other index touched (%d)", others);

	printf("test_battery: %d failed\n", fails);
	return fails != 0;
}
