/*
 * Host test for the boot console (esp/main/console.c, built with
 * CONSOLE_HOST): draws into an 800x480 RGB565 frame buffer in memory, as on
 * the Korvo-1, and
 *
 *  - reads the text back off the pixels (each 8x16 cell matched against
 *    the VGA font) and checks it against what was printed: the title row,
 *    lines, colours, wrapping, scrolling;
 *  - checks the colour card along the bottom;
 *  - checks that every row drawn was handed to the flush function;
 *  - checks the ring: the lines of a "previous run", the tail, the crash
 *    flag;
 *  - draws the crash screen and reads it back;
 *  - the UART quiet while the PC runs (v3.8, [esp] uart_log = 0);
 *  - lines that repeat (v3.8, [esp] verbose_log): of a kind, the first 5
 *    in 5 s out and the rest counted in one line, kinds apart, more kinds
 *    than places, console_printf() never held back, the limit turned off;
 *  - the UART's queue (v3.8, [esp] uart_log = 1): a line written out as the
 *    FIFO takes it, "\r\n" at its end, lines that do not fit dropped from
 *    the UART only and counted, the queue written out when it is turned
 *    off;
 *  - (v3.9) held error lines counted at their own level, and the lines
 *    still held when a crash comes said into the ring before the crash
 *    screen reads its last lines;
 *  - writes PPM pictures of each stage to OUT for a person to look at.
 *
 *   test_console OUT
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"

extern const uint8_t vgafont16[256 * 16];
void console_host_ring_preset(const char *text, bool crashed);
void console_host_set_uart(FILE *f);
void console_host_tee_line(const char *s);
void console_host_log_line(const char *s);
void console_host_set_ms(uint32_t ms);
uint32_t console_host_uart_drain(uint32_t room);

#define W 800
#define H 480
#define COLS (W / 8)

static uint16_t fb[W * H];
static int failures;
static const char *out_dir = ".";

static void check(int ok, const char *what)
{
	if (!ok) {
		failures++;
		printf("  FAIL: %s\n", what);
	}
}

/* ==== the flush function: which rows were handed over ==== */

static unsigned char flushed[H];
static int flush_calls;

static void host_flush(int x, int y, int w, int h)
{
	(void) x; (void) w;
	flush_calls++;
	for (int i = y; i < y + h && i < H; i++)
		if (i >= 0)
			flushed[i] = 1;
}

/* a copy of the frame buffer as the panel would show it: only rows that
   were flushed since the last look are taken over */
static uint16_t panel[W * H];

static void panel_take(void)
{
	for (int y = 0; y < H; y++)
		if (flushed[y]) {
			memcpy(panel + y * W, fb + y * W, W * 2);
			flushed[y] = 0;
		}
}

/* ==== pictures ==== */

static void write_ppm(const char *name, const uint16_t *p)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", out_dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		failures++;
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; i++) {
		unsigned r = (p[i] >> 11) & 31, g = (p[i] >> 5) & 63, b = p[i] & 31;
		unsigned char px[3] = { r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2 };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
	printf("  wrote %s\n", path);
}

/* ==== reading text back off the pixels ==== */

/* the character in the cell at (col, row), or -1 if no glyph matches;
   *fg gets its foreground colour (the colour that is not bg) */
static int cell_char(const uint16_t *p, int col, int row, uint16_t bg, uint16_t *fg)
{
	uint8_t bits[16];
	uint16_t f = bg;
	for (int l = 0; l < 16; l++) {
		bits[l] = 0;
		for (int b = 0; b < 8; b++) {
			uint16_t px = p[(row * 16 + l) * W + col * 8 + b];
			if (px != bg) {
				bits[l] |= 0x80 >> b;
				f = px;
			}
		}
	}
	if (fg)
		*fg = f;
	for (int c = 0x20; c < 0x7f; c++)
		if (!memcmp(bits, vgafont16 + c * 16, 16))
			return c;
	for (int c = 0; c < 256; c++)
		if (!memcmp(bits, vgafont16 + c * 16, 16))
			return c;
	return -1;
}

/* the row's text, trailing spaces taken off */
static void row_text(const uint16_t *p, int row, uint16_t bg, char *out)
{
	int n = 0;
	for (int c = 0; c < COLS; c++) {
		int ch = cell_char(p, c, row, bg, NULL);
		out[n++] = ch < 0 ? '#' : (ch < 0x20 || ch >= 0x7f) ? '~' : ch;
	}
	while (n > 0 && out[n - 1] == ' ')
		n--;
	out[n] = 0;
}

static uint16_t rgb(int r, int g, int b)
{
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void expect_row(const uint16_t *p, int row, uint16_t bg, const char *want)
{
	char got[COLS + 1];
	row_text(p, row, bg, got);
	if (strcmp(got, want)) {
		failures++;
		printf("  FAIL: row %d reads \"%s\", expected \"%s\"\n", row, got, want);
	}
}

static void expect_colour(const uint16_t *p, int col, int row, uint16_t want, const char *what)
{
	uint16_t fg;
	cell_char(p, col, row, 0, &fg);
	if (fg != want) {
		failures++;
		printf("  FAIL: %s: colour %04x, expected %04x\n", what, fg, want);
	}
}

/* ==== the ring after a mark line (part 7) ==== */

static char ring_text[8192];

/* the ring's text after the last line that is mark, or NULL */
static const char *after(const char *mark)
{
	console_ring_tail(ring_text, sizeof(ring_text), 400);
	const char *p = NULL;
	for (const char *q = ring_text; (q = strstr(q, mark)); q++)
		p = q;
	return p ? strchr(p, '\n') : NULL;
}

/* lines after the mark that begin with prefix; -1 if no mark */
static int lines_after(const char *mark, const char *prefix)
{
	const char *p = after(mark);
	if (!p)
		return -1;
	int n = 0;
	for (; p && p[1]; p = strchr(p + 1, '\n'))
		n += !strncmp(p + 1, prefix, strlen(prefix));
	return n;
}

/* a whole line after the mark */
static bool line_after(const char *mark, const char *want)
{
	const char *p = after(mark);
	size_t l = strlen(want);
	for (; p && p[1]; p = strchr(p + 1, '\n'))
		if (!strncmp(p + 1, want, l) && (p[1 + l] == '\n' || !p[1 + l]))
			return true;
	return false;
}

int main(int argc, char **argv)
{
	char line[256];
	if (argc > 1)
		out_dir = argv[1];
	/* what the firmware would send to the UART */
	snprintf(line, sizeof(line), "%s/console-uart.log", out_dir);
	FILE *uart = fopen(line, "w");
	console_host_set_uart(uart);

	/* ==== 1. a previous run that ended on the crash screen ==== */
	printf("previous run and start-up\n");
	console_host_ring_preset(
		"I (100) storage: Filesystem mounted\n"
		"SeaBIOS (version tiny386)\n"
		"Booting from Hard Disk...\n"
		"CRASH (the crash screen was shown):\n"
		"What happened: Load access fault\n", true);
	console_init(fb, W, H, host_flush);
	check(console_previous_lines() == 5, "five lines kept from the last run");
	check(console_previous_crashed(), "the last run's crash flag");
	char tail[512];
	check(console_ring_tail(tail, sizeof(tail), 10) == 0,
	      "the ring starts again empty");

	console_set_title(" tiny386 on the ESP32-S31-Korvo-1   v3 host test   ESP-IDF v6.1");
	console_printf(CON_INFO, "tiny386 for the ESP32-S31-Korvo-1, version v3, ESP-IDF v6.1");
	console_printf(CON_WARN, "Reset by %s.", "a crash (panic)");
	console_show_previous("the crash screen");
	console_puts(CON_GOOD, "Card mounted at /sdcard.");
	console_puts(CON_INFO, "Settings from /sdcard/tiny386.ini:\n  [pc] mem_size = 4M\n"
		     "  [cpu] gen = 4");
	const char *err = "[pc] hda = /sdcard/freedos.img: there is no such file on "
		"the card. Check the name (and the folder) in tiny386.ini.";
	console_puts(CON_ERROR, err);
	console_puts(CON_INFO, "tabs:\tone\ttwo; control \x01 and \x1b[31mcolour\x1b[0m codes");
	char word[151];
	memset(word, 'x', 150);
	word[150] = 0;
	console_puts(CON_INFO, word);           /* no space to break at */
	panel_take();
	write_ppm("console-1-boot.ppm", panel);

	uint16_t blue = rgb(0, 0, 170);
	expect_row(panel, 0, blue, " tiny386 on the ESP32-S31-Korvo-1   v3 host test   ESP-IDF v6.1");
	check(panel[5 * W + 790] == blue, "the title bar is blue to the right edge");
	expect_row(panel, 1, 0, "tiny386 for the ESP32-S31-Korvo-1, version v3, ESP-IDF v6.1");
	expect_row(panel, 2, 0, "Reset by a crash (panic).");
	expect_row(panel, 3, 0, "The last run ended with the crash screen. Its last 5 lines:");
	expect_row(panel, 4, 0, "I (100) storage: Filesystem mounted");
	expect_row(panel, 8, 0, "What happened: Load access fault");
	expect_row(panel, 9, 0, "(end of the last run's lines)");
	expect_row(panel, 10, 0, "Card mounted at /sdcard.");
	expect_row(panel, 12, 0, "  [pc] mem_size = 4M");
	/* longer than a row: broken at the last space that fits */
	check(strlen(err) > COLS, "the error line is longer than a row");
	expect_row(panel, 14, 0, "[pc] hda = /sdcard/freedos.img: there is no such file on "
		   "the card. Check the name (and the folder)");
	expect_row(panel, 15, 0, "in tiny386.ini.");
	expect_row(panel, 16, 0, "tabs:   one     two; control ? and colour codes");
	expect_row(panel, 17, 0, word + 50);    /* 100 of them */
	expect_row(panel, 18, 0, word + 100);   /* and the other 50 */
	expect_colour(panel, 0, 1, rgb(170, 170, 170), "information in light grey");
	expect_colour(panel, 0, 2, rgb(255, 255, 85), "a warning in yellow");
	expect_colour(panel, 0, 4, rgb(85, 255, 255), "the last run's lines in cyan");
	expect_colour(panel, 0, 10, rgb(85, 255, 85), "good news in green");
	expect_colour(panel, 0, 14, rgb(255, 85, 85), "an error in red");
	expect_colour(panel, 0, 15, rgb(255, 85, 85), "a wrapped error stays red");

	/* the colour card: 16 pixels at the bottom, under a grey line */
	static const int bars[8][3] = {
		{ 0, 0, 0 }, { 0, 0, 255 }, { 0, 255, 0 }, { 0, 255, 255 },
		{ 255, 0, 0 }, { 255, 0, 255 }, { 255, 255, 0 }, { 255, 255, 255 },
	};
	check(panel[464 * W + 10] == rgb(85, 85, 85), "the grey line above the card");
	for (int i = 0; i < 8; i++) {
		snprintf(line, sizeof(line), "colour bar %d", i);
		check(panel[470 * W + i * 50 + 25] == rgb(bars[i][0], bars[i][1], bars[i][2]), line);
	}
	check(panel[470 * W + 400] == 0, "the grey ramp starts black");
	check(panel[470 * W + 799] == 0xffff, "the grey ramp ends white");

	/* every row with text on it was flushed: the panel copy matches */
	check(!memcmp(panel, fb, sizeof(fb)), "every drawn row was handed to the flush");

	/* ==== 2. scrolling ==== */
	printf("scrolling\n");
	for (int i = 1; i <= 60; i++)
		console_printf(i % 10 ? CON_INFO : CON_WARN, "line %d of 60", i);
	int calls_before = flush_calls;
	panel_take();
	write_ppm("console-2-scrolled.ppm", panel);
	expect_row(panel, 0, blue, " tiny386 on the ESP32-S31-Korvo-1   v3 host test   ESP-IDF v6.1");
	expect_row(panel, 28, 0, "line 60 of 60");
	expect_row(panel, 1, 0, "line 33 of 60");
	expect_colour(panel, 0, 28, rgb(255, 255, 85), "line 60 (a warning) in yellow");
	check(!memcmp(panel, fb, sizeof(fb)), "scrolled rows were handed to the flush");
	console_puts(CON_INFO, "");
	console_puts(CON_INFO, "after an empty line");
	panel_take();
	expect_row(panel, 27, 0, "");
	expect_row(panel, 28, 0, "after an empty line");
	check(flush_calls > calls_before, "flushed after the last lines");

	/* nothing printed, nothing flushed */
	calls_before = flush_calls;
	console_set_title(" tiny386 on the ESP32-S31-Korvo-1   v3 host test   ESP-IDF v6.1");
	check(flush_calls == calls_before, "the same title again flushes nothing");

	/* ==== 3. the ring ==== */
	printf("ring\n");
	int n = console_ring_tail(tail, sizeof(tail), 3);
	check(n == 3, "three lines from the ring's tail");
	check(!strcmp(tail, "line 60 of 60\n\nafter an empty line\n"),
	      "the ring's last three lines");
	char small[24];
	n = console_ring_tail(small, sizeof(small), 3);
	check(n == 2 && !strcmp(small, "\nafter an empty line\n"),
	      "a small buffer gets the whole lines that fit");
	/* far more than the ring holds: it wraps, and the tail still reads */
	for (int i = 0; i < 400; i++)
		console_printf(CON_INFO, "filler %03d: the quick brown fox jumps over the lazy dog", i);
	n = console_ring_tail(tail, sizeof(tail), 2);
	check(n == 2 && !strcmp(tail, "filler 398: the quick brown fox jumps over the lazy dog\n"
			     "filler 399: the quick brown fox jumps over the lazy dog\n"),
	      "the tail after the ring wrapped");

	/* ==== 4. the emulator takes the panel, and the console takes it back ==== */
	printf("handing the panel over and back\n");
	panel_take();
	uint16_t before[W * H];
	memcpy(before, fb, sizeof(fb));
	console_release_display();
	check(!console_owns_display(), "released");
	calls_before = flush_calls;
	console_puts(CON_INFO, "a line while the picture has the panel");
	check(flush_calls == calls_before, "no drawing while the picture has the panel");
	check(!memcmp(before, fb, sizeof(fb)), "the frame buffer left alone");
	for (int i = 0; i < W * H; i++)       /* the VGA code draws something */
		fb[i] = (uint16_t) (i * 2654435761u >> 16);
	console_take_display();
	panel_take();
	write_ppm("console-3-taken-back.ppm", panel);
	expect_row(panel, 28, 0, "a line while the picture has the panel");
	expect_row(panel, 27, 0, "filler 399: the quick brown fox jumps over the lazy dog");
	check(panel[470 * W + 4 * 50 + 25] == rgb(255, 0, 0), "the colour card drawn again");
	check(!memcmp(panel, fb, sizeof(fb)), "the whole screen drawn again and flushed");

	/* ==== 5. the crash screen ==== */
	printf("crash screen\n");
	struct crash_report r = {
		.core = 1,
		.what = "abort() was called",
		.detail = "assert failed: pc_new pc.c:812 (ret == 0)",
		.mepc = 0x2f00a1b4, .ra = 0x4004c3d2, .sp = 0x2f07d2a0,
		.mcause = 2, .mtval = 0,
		.bt = { 0x4004c3d2, 0x40051a10, 0x2f006e00 }, .nbt = 3,
	};
	console_puts(CON_INFO, "Cannot open the disk image /sdcard/win95.img (No such file or directory).");
	console_draw_crash(fb, W, H, &r);   /* straight into memory, no flush */
	write_ppm("console-4-crash.ppm", fb);
	uint16_t red = rgb(170, 0, 0);
	expect_row(fb, 0, red, " tiny386 has stopped: a crash on core 1");
	check(fb[5 * W + 795] == red, "the crash bar is red to the right edge");
	expect_row(fb, 2, 0, "What happened: abort() was called");
	expect_row(fb, 3, 0, "assert failed: pc_new pc.c:812 (ret == 0)");
	expect_row(fb, 5, 0, "Where: mepc 2f00a1b4  ra 4004c3d2  sp 2f07d2a0  mcause 00000002  mtval 00000000");
	expect_row(fb, 6, 0, "Backtrace (code addresses on the stack, newest first): 4004c3d2 40051a10 2f006e00");
	expect_row(fb, 7, 0, "To decode: riscv32-esp-elf-addr2line -pfiaC -e tiny386.elf and the addresses above");
	expect_row(fb, 9, 0, "The last lines of the log:");
	expect_row(fb, 10, 0, "filler 384: the quick brown fox jumps over the lazy dog");
	expect_row(fb, 27, 0, "Cannot open the disk image /sdcard/win95.img (No such file or directory).");
	expect_row(fb, 29, 0, "The full report is on the UART (115200 baud). Take a photo of this screen, then press RESET.");
	expect_colour(fb, 0, 3, rgb(255, 255, 85), "the assert text in yellow");

	/* the next boot sees the crash */
	n = console_ring_tail(tail, sizeof(tail), 4);
	check(n == 4 && strstr(tail, "What happened: abort() was called") &&
	      strstr(tail, "Backtrace"), "the report is in the ring");
	memset(fb, 0, sizeof(fb));
	console_init(fb, W, H, host_flush);
	check(console_previous_crashed(), "the next boot knows the last run crashed");
	check(console_previous_lines() > 5, "and has its last lines");

	/* ==== 6. the UART quiet while the PC runs (v3.8: [esp] uart_log = 0) ==== */
	printf("the UART quiet\n");
	console_uart_quiet(true);
	console_puts(CON_INFO, "quiet: to the ring, not the UART");
	console_uart_printf("quiet: a UART-only line, dropped");
	n = console_ring_tail(tail, sizeof(tail), 1);
	check(n == 1 && !strcmp(tail, "quiet: to the ring, not the UART\n"),
	      "while quiet a line still goes to the ring");
	console_uart_quiet(false);
	console_puts(CON_INFO, "loud again: to the UART");

	/* ==== 7. lines that repeat (v3.8: console_log_limit, [esp] verbose_log) ==== */
	printf("lines that repeat\n");
	console_host_set_ms(1000);
	/* off, as before the PC starts or with verbose_log = 1: every line */
	console_puts(CON_INFO, "-- mark 1");
	for (int i = 0; i < 20; i++) {
		snprintf(line, sizeof(line), "outw 0x%x => 0xe22", i);
		console_host_tee_line(line);
	}
	check(lines_after("-- mark 1", "outw 0x") == 20, "limit off: all 20 lines out");
	/* on: of one kind (the numbers left out) the first 5 in 5 s */
	console_log_limit(true);
	console_puts(CON_INFO, "-- mark 2");
	for (int i = 0; i < 50; i++) {
		snprintf(line, sizeof(line), "inw 0x%x <= 0xffff", i);
		console_host_tee_line(line);
	}
	check(lines_after("-- mark 2", "inw 0x") == 5, "limit on: 5 of 50 lines of a kind out");
	check(line_after("-- mark 2", "inw 0x4 <= 0xffff") && !line_after("-- mark 2", "inw 0x5 <= 0xffff"),
	      "limit on: the first five out, not the sixth");
	check(lines_after("-- mark 2", "(not shown") == 0, "no count before the 5 s are over");
	console_host_set_ms(5999);
	console_log_tick();
	check(lines_after("-- mark 2", "(not shown") == 0, "no count at 4.999 s");
	console_host_set_ms(6000);
	console_log_tick();
	check(line_after("-- mark 2", "(not shown: 45 more lines like \"inw 0x31 <= 0xffff\" in 5 s)"),
	      "at 5 s: one line with the count and the last line held back");
	console_log_tick();
	check(lines_after("-- mark 2", "(not shown") == 1, "the count said once");
	/* a new period, kinds apart, console_printf() never held back, ESP_LOGx
	   lines held back as the rest */
	console_puts(CON_INFO, "-- mark 3");
	for (int i = 0; i < 7; i++) {
		snprintf(line, sizeof(line), "inw 0x%x <= 0xffff", 100 + i);
		console_host_tee_line(line);
		snprintf(line, sizeof(line), "read_desc: sel 77bc base 0 limit 0 off 77b8 at "
			 "0028:c0039dc8 (%d)", i + 1);
		console_host_tee_line(line);
		console_host_tee_line("atapi_cmd_check_status");
		console_printf(CON_INFO, "console_printf %d", i);
		snprintf(line, sizeof(line), "E (%d) sdmmc_cmd: sdmmc_read_sectors_dma: "
			 "sdmmc_send_cmd returned 0x107\n", 1234 + i);
		console_host_log_line(line);
	}
	check(lines_after("-- mark 3", "inw 0x") == 5, "a new period: 5 more of the kind out");
	check(lines_after("-- mark 3", "read_desc: ") == 5, "another kind: its own 5");
	check(lines_after("-- mark 3", "atapi_cmd_check_status") == 5, "a line with no number: 5 of 7");
	check(lines_after("-- mark 3", "console_printf ") == 7, "console_printf(): never held back");
	check(lines_after("-- mark 3", "E (") == 5, "ESP_LOGx lines: 5 of 7");
	console_host_set_ms(11000);
	console_log_tick();
	check(lines_after("-- mark 3", "(not shown: 2 more lines like ") == 4 &&
	      line_after("-- mark 3", "(not shown: 2 more lines like \"atapi_cmd_check_status\" in 5 s)") &&
	      line_after("-- mark 3", "(not shown: 2 more lines like \"E (1240) sdmmc_cmd: "
			 "sdmmc_read_sectors_dma: sdmmc_send_cmd returned 0x107\" in 5 s)"),
	      "at 5 s: a count for each of the four kinds");
	/* more kinds than places for them: the one least lately seen makes way
	   and says its count first; no line is lost from the counts */
	console_puts(CON_INFO, "-- mark 4");
	for (int k = 0; k < 20; k++) {
		console_host_set_ms(12000 + 10 * k);
		for (int i = 0; i < 7; i++) {
			snprintf(line, sizeof(line), "kind %c: a line", 'A' + k);
			console_host_tee_line(line);
		}
	}
	int early = lines_after("-- mark 4", "(not shown: 2 more lines like \"kind ");
	console_host_set_ms(18000);
	console_log_tick();
	int all = lines_after("-- mark 4", "(not shown: 2 more lines like \"kind ");
	check(lines_after("-- mark 4", "kind ") == 100 && early == 4 && all == 20,
	      "20 kinds of 7 lines in 16 places: 5 of each out, 4 counts said as their places "
	      "went, all 20 by the end");
	/* off again: what is held is said at once, then every line */
	console_puts(CON_INFO, "-- mark 5");
	for (int i = 0; i < 8; i++)
		console_host_tee_line("set VGA mode 12");
	console_log_limit(false);
	check(line_after("-- mark 5", "(not shown: 3 more lines like \"set VGA mode 12\" in 1 s)"),
	      "limit off: the count held so far said at once");
	for (int i = 0; i < 8; i++)
		console_host_tee_line("set VGA mode 3");
	check(lines_after("-- mark 5", "set VGA mode 3") == 8, "limit off: every line again");
	unsigned held, dropped;
	console_log_stats(&held, &dropped);
	check(held == 45 + 8 + 40 + 3, "the lines held back since boot, counted");

	/* ==== 8. the UART's queue (v3.8: [esp] uart_log = 1 while the PC runs) ==== */
	printf("the UART's queue\n");
	console_uart_async(true);
	console_puts(CON_INFO, "queued 1");
	check(console_host_uart_drain(0) == 9, "a line waits in the queue, 9 bytes");
	check(console_host_uart_drain(4) == 5, "the FIFO takes 4: 5 left");
	check(console_host_uart_drain(5) == 1, "room for 5: 4 more, the line's end needs 2");
	check(console_host_uart_drain(2) == 0, "and then its \\r\\n");
	/* more than the queue holds and no room in the FIFO: dropped from the
	   UART, kept in the ring, counted, and said once the queue is out */
	console_puts(CON_INFO, "-- mark 6");
	for (int i = 0; i < 100; i++) {
		snprintf(line, sizeof(line), "line %03d: %049d", i, i);
		console_puts(CON_INFO, line);
	}
	console_log_stats(&held, &dropped);
	check(dropped == 32, "60 bytes a line, 4096 in the queue: 68 kept, 32 dropped");
	check(lines_after("-- mark 6", "line ") == 100, "the ring has every line");
	int rounds = 0;
	while (console_host_uart_drain(128) && rounds < 1000)
		rounds++;
	check(rounds >= 4096 / 128 && rounds < 1000, "the queue written out 128 bytes at a time");
	console_puts(CON_INFO, "queued last");
	console_uart_async(false);
	console_puts(CON_INFO, "straight again");

	/* ==== 9. held error lines, and what is held when a crash comes (v3.9) ==== */
	printf("held error lines, held lines at a crash\n");
	console_log_limit(true);
	console_host_set_ms(30000);
	console_puts(CON_INFO, "-- mark 7");
	int problems = console_problems();
	for (int i = 0; i < 7; i++) {
		snprintf(line, sizeof(line), "E (%d) diskio_sdmmc: sdmmc_write_blocks failed (0x107)\n",
			 30000 + i);
		console_host_log_line(line);
	}
	check(lines_after("-- mark 7", "E (") == 5 && console_problems() == problems + 5,
	      "held errors: 5 of 7 shown, each a problem");
	console_host_set_ms(35000);
	console_log_tick();
	check(line_after("-- mark 7", "(not shown: 2 more lines like \"E (30006) diskio_sdmmc: "
			 "sdmmc_write_blocks failed (0x107)\" in 5 s)"), "held errors: their count said");
	check(console_problems() == problems + 6,
	      "held errors: the count line at the errors' level (red), not grey");
	/* 40 lines of a kind over 2 s, then a crash that is not the emulator's
	   own abort (which says the counts itself): the crash screen's last
	   lines have the count and the newest line held */
	console_puts(CON_INFO, "-- mark 8");
	for (int i = 0; i < 40; i++) {
		console_host_set_ms(40000 + 50 * i);
		snprintf(line, sizeof(line), "read_desc: sel %x base 0 limit 0 off 77b8 at "
			 "0028:c0039dc8 (%d)", 0x77bc + i, i + 1);
		console_host_tee_line(line);
	}
	check(lines_after("-- mark 8", "read_desc: ") == 5, "40 of a kind in 2 s: 5 out");
	struct crash_report r2 = {
		.core = 0, .what = "Load access fault",
		.mepc = 0x40012345, .ra = 0x40012300, .sp = 0x2f07d000, .mcause = 5, .mtval = 0x10,
	};
	console_draw_crash(fb, W, H, &r2);
	write_ppm("console-5-crash-held.ppm", fb);
	const char *held_line = "(not shown: 35 more lines like \"read_desc: sel 77e3 base 0 "
		"limit 0 off 77b8 at 0028:c0039dc8 (40)\" before the crash)";
	check(line_after("-- mark 8", held_line),
	      "at the crash: the count and the newest line held, into the ring");
	const char *cnt = after("-- mark 8");
	cnt = cnt ? strstr(cnt, "(not shown: 35 ") : NULL;
	check(cnt && strstr(cnt, "\nCRASH (the crash screen was shown):\n"),
	      "the count before the crash report's own lines");
	bool on_screen = false;
	for (int row = 2; row < 29 && !on_screen; row++) {
		char text[COLS + 1];
		row_text(fb, row, 0, text);
		on_screen = !strncmp(text, held_line, COLS);
	}
	check(on_screen, "the count on the crash screen, among the log's last lines");
	console_log_limit(false);
	check(lines_after("-- mark 8", "(not shown: 35 ") == 1, "said once");
	/* v4.6: a crash with no frame buffer (no panel): nothing drawn, and
	   the ring says so, not "the crash screen was shown" */
	console_draw_crash(NULL, W, H, &r2);
	n = console_ring_tail(tail, sizeof(tail), 4);
	check(n == 4 && strstr(tail, "CRASH (no panel, so nothing was shown):\n") &&
	      strstr(tail, "What happened: Load access fault\n") &&
	      !strstr(tail, "the crash screen was shown"),
	      "a crash with no panel: the ring says nothing was shown");

	/* the UART got every line, also those printed while the picture had
	   the panel */
	if (uart) {
		fclose(uart);
		snprintf(line, sizeof(line), "%s/console-uart.log", out_dir);
		FILE *f = fopen(line, "r");
		static char log[64 * 1024];
		size_t got = f ? fread(log, 1, sizeof(log) - 1, f) : 0;
		log[got] = 0;
		if (f)
			fclose(f);
		check(strstr(log, "\nCard mounted at /sdcard.\n") != NULL, "the UART has the card line");
		check(strstr(log, "\na line while the picture has the panel\n") != NULL,
		      "the UART has the line printed while the picture had the panel");
		check(strstr(log, "\nI (100) storage: Filesystem mounted\n") != NULL,
		      "the UART has the last run's lines");
		check(strstr(log, "quiet:") == NULL, "nothing reached the UART while it was quiet");
		check(strstr(log, "\nloud again: to the UART\n") != NULL,
		      "the UART has the line after it was loud again");
		check(strstr(log, "\nqueued 1\r\n") != NULL, "the queue: a line with \\r\\n");
		check(strstr(log, "line 067: ") && !strstr(log, "line 068: ") &&
		      strstr(log, "\n(32 lines dropped here: the UART could not keep up; the ring "
			     "and the panel have them)\r\n"),
		      "the queue: 68 lines, then the count of those dropped");
		check(strstr(log, "\nqueued last\r\nstraight again\n") != NULL,
		      "the queue written out when it is turned off, then lines straight again");
		const char *d1 = strstr(log, "(32 lines dropped"), *q1 = strstr(log, "queued last");
		check(d1 && q1 && d1 < q1, "the lines on the UART in order");
	} else {
		check(0, "could not open the UART log");
	}

	if (failures) {
		printf("test_console: %d checks FAILED\n", failures);
		return 1;
	}
	printf("test_console: all checks passed\n");
	return 0;
}
