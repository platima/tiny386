/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_psram.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"

#include "../../ini.h"
#include "../../pc.h"
#include "../../vga.h"
#include "../../i8254.h"
#include "common.h"
#include "console.h"
#include "disk_image.h"
#include "kbd_input.h"
#include "ini_edit.h"
#include "setup.h"
#include "storage.h"
#include "bootmenu.h"
#include "speed.h"
#include "status_led.h"
#ifdef LCD_ZERO_COPY
#include "display.h"
#include "lcd.h"
#endif
#ifdef KM_UART_NUM
#include "ch9350.h"
#include "dosboard.h"
#endif

#ifndef BOARD_NAME
#define BOARD_NAME "an ESP32 board"
#endif

/* guest disk writes go to the card once the guest has written nothing
   for this long */
#define DISK_QUIET_US 2000000

//
#include "esp_private/system_internal.h"
uint32_t get_uticks()
{
	return esp_system_get_time();
}

/* the ini the PC is started from: its path, its name for messages, its
   folder (a file named in it without a / is looked for there) */
static char s_ini_path[176];
static char s_ini_name[SETUP_NAME_MAX] = "tiny386.ini";
static char s_ini_dir[96];
/* the Linux route's mount points, as this firmware has them */
static struct setup_map s_maps[2] = {
	{ "/mnt/sd", "/sdcard" },
	{ "/mnt/win", NULL },
};
static int s_ini_bad;           /* errors found while reading the ini */

void *psmalloc(long size);
void *fbmalloc(long size);
void *bigmalloc(size_t size)
{
	return psmalloc(size);
}

/* An error that the emulator's own code (ide.c) cannot go on from. */
void tiny386_fatal(const char *fmt, ...) __attribute__((noreturn));
void tiny386_fatal(const char *fmt, ...)
{
	static char msg[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	console_fatal("%s", msg);
}

/* The devices' state lives in the guest's own 0xA0000 window (128 KiB). */
static char *pcram;
static long pcram_off;
static long pcram_len;
void *pcmalloc(long size)
{
	void *ret = pcram + pcram_off;

	size = (size + 31) / 32 * 32;
	if (pcram_off + size > pcram_len) {
		console_fatal("The emulator's device memory is full: %ld more bytes "
			      "wanted, %ld of %ld used. This is a fault in this "
			      "build, not in the settings.", size, pcram_off, pcram_len);
	}
	pcram_off += size;
	return ret;
}

void pcmalloc_init(void *ptr, long len)
{
	pcram = ptr;
	pcram_len = len;
}

/* true if the partition or file a BIOS line names is there; why not in why */
static bool rom_exists(const char *file, char *why, int whylen)
{
	if (file && file[0] == '/') {
		struct stat st;
		if (stat(file, &st) != 0) {
			snprintf(why, whylen, "there is no file %s", file);
			return false;
		}
		if (st.st_size <= 0 || st.st_size > 256 * 1024) {
			snprintf(why, whylen, "%s is %ld bytes, not a BIOS", file,
				 (long) st.st_size);
			return false;
		}
		return true;
	}
	const esp_partition_t *part =
		esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
					 ESP_PARTITION_SUBTYPE_ANY, file);
	if (!part) {
		snprintf(why, whylen, "there is no file %s beside %s, and no flash "
			 "partition called \"%s\"", file, s_ini_name, file);
		return false;
	}
	/* load_rom() reads the whole partition, below the BIOS's top: the
	   same limit as for a file, so that "bios = linux" cannot read 7.5 MiB
	   over the guest's memory */
	if (part->size > 256 * 1024) {
		snprintf(why, whylen, "the flash partition \"%s\" is %lu KiB, not a "
			 "BIOS", file, (unsigned long) (part->size / 1024));
		return false;
	}
	return true;
}

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
	if (file && file[0] == '/') {
		FILE *fp = fopen(file, "rb");
		if (!fp) {
			status_led_fatal(STATUS_ERR_BIOS);
			console_fatal("Cannot open %s (%s). Put it on the card, or "
				      "correct its line in %s.", file,
				      strerror(errno), s_ini_name);
		}
		fseek(fp, 0, SEEK_END);
		int len = ftell(fp);
		fprintf(stderr, "%s len %d\n", file, len);
		rewind(fp);
		int got;
		if (backward)
			got = fread(phys_mem + addr - len, 1, len, fp);
		else
			got = fread(phys_mem + addr, 1, len, fp);
		fclose(fp);
		if (got != len) {
			status_led_fatal(STATUS_ERR_BIOS);
			console_fatal("Could read only %d of the %d bytes of %s. "
				      "The card may be faulty.", got, len, file);
		}
		return len;
	}
	const esp_partition_t *part =
		esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
					 ESP_PARTITION_SUBTYPE_ANY,
					 file);
	if (!part) {
		status_led_fatal(STATUS_ERR_BIOS);
		console_fatal("There is no flash partition called \"%s\", and no "
			      "such file beside %s. Put the file there, or give its "
			      "whole path, for example bios = /sdcard/bios.bin",
			      file ? file : "", s_ini_name);
	}
	int len = part->size;
	fprintf(stderr, "%s len %d\n", file, len);
	if (backward)
		esp_partition_read(part, 0, phys_mem + addr - len, len);
	else
		esp_partition_read(part, 0, phys_mem + addr, len);
	return len;
}

//
EventGroupHandle_t global_event_group;
struct Globals globals;

/* what the VGA code draws into */
typedef struct {
	PC *pc;
	u8 *fb1;
	u8 *fb;
} PCScreen;

#define NN 32
static PCScreen *screen_init(int width, int height)
{
	PCScreen *c = malloc(sizeof(PCScreen));
	(void) width; (void) height;
#ifndef LCD_ZERO_COPY
	c->fb1 = fbmalloc(LCD_WIDTH * LCD_HEIGHT / NN * 2);
#endif
	if (globals.panel_fb) {
		/* Zero-copy: VGA renders directly into the RGB DMA frame buffer */
		c->fb = globals.panel_fb;
	} else {
		c->fb = bigmalloc(LCD_WIDTH * LCD_HEIGHT * 2);
	}
	return c;
}

#ifdef LCD_ZERO_COPY
static void redraw(void *opaque,
		   int x, int y, int w, int h)
{
	(void) opaque;
	lcd_flush(x, y, w, h);
}
#else
void lcd_draw(int x_start, int y_start, int x_end, int y_end, void *src);
static void redraw(void *opaque,
		   int x, int y, int w, int h)
{
	PCScreen *s = opaque;
	for (int i = 0; i < NN; i++) {
		uint16_t *src = (uint16_t *) s->fb;
		src += LCD_WIDTH * LCD_HEIGHT / NN * i;
		memcpy(s->fb1, src, LCD_WIDTH * LCD_HEIGHT / NN * 2);
		lcd_draw(0, LCD_WIDTH / NN * i,
			 LCD_HEIGHT, LCD_WIDTH / NN * (i + 1),
			 s->fb1);
		vga_step(s->pc->vga);
		usleep(900);
	}
}
#endif

//

void *esp_psram_get(size_t *size);
void vga_task(void *arg);
void i2s_main();
#ifdef NO_WIFI
/* what wifi.c would give the emulated network card: no way out */
void (*_Atomic esp32_send_packet)(uint8_t *buf, int size);
#else
void wifi_main(const char *, const char *);
#endif
void usb_setup(void);

/* A board's own start-up, where it has one (dosboard.c): the first thing
   in app_main(), before any pin is driven, and once the boot console is
   up, for what it noted before that. */
void __attribute__((weak)) board_early_init(void) {}
void __attribute__((weak)) board_console_ready(void) {}

/* The keyboard and mouse: the USB host, or on the DOS board the CH9350L
   (which has its own USB host chip), once; either may be running already
   for the boot menu. */
static void input_setup(void)
{
#ifdef KM_UART_NUM
	ch9350_setup(dosboard_cfg.ch9350_state, dosboard_cfg.kbd_zero_guard_ms);
#else
	usb_setup();
#endif
}

struct esp_ini_config {
	const char *filename;
	char ssid[32];
	char pass[64];
	int enable_usb;
	int console_s;          /* seconds the boot console stays up */
	int lcd_invalidate;
	int crash_test;         /* only read in a TINY386_CRASH_TEST build */
	int uart_log;           /* the log to the UART too while the PC runs */
	int verbose_log;        /* every line while the PC runs, none held back */
	int fast_seek;          /* the disk images' clusters mapped (disk_image.c) */
	int timer_catchup;      /* i8254_max_owed: timer ticks owed at once */
	int memtest;            /* passes of the PSRAM test before the PC is built */
	int vbe_blocks;         /* vga_dirty_blocks: VBE pictures from the blocks written */
	int pace_quiet;         /* v4.4: vga_pace_quiet, guest instructions; 0 off */
	int pace_wait_ms;       /* v4.4: the most a picture waits for a quiet moment */
};

#ifdef TINY386_CRASH_TEST
#include "soc/rtc.h"

/* [esp] crash_test, in a test build only (idf.py -DCRASH_TEST=1): makes
   the crash screen appear on purpose.  1 abort(), 2 a store to address 0,
   3 a failed assert, all on core 0 before the PC starts; 4 abort() on
   core 1 three seconds after the PC has started, over its picture; 5
   abort() on core 0 before the PC starts, with the CPU first set to 80 MHz
   and the status LED told that app_main() has not run, so that its panic
   path sends yellow 1 at the clock a panic before app_main() has (the
   second-stage bootloader's 80 MHz): for a scope on the LED's data line
   (TEST-PLAN 10, STATUS-LED.md 5). */
static void crash_test(int what)
{
	if (what >= 1 && what <= 3)
		console_printf(CON_WARN, "crash_test = %d: crashing on purpose.", what);
	switch (what) {
	case 1:
		abort();
	case 2: {
		/* through a variable, so that the compiler makes it a real store */
		volatile int *volatile nowhere = NULL;
		*nowhere = 1;
		break;
	}
	case 3:
		assert(what != 3);
		break;
	case 5: {
		rtc_cpu_freq_config_t cfg;
		if (!rtc_clk_cpu_freq_mhz_to_config(80, &cfg)) {
			console_puts(CON_WARN, "crash_test = 5: no 80 MHz setting here; not crashing.");
			break;
		}
		console_puts(CON_WARN, "crash_test = 5: the CPU to 80 MHz, then crashing on purpose "
			     "as if before tiny386 started (yellow 1).");
		status_led_test_early();
		rtc_clk_cpu_freq_set_config(&cfg);
		abort();
	}
	}
}
#endif

static struct esp_ini_config config = {
	.console_s = 2,
	.enable_usb = 1,        /* a keyboard unless the ini says no */
	.uart_log = 0,          /* the log is on the panel (Ctrl+Alt+F12) */
	.verbose_log = 0,       /* lines that repeat held back after 5 in 5 s */
	.fast_seek = 1,
	.timer_catchup = 1,     /* as a real 8254 and 8259 (i8254.c) */
	.vbe_blocks = 1,        /* v4.0: only what the guest wrote is drawn again */
	.pace_quiet = 0,        /* v4.4: pictures when they are due (BUILD-NOTES 29) */
	.pace_wait_ms = 15,     /* DISPLAY_PACE_WAIT_MS */
};
static PCConfig conf;

static char *psram;
static long psram_off;
static long psram_len;
void *psmalloc(long size)
{
	void *ret = psram + psram_off;

	size = (size + 4095) / 4096 * 4096;
	if (psram_off + size > psram_len) {
		console_fatal("Out of emulator memory: %ld more KiB wanted, %ld of "
			      "%ld KiB used. Lower mem_size or vga_mem_size in "
			      "%s.", size / 1024, psram_off / 1024,
			      psram_len / 1024, s_ini_name);
	}
	psram_off += size;
	return ret;
}

void *fbmalloc(long size)
{
	void *fb = (uint8_t *) heap_caps_calloc(1, size, MALLOC_CAP_DMA);
	if (!fb)
		console_fatal("No room for a %ld-byte drawing buffer.", size);
	return fb;
}

/* everything the guest writes to: the IDE disks and the floppies */
static unsigned disk_writes(void)
{
	return ide_write_count() + emulink_write_count();
}

/* the status LED's disk light (v3.8): every read and write the guest asks
   of its disks, CDs and floppies, each counted by one store in ide.c and
   misc.c; read by the LED's task on core 0 */
static uint32_t disk_activity(void)
{
	return ide_activity + emulink_activity;
}

static void disk_flush(PC *pc)
{
	ide_flush();
	emulink_flush(pc->emulink);
}

/* disk_image.c's lines: the images' pieces at the start, a card error got
   over (or not) while the PC runs */
static void disk_image_line(bool warn, const char *line)
{
	console_puts(warn ? CON_WARN : CON_INFO, line);
}

/* first thing when the emulator aborts (i386.c's cpu_abort()): the UART
   on again for the dump, which version this is, and what held the
   emulator up, so that the crash screen's last lines show them.  The
   counts of lines held back are said, every line of the dump goes out,
   and the UART is written straight again, what was queued first, so that
   all of it is there before the panic handler's report. */
static void abort_note(void)
{
	char disk[160], timer[120];
	console_uart_quiet(false);
	console_log_limit(false);
	console_uart_async(false);
	speed_health(disk, sizeof(disk), timer, sizeof(timer));
	fprintf(stderr, "tiny386 %s stopped the PC. %s\n%s\n",
		esp_app_get_description()->version, disk, timer);
}

/* The card refusing a read or a write (pulled out, or failing): said once,
   then again every 10 s while it goes on.  The guest is told of each one
   by its IDE driver, or by the floppy BIOS's status. */
static void disk_errors(uint32_t now)
{
	static unsigned seen;
	static uint32_t said_at;
	unsigned e = ide_error_count() + emulink_error_count();
	if (e != seen && (!seen || now - said_at >= 10000000)) {
		console_printf(CON_ERROR, "The card refused %u read%s or write%s of the "
			       "disk images. Is it still in? Shut the guest down and "
			       "check the card on a PC.", e - seen, e - seen == 1 ? "" : "s",
			       e - seen == 1 ? "" : "s");
		seen = e;
		said_at = now;
	}
}

/* v4.4: a setting a Ctrl+Alt key changed (F11: vbe_blocks, F10:
   pace_quiet) into the ini the PC was started from (setup_ini_save():
   only that line changed or added, the new file made whole beside the old
   one before they are swapped), and a line on the console to say what was
   saved.  On the emulator's core between two steps, after the guest's disk
   writes are on the card, so that no FatFs work of the guest's is under
   way; the guest waits meanwhile. */
static void save_setting(const char *name, unsigned long v)
{
	char why[200], value[16];
	snprintf(value, sizeof(value), "%lu", v);
	int r = setup_ini_save(s_ini_path, "esp", name, value, why, sizeof(why));
	if (r < 0)
		console_printf(CON_ERROR, "%s = %s could not be saved in %s: %s. It holds "
			       "until the PC starts again.", name, value, s_ini_path, why);
	else if (r == INI_EDIT_SAME)
		console_printf(CON_INFO, "%s = %s: %s has it already, so nothing was written.",
			       name, value, s_ini_path);
	else
		console_printf(why[0] ? CON_WARN : CON_GOOD, "%s = %s saved in %s (%s)%s%s.",
			       name, value, s_ini_path, r == INI_EDIT_CHANGED ?
			       "its line in [esp] changed" : r == INI_EDIT_ADDED ?
			       "a line added to [esp]" : "[esp] and the line added at the end",
			       why[0] ? "; but " : "", why);
}

static void save_settings(unsigned what)
{
	if (what & SPEED_SAVE_BLOCKS)
		save_setting("vbe_blocks", vga_dirty_blocks ? 1 : 0);
	if (what & SPEED_SAVE_PACE)
		save_setting("pace_quiet", vga_pace_quiet);
}

/* Runs the PC, and puts what the guest wrote to its disks on the card
   once it has been quiet for DISK_QUIET_US: until then FatFs may hold the
   last sectors and the directory entry. */
static void run_pc(PC *pc)
{
	unsigned writes = disk_writes();
	uint32_t write_time = get_uticks();
	bool unflushed = false;
#ifdef LCD_ZERO_COPY
	uint32_t cyc = (uint32_t) cpui386_get_cycle(pc->cpu);
#endif

	speed_run_init(pc);
	while (pc->shutdown_state != 8) {
		pc_step(pc);
#ifdef LCD_ZERO_COPY
		/* v4.4: the guest's last store into video memory, for the
		   pictures at its quiet moments (display.c) */
		{
			uint32_t c = (uint32_t) cpui386_get_cycle(pc->cpu);
			display_pace_step(c, c == cyc);
			cyc = c;
		}
#endif
		status_led_heartbeat();
		uint32_t now = get_uticks();
		speed_run_step(pc, now);
		unsigned w = disk_writes();
		if (w != writes) {
			writes = w;
			write_time = now;
			unflushed = true;
		} else if (unflushed && now - write_time >= DISK_QUIET_US) {
			disk_flush(pc);
			unflushed = false;
			speed_run_aside(get_uticks());
		}
		unsigned saves = speed_take_saves();
		if (saves) {
			/* the guest's writes first, as at a quiet moment */
			if (unflushed) {
				disk_flush(pc);
				unflushed = false;
				speed_run_aside(get_uticks());
			}
			save_settings(saves);
			speed_run_pause(get_uticks());
		}
		disk_errors(now);
	}
	if (unflushed)
		disk_flush(pc);
	unsigned saves = speed_take_saves();
	if (saves)
		save_settings(saves);
	disk_errors(get_uticks());
}

static void i386_task(void *arg)
{
	PCConfig *conf = arg;
#ifndef LCD_ZERO_COPY
	/* Wait for LCD panel (and panel_fb) to be ready before starting the
	 * PC emulator.  screen_init() uses globals.panel_fb if set. */
	xEventGroupWaitBits(global_event_group,
	                    BIT1,
	                    pdFALSE,
	                    pdFALSE,
	                    portMAX_DELAY);
#endif
	PCScreen *screen = screen_init(conf->width, conf->height);
	PC *pc = pc_new(redraw, screen, screen->fb, conf);
	screen->pc = pc;
	globals.pc = pc;
	globals.kbd = pc->kbd;
	globals.mouse = pc->mouse;
	/* PSRAM free too: the 16-colour picture's cache (150 KB at 640x480,
	   234 KB at 800x600) comes from what the emulator's pool left, and
	   without room the picture is drawn the slow way (emulator correctness
	   review, round 2, note 11) */
	console_printf(CON_INFO, "PC built. Device memory (the 0xA0000 window): %ld "
		       "of %ld bytes used. Internal RAM free %u KiB (largest "
		       "block %u KiB); PSRAM free %u KiB (largest block %u KiB).",
		       pcram_off, pcram_len,
		       (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
		       (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
		       (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
		       (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));

	load_bios_and_reset(pc);
	status_led_stage(STATUS_STAGE_BIOS);

	/* Time to read the console, or to photograph it; longer if it has
	   warnings on it. */
	int hold_ms = config.console_s * 1000;
	if (console_problems() && hold_ms < 10000)
		hold_ms = 10000;
	if (hold_ms > 0) {
		console_printf(CON_INFO, "The PC starts in %d seconds.", hold_ms / 1000);
		vTaskDelay(pdMS_TO_TICKS(hold_ms));
	}
	console_puts(CON_GOOD, "The PC is starting: its picture takes the panel. "
		     "Ctrl+Alt+F12 shows this log again, with the speed.");
	if (config.uart_log)
		console_puts(CON_INFO, "While the PC runs the log goes on on the UART "
			     "([esp] uart_log = 1), through a queue that no task waits for.");
	else
		console_puts(CON_INFO, "While the PC runs the log stays off the UART "
			     "([esp] uart_log = 1 puts it there); a crash report goes there still.");
	if (!config.verbose_log)
		console_puts(CON_INFO, "A line the emulator repeats is shown 5 times in 5 s, "
			     "then counted ([esp] verbose_log = 1 shows every one).");
	/* Written straight out, a line at 115200 baud would hold the
	   emulator's core about 87 us a byte, and every such wait comes back
	   to the guest as timer ticks owed: so the UART is off, or reached
	   through the queue */
	if (config.uart_log)
		console_uart_async(true);
	else
		console_uart_quiet(true);
	console_log_limit(!config.verbose_log);
	console_release_display();
	/* a Ctrl+Alt+F12 from the boot console's time is not for the PC's
	   screen: forgotten before vga_task can see it */
	speed_forget_requests();
	xEventGroupSetBits(global_event_group, BIT0);
	/* the LED: a steady green, and blue for each 20 ms in which the guest
	   reads or writes a disk */
	status_led_disk_counter(disk_activity);
	status_led_stage(STATUS_STAGE_RUNNING);
	/* the keyboard and mouse reach the PC from now: keys pressed while
	   the console was up would wait in the PS/2 queue ahead of the BIOS's
	   keyboard reset */
	kbd_input_set_target(pc->kbd, pc->mouse);

	pc->boot_start_time = get_uticks();
#ifdef TINY386_CRASH_TEST
	if (config.crash_test == 4) {
		while (get_uticks() - pc->boot_start_time < 3000000)
			pc_step(pc);
		console_puts(CON_WARN, "crash_test = 4: crashing on purpose.");
		abort();
	}
#endif
	run_pc(pc);
	status_led_stage(STATUS_STAGE_PC_OFF);
	kbd_input_set_target(NULL, NULL);
	console_puts(CON_INFO, "The PC was switched off. Press RESET to start again.");
	console_hold();
	vTaskDelete(NULL);
}

/* ==== the ini file ==== */

static const char *const known_keys[] = {
	"esp:ssid", "esp:pass", "esp:enable_usb", "esp:console",
	"esp:lcd_invalidate", "esp:led_brightness",
	"esp:uart_log", "esp:fast_seek", "esp:timer_catchup", "esp:memtest",
	"esp:verbose_log", "esp:vbe_blocks", "esp:pace_quiet", "esp:pace_wait_ms",
#ifdef TINY386_CRASH_TEST
	"esp:crash_test",
#endif
	"pc:bios", "pc:vga_bios", "pc:mem_size", "pc:vga_mem_size",
	"pc:hda", "pc:hdb", "pc:hdc", "pc:hdd",
	"pc:cda", "pc:cdb", "pc:cdc", "pc:cdd", "pc:fda", "pc:fdb",
	"pc:fill_cmos", "pc:linuxstart", "pc:kernel", "pc:initrd",
	"pc:cmdline", "pc:enable_serial", "pc:vga_force_8dm",
	"display:width", "display:height",
	"cpu:gen", "cpu:fpu", "cpu:cpuid_vendor",
	NULL
};

static bool known_key(const char *section, const char *name)
{
	int sl = strlen(section);
	for (int i = 0; known_keys[i]; i++) {
		const char *k = known_keys[i];
		if (!strncmp(k, section, sl) && k[sl] == ':' && !strcmp(k + sl + 1, name))
			return true;
	}
	return false;
}

/* [esp] led_brightness: the status LED's level, 0 (dark, a crash included)
   to 255, taken at once, so that the LED is at it from the ini on; until
   then it is at STATUS_BRIGHTNESS_DEFAULT (status_code.h). */
static void set_led_brightness(const char *value)
{
#ifdef STATUS_LED_GPIO
	char *end;
	long v = strtol(value, &end, 10);
	while (*end == ' ' || *end == '\t')
		end++;
	if (end == value || *end) {
		console_printf(CON_WARN, "%s: led_brightness = %s is not a number "
			       "from 0 to 255; it was ignored.", s_ini_name, value);
		return;
	}
	if (v < 0 || v > STATUS_BRIGHTNESS_MAX) {
		long held = v < 0 ? 0 : STATUS_BRIGHTNESS_MAX;
		console_printf(CON_WARN, "%s: led_brightness = %s is outside 0 to "
			       "255; %ld is used.", s_ini_name, value, held);
		v = held;
	}
	status_led_brightness((int) v);
	if (v == 0)
		console_puts(CON_INFO, "Status LED: dark (led_brightness = 0), a "
			     "crash included.");
#else
	(void) value;
	console_printf(CON_INFO, "%s: led_brightness is ignored: this board "
		       "has no status LED.", s_ini_name);
#endif
}

/* a whole number from lo to hi for [esp] name; anything else keeps *out
   and says so in yellow */
static void ini_whole(const char *name, const char *value, long lo, long hi, int *out)
{
	char *end;
	long v = strtol(value, &end, 10);
	while (*end == ' ' || *end == '\t')
		end++;
	if (end == value || *end || v < lo || v > hi) {
		console_printf(CON_WARN, "%s: %s = %s is not a whole number from %ld to "
			       "%ld; %d is used.", s_ini_name, name, value, lo, hi, *out);
		return;
	}
	*out = (int) v;
}

static int parse_esp_ini(struct esp_ini_config *conf,
			 const char *name, const char *value)
{
#define NAME(a) (strcmp(name, a) == 0)
	if (NAME("ssid")) {
		if (strlen(value) < sizeof(conf->ssid))
			strcpy(conf->ssid, value);
		else
			console_printf(CON_WARN, "%s: ssid is longer than %d "
				       "characters; ignored.", s_ini_name,
				       (int) sizeof(conf->ssid) - 1);
	} else if (NAME("pass")) {
		if (strlen(value) < sizeof(conf->pass))
			strcpy(conf->pass, value);
		else
			console_printf(CON_WARN, "%s: pass is longer than %d "
				       "characters; ignored.", s_ini_name,
				       (int) sizeof(conf->pass) - 1);
	} else if (NAME("enable_usb")) {
		conf->enable_usb = atoi(value);
	} else if (NAME("console")) {
		conf->console_s = atoi(value);
	} else if (NAME("lcd_invalidate")) {
		conf->lcd_invalidate = atoi(value);
	} else if (NAME("led_brightness")) {
		set_led_brightness(value);
	} else if (NAME("uart_log")) {
		ini_whole(name, value, 0, 1, &conf->uart_log);
	} else if (NAME("verbose_log")) {
		ini_whole(name, value, 0, 1, &conf->verbose_log);
	} else if (NAME("fast_seek")) {
		ini_whole(name, value, 0, 1, &conf->fast_seek);
	} else if (NAME("timer_catchup")) {
		ini_whole(name, value, 0, 1000, &conf->timer_catchup);
	} else if (NAME("memtest")) {
		ini_whole(name, value, 0, 10, &conf->memtest);
	} else if (NAME("vbe_blocks")) {
		ini_whole(name, value, 0, 1, &conf->vbe_blocks);
	} else if (NAME("pace_quiet")) {
		ini_whole(name, value, 0, 10000000, &conf->pace_quiet);
	} else if (NAME("pace_wait_ms")) {
		ini_whole(name, value, 1, 200, &conf->pace_wait_ms);
	} else if (NAME("crash_test")) {
		conf->crash_test = atoi(value);
	}
#undef NAME
	return 1;
}

/* every key: shown, checked against the list, a file name resolved, then
   handed on */
static int parse_ini(void *user, const char *section,
		     const char *name, const char *value)
{
	(void) user;
	bool secret = !strcmp(section, "esp") && !strcmp(name, "pass");
	static char resolved[200];

	if (known_key(section, name) && setup_is_path_key(section, name) && value[0]) {
		bool missing;
		setup_resolve(s_ini_dir, value, s_maps, 2, resolved, sizeof(resolved),
			      &missing);
		/* a BIOS named without a /, not beside the ini, may be a flash
		   partition, as tiny386's own partition tables have them */
		if ((!strcmp(name, "bios") || !strcmp(name, "vga_bios")) &&
		    !strchr(value, '/') && access(resolved, R_OK) != 0 &&
		    esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
					     ESP_PARTITION_SUBTYPE_ANY, value))
			snprintf(resolved, sizeof(resolved), "%s", value);
		if (missing) {
			console_printf(CON_ERROR, "  [%s] %s = %s: /mnt/win is the "
				       "Linux route's name for the card's WIN9X "
				       "partition, and this card has no second FAT "
				       "partition.", section, name, value);
			s_ini_bad++;
			return 1;
		}
		if (strcmp(resolved, value))
			console_printf(CON_INFO, "  [%s] %s = %s   (%s)", section, name,
				       value, resolved);
		else
			console_printf(CON_INFO, "  [%s] %s = %s", section, name, value);
		value = resolved;
	} else {
		console_printf(CON_INFO, "  [%s] %s = %s", section, name,
			       secret ? "(not shown)" : value);
	}
	if (!strcmp(section, "dosboard")) {
		console_printf(CON_WARN, "%s: [dosboard] %s is ignored here: the DOS "
			       "board's settings go in dosboard.ini beside the inis, "
			       "which is read before the picture starts.", s_ini_name, name);
		return 1;
	}
	if (!known_key(section, name)) {
		console_printf(CON_WARN, "%s: [%s] %s is not a setting tiny386 "
			       "knows; it was ignored. Check the spelling and the "
			       "[section] it is under.", s_ini_name, section, name);
		return 1;
	}
	if (!strcmp(section, "esp"))
		return parse_esp_ini(&config, name, value);
	return parse_conf_ini(&conf, section, name, value);
}

static long round4k(long n)
{
	return (n + 4095) / 4096 * 4096;
}

/* [esp] memtest = N (v3.8): N passes over the emulator's PSRAM pool before
   the PC is built, with the panel's DMA reading the frame buffer as it does
   while the PC runs.  Each word gets a pattern made from its index and the
   pass (every other pass the inverse), and is read back once the whole
   pool is written, so that it has long left the 64 KiB cache.  A wrong word
   means the PSRAM did not keep what it was given: guest memory, and so the
   guest, would be wrong without any fault in the emulator.  The pool is
   zeroed after.  Off by default; about a second a pass. */
static inline uint32_t memtest_word(uint32_t i, uint32_t pass)
{
	uint32_t x = i * 2654435761u ^ (pass + 1) * 0x9e3779b9u;
	x ^= x >> 15;
	return pass & 1 ? ~x : x;
}

static void psram_test(int passes)
{
	if (passes <= 0 || !psram)
		return;
	volatile uint32_t *w = (volatile uint32_t *) psram;
	uint32_t n = (uint32_t) (psram_len / 4);
	uint32_t bad = 0, at = 0, want0 = 0, got0 = 0, bits = 0;
	uint32_t t0 = get_uticks();
	console_printf(CON_INFO, "PSRAM test ([esp] memtest = %d): %ld KiB, the emulator's "
		       "pool...", passes, psram_len / 1024);
	for (uint32_t p = 0; p < (uint32_t) passes; p++) {
		for (uint32_t i = 0; i < n; i++) {
			w[i] = memtest_word(i, p);
			if (!(i & 0x3ffff))
				vTaskDelay(1);          /* every MiB: core 0's other tasks */
		}
		for (uint32_t i = 0; i < n; i++) {
			uint32_t want = memtest_word(i, p), got = w[i];
			if (got != want) {
				if (!bad++) {
					at = (uint32_t) (uintptr_t) &w[i];
					want0 = want;
					got0 = got;
				}
				bits |= got ^ want;
			}
			if (!(i & 0x3ffff))
				vTaskDelay(1);
		}
	}
	memset(psram, 0, psram_len);
	unsigned ms = (unsigned) ((get_uticks() - t0) / 1000);
	if (!bad)
		console_printf(CON_GOOD, "PSRAM test: %d pass%s over %ld KiB, every word "
			       "right (%u ms).", passes, passes == 1 ? "" : "es",
			       psram_len / 1024, ms);
	else
		console_printf(CON_ERROR, "PSRAM test: %u word%s wrong in %d pass%s; the "
			       "first at %08x, %08x written, %08x read; bits wrong "
			       "anywhere %08x. The board's PSRAM does not keep what it "
			       "is given.", (unsigned) bad, bad == 1 ? "" : "s", passes,
			       passes == 1 ? "" : "es",
			       (unsigned) at, (unsigned) want0, (unsigned) got0,
			       (unsigned) bits);
}

/* The guest's memory: what the ini asks for if it fits; "mem_size = max",
   0 or no line at all gives the most that fits (14M on the Korvo-1 with
   vga_mem_size 256K or 320K); more than fits is lowered to that, with a
   warning, rather than stopping. */
static void settle_memory(void)
{
	/* The guest is told the size of video memory in 64 KiB (VBE's
	   VIDEO_MEMORY_64K), and its bank window steps by 64 KiB: a size in
	   between would be reported smaller than it is, so it is rounded down
	   to a whole 64 KiB here, with a line to say so. */
	if (conf.vga_mem_size % (64 * 1024)) {
		long down = conf.vga_mem_size / (64 * 1024) * (64 * 1024);
		if (down < 64 * 1024)
			down = 64 * 1024;
		console_printf(CON_WARN, "vga_mem_size %ldK is not a whole number of 64K: "
			       "%ldK is used.", conf.vga_mem_size / 1024, down / 1024);
		conf.vga_mem_size = down;
	}
	long vga = round4k(conf.vga_mem_size + VGA_MEM_SLACK);
	long fit = (psram_len - vga) / (1024 * 1024) * (1024 * 1024);
	if (conf.mem_size <= 0) {
		conf.mem_size = fit;
		console_printf(CON_INFO, "mem_size: %ldM, the most there is room for "
			       "(no mem_size line, or max).", fit / (1024 * 1024));
	} else if (round4k(conf.mem_size) + vga > psram_len && fit >= 1024 * 1024) {
		console_printf(CON_WARN, "mem_size %ldK with vga_mem_size %ldK needs "
			       "%ldK, and the emulator has %ldK: mem_size lowered to "
			       "%ldM, the most that fits.", conf.mem_size / 1024,
			       conf.vga_mem_size / 1024, (round4k(conf.mem_size) + vga) / 1024,
			       psram_len / 1024, fit / (1024 * 1024));
		conf.mem_size = fit;
	}
}

/* A BIOS the ini does not name: bios.bin and vgabios.bin beside the ini,
   if they are there (else pc.c's default, a flash partition). */
static void default_roms(void)
{
	static char bios[160], vga_bios[160];
	if (!conf.bios) {
		snprintf(bios, sizeof(bios), "%s/bios.bin", s_ini_dir);
		if (access(bios, R_OK) == 0) {
			conf.bios = bios;
			console_printf(CON_INFO, "  bios (not in the ini): %s", bios);
		}
	}
	if (!conf.vga_bios) {
		snprintf(vga_bios, sizeof(vga_bios), "%s/vgabios.bin", s_ini_dir);
		if (access(vga_bios, R_OK) == 0) {
			conf.vga_bios = vga_bios;
			console_printf(CON_INFO, "  vga_bios (not in the ini): %s", vga_bios);
		}
	}
}

/* Everything the ini names, before the PC is built: anything wrong is
   said here in plain words, rather than an abort later. */
static int s_bios_bad;           /* how many of check_config()'s were a BIOS */

static int check_config(void)
{
	int bad = s_ini_bad;
	char why[200];

	if (conf.cpu_gen < 3 || conf.cpu_gen > 6) {
		console_printf(CON_ERROR, "[cpu] gen = %d: it must be 3, 4, 5 or 6 "
			       "(386, 486, Pentium, Pentium Pro).", conf.cpu_gen);
		bad++;
	}
	const char *bios = conf.bios ? conf.bios : "bios.bin";
	const char *vga_bios = conf.vga_bios ? conf.vga_bios : "vgabios.bin";
	if (!rom_exists(bios, why, sizeof(why))) {
		console_printf(CON_ERROR, "The BIOS cannot be loaded: %s. Put bios.bin "
			       "from the tiny386 release beside %s, or name it "
			       "with bios = under [pc].", why, s_ini_name);
		bad++;
		s_bios_bad++;
	}
	if (!rom_exists(vga_bios, why, sizeof(why))) {
		console_printf(CON_ERROR, "The VGA BIOS cannot be loaded: %s. Put "
			       "vgabios.bin beside %s, or name it with vga_bios = "
			       "under [pc].", why, s_ini_name);
		bad++;
		s_bios_bad++;
	}
	static const char *const disk_names[4][2] = {
		{ "hda", "cda" }, { "hdb", "cdb" }, { "hdc", "cdc" }, { "hdd", "cdd" },
	};
	int disks = 0;
	for (int i = 0; i < 6; i++) {
		const char *path = i < 4 ? conf.disks[i] : conf.fdd[i - 4];
		const char *key = i < 4 ? disk_names[i][conf.iscd[i] ? 1 : 0] :
			(i == 4 ? "fda" : "fdb");
		if (!path || !path[0])
			continue;
		disks++;
		/* One image named twice (two disks, or a disk and a floppy) would
		   be open twice, each with a cache of its own and FatFs's file
		   lock off, and the two could leave its clusters crossed (boot
		   safety review, round 2, note 6).  FAT names have no case. */
		bool twice = false;
		for (int j = 0; j < i && !twice; j++) {
			const char *other = j < 4 ? conf.disks[j] : conf.fdd[j - 4];
			if (other && other[0] && strcasecmp(other, path) == 0) {
				console_printf(CON_ERROR, "[pc] %s and %s both name %s: one image "
					       "cannot be two drives. Change one of them in %s.",
					       j < 4 ? disk_names[j][conf.iscd[j] ? 1 : 0] :
					       (j == 4 ? "fda" : "fdb"), key, path, s_ini_name);
				twice = true;
			}
		}
		if (twice) {
			bad++;
			continue;
		}
		struct stat st;
		if (stat(path, &st) != 0) {
			console_printf(CON_ERROR, "[pc] %s: there is no file %s. Check "
				       "the name in %s (a name without a / in front "
				       "is looked for beside the ini).", key, path,
				       s_ini_name);
			bad++;
		} else {
			console_printf(CON_INFO, "%s: %s, %ld KiB", key, path,
				       (long) (st.st_size / 1024));
		}
	}
	if (!disks)
		console_printf(CON_WARN, "No disk image is set (hda = ... under [pc] "
			       "in %s): the PC will have nothing to start from but "
			       "the network or a floppy.", s_ini_name);

	long vga = round4k(conf.vga_mem_size + VGA_MEM_SLACK);
	long need = round4k(conf.mem_size) + vga;
	if (conf.mem_size < 1024 * 1024) {
		console_printf(CON_ERROR, "[pc] mem_size = %ld KiB is too small: give "
			       "at least 1M.", conf.mem_size / 1024);
		bad++;
	} else if (need > psram_len) {
		console_printf(CON_ERROR, "mem_size %ld KiB and vga_mem_size %ld KiB "
			       "need %ld KiB, but the emulator has %ld KiB. Lower "
			       "vga_mem_size in %s.", conf.mem_size / 1024,
			       conf.vga_mem_size / 1024, need / 1024, psram_len / 1024,
			       s_ini_name);
		bad++;
	}
	return bad;
}

static const char *reset_words(esp_reset_reason_t r, bool *bad)
{
	*bad = false;
	switch (r) {
	case ESP_RST_POWERON:   return "power-on";
	case ESP_RST_EXT:       return "the reset pin";
	case ESP_RST_SW:        return "a software restart";
	case ESP_RST_DEEPSLEEP: return "waking from deep sleep";
	case ESP_RST_SDIO:      return "SDIO";
	case ESP_RST_USB:       return "USB";
	case ESP_RST_JTAG:      return "JTAG";
	default: break;
	}
	*bad = true;
	switch (r) {
	case ESP_RST_PANIC:      return "a crash (panic)";
	case ESP_RST_INT_WDT:    return "the interrupt watchdog";
	case ESP_RST_TASK_WDT:   return "the task watchdog";
	case ESP_RST_WDT:        return "a watchdog";
	case ESP_RST_BROWNOUT:   return "a brownout (the supply dipped: try "
				       "another USB supply or cable)";
	case ESP_RST_PWR_GLITCH: return "a power glitch";
	case ESP_RST_CPU_LOCKUP: return "a CPU lock-up";
	case ESP_RST_EFUSE:      return "an eFuse error";
	default:                 return "an unknown cause";
	}
}

static char s_title[160];       /* the console's title row */

static void banner(void)
{
	const esp_app_desc_t *app = esp_app_get_description();
	snprintf(s_title, sizeof(s_title), " tiny386 on %s   %s   ESP-IDF %s",
		 BOARD_NAME, app->version, esp_get_idf_version());
	console_set_title(s_title);
	console_printf(CON_INFO, "tiny386 for %s, version %s, ESP-IDF %s, ELF "
		       "%.16s", BOARD_NAME, app->version, esp_get_idf_version(),
		       esp_app_get_elf_sha256_str());

	bool bad;
	esp_reset_reason_t rr = esp_reset_reason();
	const char *why = reset_words(rr, &bad);
	console_printf(bad ? CON_WARN : CON_INFO, "Reset by %s.", why);

	esp_chip_info_t chip;
	esp_chip_info(&chip);
	console_printf(CON_INFO, "Chip: %s revision v%d.%d, %d cores, CPU %d MHz; "
		       "PSRAM %u KiB.", CONFIG_IDF_TARGET, chip.revision / 100,
		       chip.revision % 100, chip.cores,
		       CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
		       (unsigned) (esp_psram_get_size() / 1024));
	console_printf(CON_INFO, "Internal RAM free %u KiB (largest block %u "
		       "KiB); PSRAM free %u KiB.",
		       (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
		       (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
		       (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

	if (console_previous_lines() && (bad || console_previous_crashed()))
		console_show_previous(console_previous_crashed() ?
				      "the crash screen" : why);
}

static const char *part_type(uint8_t t)
{
	switch (t) {
	case 0x01: return "FAT12";
	case 0x04: case 0x06: case 0x0E: return "FAT16";
	case 0x0B: case 0x0C: return "FAT32";
	case 0x07: return "NTFS or exFAT";
	case 0x82: return "Linux swap";
	case 0x83: return "Linux";
	case 0xEE: return "GPT";
	default:   return "other";
	}
}

/* The card's partitions: the first FAT one is /sdcard; every other FAT one
   is mounted too (/sdcardN), so that the WIN9X partition of a card laid
   out for Linux can hold the Windows image. */
static void card_volumes(void)
{
	storage_sd_mount_others();
	int n;
	const struct storage_volume *v = storage_volumes(&n);
	for (int i = 0; i < n; i++) {
		char name[24] = "";
		if (v[i].label[0])
			snprintf(name, sizeof(name), " \"%s\"", v[i].label);
		if (v[i].mounted)
			console_printf(CON_GOOD, "Card partition %d: %s, %u MiB%s, at %s.",
				       v[i].part, v[i].type ? part_type(v[i].type) : "FAT",
				       (unsigned) v[i].mib, name, v[i].path);
		else if (v[i].fat)
			console_printf(CON_WARN, "Card partition %d: FAT, %u MiB%s, not "
				       "mounted: %s.", v[i].part, (unsigned) v[i].mib,
				       name, v[i].why ? v[i].why : "unknown");
		else
			console_printf(CON_INFO, "Card partition %d: %s (type %02X), %u "
				       "MiB, not FAT: not used.", v[i].part,
				       part_type(v[i].type), v[i].type, (unsigned) v[i].mib);
	}
	s_maps[1].to = storage_win_path();
	if (s_maps[1].to)
		console_printf(CON_INFO, "In an ini, /mnt/sd/... means /sdcard/... and "
			       "/mnt/win/... means %s/..., as on the Linux route.",
			       s_maps[1].to);
}

/* The card, tried every two seconds until it is there.  No card detect
   line on the Korvo-1: so a card can go in without a RESET. */
static void mount_storage(void)
{
	int got = storage_init();
	if (got & 1) {
		console_puts(CON_GOOD, "Card mounted at /sdcard.");
		status_led_stage(STATUS_STAGE_CARD);
		card_volumes();
		return;
	}
#ifdef SD_CLK
	if (got & 2) {
		console_puts(CON_INFO, "No card; using the flash's storage "
			     "partition (/spiflash).");
		status_led_stage(STATUS_STAGE_CARD);
		return;
	}
	status_led_error(STATUS_ERR_NO_CARD, true);
	console_puts(CON_ERROR, "No microSD card could be read. Put in a FAT32 "
		     "card with a tiny386 folder (the ini files, bios.bin, "
		     "vgabios.bin and the disk images) in its top folder. Trying "
		     "again every 2 seconds.");
	for (int tries = 1; ; tries++) {
		vTaskDelay(pdMS_TO_TICKS(2000));
		/* the drivers' own error lines once, not every two seconds */
		esp_log_level_set("*", ESP_LOG_NONE);
		bool ok = storage_sd_mount();
		esp_log_level_set("*", ESP_LOG_INFO);
		if (ok) {
			console_puts(CON_GOOD, "Card mounted at /sdcard.");
			status_led_error(STATUS_ERR_NO_CARD, false);
			status_led_stage(STATUS_STAGE_CARD);
			card_volumes();
			return;
		}
		if (tries % 15 == 0)
			console_printf(CON_WARN, "Still no card after %d tries. Is it "
				       "FAT32, and pushed fully in?", tries + 1);
	}
#else
	if (got & 2)
		console_puts(CON_INFO, "Using the flash's storage partition "
			     "(/spiflash).");
	status_led_stage(STATUS_STAGE_CARD);
#endif
}

/* ==== which ini ==== */

static const struct setup_place s_places[] = {
	{ "/sdcard/tiny386", NULL },
	{ "/sdcard", "tiny386.ini" },
	{ "/spiflash", "tiny386.ini" },
	{ NULL, NULL },
};

/* The ini to start from: the only one, or the one the boot menu gives.
   Returns true if the USB host was started for the menu. */
static bool choose_ini(void)
{
	/* 2 KiB: PSRAM, not internal RAM, and only while choosing */
	struct setup_list *inis = heap_caps_malloc(sizeof(*inis), MALLOC_CAP_SPIRAM);
	if (!inis)
		inis = malloc(sizeof(*inis));
	if (!inis || !setup_find(s_places, inis)) {
		status_led_fatal(inis ? STATUS_ERR_NO_INI : STATUS_ERR_STOPPED);
		console_fatal("There is no ini file on the card. Put a folder called "
			      "tiny386 in its top folder, with the ini files "
			      "(freedos.ini, ...), bios.bin, vgabios.bin and the "
			      "disk images, as the Linux route has it; or a "
			      "tiny386.ini in the top folder. Then press RESET.");
	}
	status_led_stage(STATUS_STAGE_INI);
	char last[SETUP_NAME_MAX];
	bool have_last = setup_last_read(inis->dir, last, sizeof(last));
	int pick = setup_default(inis, have_last ? last : NULL);
	bool usb = false;
	if (inis->n > 1) {
		/* the keyboard is needed now, before any ini says enable_usb */
		input_setup();
		usb = true;
		pick = boot_menu(inis, pick, s_title);
	}
	setup_path(inis, pick, s_ini_path, sizeof(s_ini_path));
	snprintf(s_ini_name, sizeof(s_ini_name), "%s", inis->ini[pick].name);
	snprintf(s_ini_dir, sizeof(s_ini_dir), "%s", inis->dir);
	config.filename = s_ini_path;
	free(inis);
	return usb;
}

void app_main(void)
{
	/* the status LED before anything else: white, or the last run's code
	   after a crash, a watchdog or a power dip.  GPIO37 drives the same
	   WS2812-protocol LED circuit on the Korvo-1 and on the DOS board, so
	   it may come before the DOS board's check. */
	status_led_init();
	global_event_group = xEventGroupCreate();
	/* the DOS board's check: on another board it stops here, before any
	   pin that is new on the DOS board has been driven */
	board_early_init();

	/* the panel first, so that everything after it can be seen (on the
	   DOS board this also brings the HDMI transmitter up) */
#ifdef LCD_ZERO_COPY
	void *fb = lcd_init();
	console_init(fb, LCD_WIDTH, LCD_HEIGHT, lcd_flush);
#else
	console_init(NULL, 0, 0, NULL);
#endif
	console_hooks_install();
	kbd_input_init();
	banner();
	status_led_report();
	status_led_stage(STATUS_STAGE_SCREEN);
	board_console_ready();
	/* the speed screen (speed.c), over the picture and back */
	speed_set_title(s_title);
	kbd_input_set_hotkey(HID_KEY_F12, speed_hotkey);
	/* v4.2: vbe_blocks changed while the PC runs */
	kbd_input_set_hotkey(HID_KEY_F11, speed_blocks_hotkey);
#ifdef LCD_ZERO_COPY
	/* v4.4: pictures at the guest's quiet moments, on or off */
	kbd_input_set_hotkey(HID_KEY_F10, speed_pace_hotkey);
#endif

#ifdef ESPDEBUG
	uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity	= UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	uart_param_config(UART_NUM_0, &uart_config);
	if (uart_driver_install(UART_NUM_0, 2 * 1024, 0, 0, NULL, 0) != ESP_OK) {
		assert(false);
	}
#endif

	i2s_main();
	mount_storage();

#if !CONFIG_SPIRAM_BOOT_INIT
	esp_psram_init();
#endif
#if defined(PSRAM_KEEP_FREE)
	// the largest PSRAM block, less what ESP-IDF needs
	psram_len = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
	psram_len = (psram_len - PSRAM_KEEP_FREE) & ~4095L;
	psram = psram_len > 0 ?
		heap_caps_aligned_alloc(4096, psram_len, MALLOC_CAP_SPIRAM) : NULL;
#elif !defined(PSRAM_ALLOC_LEN)
	// use the whole psram
	size_t len;
	psram = esp_psram_get(&len);
	psram_len = len;
#else
	psram_len = PSRAM_ALLOC_LEN;
	psram = heap_caps_calloc(1, psram_len, MALLOC_CAP_SPIRAM);
#endif
	if (!psram)
		console_fatal("Could not set aside the emulator's memory in PSRAM "
			      "(%u KiB free). This is a fault in this build.",
			      (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
	{
		long vga = round4k(256 * 1024 + VGA_MEM_SLACK);
		console_printf(CON_INFO, "Emulator memory: %ld KiB, room for mem_size "
			       "up to %ldM with vga_mem_size 256K.", psram_len / 1024,
			       (psram_len - vga) / (1024 * 1024));
	}

	bool usb_started = choose_ini();

	memset(&conf, 0, sizeof(conf));
	conf.mem_size = 0;              /* not given: the most that fits */
	conf.vga_mem_size = 256 * 1024;
	conf.cpu_gen = 4;
	conf.fpu = 0;
	console_printf(CON_INFO, "Settings from %s:", config.filename);
	int err = ini_parse(config.filename, parse_ini, NULL);
	if (err == -1) {
		status_led_fatal(STATUS_ERR_NO_INI);
		console_fatal("%s could not be opened.", config.filename);
	}
	if (err == -2)
		console_fatal("Out of memory while reading %s.", config.filename);
	if (err > 0)
		console_printf(CON_WARN, "%s line %d could not be read and was "
			       "skipped (and any others like it). A line is "
			       "[section], name = value, or a comment after ;",
			       s_ini_name, err);
	default_roms();
	settle_memory();

	if (conf.width != LCD_WIDTH || conf.height != LCD_HEIGHT) {
		if (conf.width || conf.height)
			console_printf(CON_INFO, "[display] %dx%d is ignored: the "
				       "panel is %dx%d.", conf.width, conf.height,
				       LCD_WIDTH, LCD_HEIGHT);
		conf.width = LCD_WIDTH;
		conf.height = LCD_HEIGHT;
	}

#ifdef TINY386_CRASH_TEST
	crash_test(config.crash_test);
#endif

	int bad = check_config();
	if (bad) {
		/* red 3 if a BIOS is among them, red 4 for the rest */
		status_led_fatal(s_bios_bad ? STATUS_ERR_BIOS : STATUS_ERR_INI);
		console_fatal("%d problem%s in %s, shown above in red.", bad,
			      bad == 1 ? "" : "s", s_ini_name);
	}

#ifdef LCD_ZERO_COPY
	lcd_set_invalidate(config.lcd_invalidate);
#endif

	/* the disk images and the guest's timer as the ini has them, and the
	   lines an emulator abort adds to the log (v3.8) */
	disk_image_fast_seek = config.fast_seek;
	disk_image_say = disk_image_line;
	i8254_max_owed = config.timer_catchup;
	cpu_abort_note = abort_note;
	if (config.timer_catchup != 1)
		console_printf(CON_INFO, "timer_catchup = %d: %s", config.timer_catchup,
			       config.timer_catchup ? "up to that many timer ticks missed "
			       "while the emulator waits are made up afterwards." :
			       "every timer tick missed while the emulator waits is made "
			       "up afterwards, as before v3.8 (Windows 95 can crash on that).");
	vga_dirty_blocks = config.vbe_blocks;
	if (!config.vbe_blocks)
		console_puts(CON_INFO, "vbe_blocks = 0: VBE pictures are drawn in full every "
			     "time, as before v4.0 (slower).");
#ifdef LCD_ZERO_COPY
	/* v4.4: pictures at the guest's quiet moments */
	vga_pace_quiet = config.pace_quiet;
	display_pace_set_wait(config.pace_wait_ms);
	speed_set_pace_on(config.pace_quiet ? config.pace_quiet : DISPLAY_PACE_QUIET_ON);
	if (config.pace_quiet)
		console_printf(CON_INFO, "pace_quiet = %d: a picture waits until the guest has stored "
			       "nothing into video memory for %d instructions, at most %d ms.",
			       config.pace_quiet, config.pace_quiet, config.pace_wait_ms);
#else
	if (config.pace_quiet)
		console_puts(CON_INFO, "pace_quiet is ignored: this board's drawing does not wait.");
#endif
	psram_test(config.memtest);

	if (config.enable_usb) {
		input_setup();          /* once: it may be running for the menu */
	} else if (usb_started) {
		console_puts(CON_INFO, "[esp] enable_usb = 0 is ignored: USB was "
			     "started for the boot menu, and stays on.");
	} else {
		console_puts(CON_WARN, "USB is off ([esp] enable_usb = 0 in the ini), so "
			     "there is no keyboard or mouse.");
	}

	if (config.ssid[0]) {
#ifdef NO_WIFI
		console_puts(CON_WARN, "Wi-Fi is not in this build: [esp] ssid is "
			     "ignored.");
#else
		wifi_main(config.ssid, config.pass);
#endif
	}

	if (xTaskCreatePinnedToCore(i386_task, "i386_main", 4096, &conf, 3, NULL, 1) != pdPASS ||
	    xTaskCreatePinnedToCore(vga_task, "vga_task", 4096, NULL, 0, NULL, 0) != pdPASS)
		console_fatal("The emulator's tasks could not be started: no memory "
			      "(%u KiB of internal RAM free).",
			      (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}
