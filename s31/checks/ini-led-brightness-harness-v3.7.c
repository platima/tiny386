/* esp_main.c's set_led_brightness() as v3.7 has it (fn.c, cut from the file
   by inicheck.sh), with stand-ins for the console and the LED: each value
   an ini line could carry, what level the LED is given and what is said. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CON_INFO, CON_WARN };
static const char *s_ini_name = "tiny386.ini";
static int last_level, warns, infos;

#ifdef WITH_LED
#define STATUS_LED_GPIO 37
#define STATUS_BRIGHTNESS_MAX 255
static void status_led_brightness(int level)
{
	last_level = level;
}
#endif

static void console_printf(int kind, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("      %s ", kind == CON_WARN ? "yellow:" : "info:  ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	if (kind == CON_WARN)
		warns++;
	else
		infos++;
}

static void console_puts(int kind, const char *s)
{
	console_printf(kind, "%s", s);
}

#include "fn.c"

int main(void)
{
	static const struct { const char *value; int level, warn; } cases[] = {
		{ "9", 9, 0 }, { "20", 20, 0 }, { "0", 0, 0 }, { "1", 1, 0 }, { "70", 70, 0 },
		{ "255", 255, 0 }, { "+20", 20, 0 }, { "020", 20, 0 }, { "256", 255, 1 },
		{ "1000", 255, 1 }, { "99999999999999999999", 255, 1 }, { "-1", 0, 1 },
		{ "-99999999999999999999", 0, 1 }, { "abc", -1, 1 }, { "", -1, 1 },
		{ "20abc", -1, 1 }, { "0x10", -1, 1 }, { "12.5", -1, 1 }, { "20 ", 20, 0 },
	};
	int bad = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		last_level = -1;
		warns = infos = 0;
		printf("  led_brightness = \"%s\":\n", cases[i].value);
		set_led_brightness(cases[i].value);
#ifdef WITH_LED
		int ok = last_level == cases[i].level && warns == cases[i].warn;
		printf("    the LED given %d%s, %d yellow line%s: %s\n", last_level,
		       last_level < 0 ? " (not called)" : "", warns, warns == 1 ? "" : "s",
		       ok ? "as expected" : "NOT AS EXPECTED");
#else
		int ok = warns == 0 && infos == 1;
		printf("    %d yellow, %d info: %s\n", warns, infos, ok ? "as expected" : "NOT AS EXPECTED");
#endif
		bad += !ok;
	}
	printf("%d cases, %d not as expected\n", (int) (sizeof(cases) / sizeof(cases[0])), bad);
	return bad != 0;
}
