/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
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
#include "common.h"
#include "console.h"
#ifdef LCD_ZERO_COPY
#include "lcd.h"
#endif

#ifndef BOARD_NAME
#define BOARD_NAME "an ESP32 board"
#endif

//
#include "esp_private/system_internal.h"
uint32_t get_uticks()
{
	return esp_system_get_time();
}

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
	if (!esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
				      ESP_PARTITION_SUBTYPE_ANY, file)) {
		snprintf(why, whylen, "\"%s\" (no / in front) means a flash "
			 "partition of that name, and there is none", file);
		return false;
	}
	return true;
}

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
	if (file && file[0] == '/') {
		FILE *fp = fopen(file, "rb");
		if (!fp)
			console_fatal("Cannot open %s (%s). Put it on the card, or "
				      "correct its line in tiny386.ini.", file,
				      strerror(errno));
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
		if (got != len)
			console_fatal("Could read only %d of the %d bytes of %s. "
				      "The card may be faulty.", got, len, file);
		return len;
	}
	const esp_partition_t *part =
		esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
					 ESP_PARTITION_SUBTYPE_ANY,
					 file);
	if (!part)
		console_fatal("There is no flash partition called \"%s\". To load "
			      "it from the card, give the whole path in tiny386.ini, "
			      "for example bios = /sdcard/bios.bin", file ? file : "");
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
int storage_init(void);
bool storage_sd_mount(void);
void usb_setup(void);

struct esp_ini_config {
	const char *filename;
	char ssid[32];
	char pass[64];
	int enable_usb;
	int console_s;          /* seconds the boot console stays up */
	int lcd_invalidate;
	int crash_test;         /* only read in a TINY386_CRASH_TEST build */
};

#ifdef TINY386_CRASH_TEST
/* [esp] crash_test, in a test build only (idf.py -DCRASH_TEST=1): makes
   the crash screen appear on purpose.  1 abort(), 2 a store to address 0,
   3 a failed assert, all on core 0 before the PC starts; 4 abort() on
   core 1 three seconds after the PC has started, over its picture. */
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
	}
}
#endif

static struct esp_ini_config config = {
	.console_s = 2,
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
			      "tiny386.ini.", size / 1024, psram_off / 1024,
			      psram_len / 1024);
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
	console_printf(CON_INFO, "Device memory (the 0xA0000 window): %ld of %ld "
		       "bytes used.", pcram_off, pcram_len);

	load_bios_and_reset(pc);

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
		     "This log goes on on the UART.");
	console_release_display();
	xEventGroupSetBits(global_event_group, BIT0);

	pc->boot_start_time = get_uticks();
#ifdef TINY386_CRASH_TEST
	if (config.crash_test == 4) {
		while (get_uticks() - pc->boot_start_time < 3000000)
			pc_step(pc);
		console_puts(CON_WARN, "crash_test = 4: crashing on purpose.");
		abort();
	}
#endif
	for (; pc->shutdown_state != 8;) {
		pc_step(pc);
	}
	console_puts(CON_INFO, "The PC was switched off. Press RESET to start again.");
	console_take_display();
	vTaskDelete(NULL);
}

/* ---- the ini file ---- */

static const char *const known_keys[] = {
	"esp:ssid", "esp:pass", "esp:enable_usb", "esp:console",
	"esp:lcd_invalidate",
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

static int parse_esp_ini(struct esp_ini_config *conf,
			 const char *name, const char *value)
{
#define NAME(a) (strcmp(name, a) == 0)
	if (NAME("ssid")) {
		if (strlen(value) < sizeof(conf->ssid))
			strcpy(conf->ssid, value);
		else
			console_printf(CON_WARN, "tiny386.ini: ssid is longer than "
				       "%d characters; ignored.", (int) sizeof(conf->ssid) - 1);
	} else if (NAME("pass")) {
		if (strlen(value) < sizeof(conf->pass))
			strcpy(conf->pass, value);
		else
			console_printf(CON_WARN, "tiny386.ini: pass is longer than "
				       "%d characters; ignored.", (int) sizeof(conf->pass) - 1);
	} else if (NAME("enable_usb")) {
		conf->enable_usb = atoi(value);
	} else if (NAME("console")) {
		conf->console_s = atoi(value);
	} else if (NAME("lcd_invalidate")) {
		conf->lcd_invalidate = atoi(value);
	} else if (NAME("crash_test")) {
		conf->crash_test = atoi(value);
	}
#undef NAME
	return 1;
}

/* every key: shown, checked against the list, then handed on */
static int parse_ini(void *user, const char *section,
		     const char *name, const char *value)
{
	(void) user;
	bool secret = !strcmp(section, "esp") && !strcmp(name, "pass");
	console_printf(CON_INFO, "  [%s] %s = %s", section, name,
		       secret ? "(not shown)" : value);
	if (!known_key(section, name)) {
		console_printf(CON_WARN, "tiny386.ini: [%s] %s is not a setting "
			       "tiny386 knows; it was ignored. Check the spelling "
			       "and the [section] it is under.", section, name);
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

/* Everything the ini names, before the PC is built: anything wrong is
   said here in plain words, rather than an abort later. */
static int check_config(void)
{
	int bad = 0;
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
			       "from the tiny386 release on the card and write "
			       "bios = /sdcard/bios.bin under [pc].", why);
		bad++;
	}
	if (!rom_exists(vga_bios, why, sizeof(why))) {
		console_printf(CON_ERROR, "The VGA BIOS cannot be loaded: %s. Put "
			       "vgabios.bin on the card and write vga_bios = "
			       "/sdcard/vgabios.bin under [pc].", why);
		bad++;
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
		struct stat st;
		if (stat(path, &st) != 0) {
			console_printf(CON_ERROR, "[pc] %s = %s: there is no such file "
				       "on the card. Check the name (and the folder) "
				       "in tiny386.ini.", key, path);
			bad++;
		} else {
			console_printf(CON_INFO, "%s: %s, %ld KiB", key, path,
				       (long) (st.st_size / 1024));
		}
	}
	if (!disks)
		console_puts(CON_WARN, "No disk image is set (hda = /sdcard/... "
			     "under [pc]): the PC will have nothing to start "
			     "from but the network or a floppy.");

	long vga = round4k(conf.vga_mem_size + VGA_MEM_SLACK);
	long need = round4k(conf.mem_size) + vga;
	long fits_mb = (psram_len - vga) / (1024 * 1024);
	if (conf.mem_size < 1024 * 1024) {
		console_printf(CON_ERROR, "[pc] mem_size = %ld KiB is too small: give "
			       "at least 1M.", conf.mem_size / 1024);
		bad++;
	} else if (need > psram_len) {
		console_printf(CON_ERROR, "mem_size %ld KiB and vga_mem_size %ld KiB "
			       "need %ld KiB, but the emulator has %ld KiB. Lower "
			       "mem_size in tiny386.ini: %ldM fits.",
			       conf.mem_size / 1024, conf.vga_mem_size / 1024,
			       need / 1024, psram_len / 1024, fits_mb);
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

static void banner(void)
{
	const esp_app_desc_t *app = esp_app_get_description();
	char title[160];
	snprintf(title, sizeof(title), " tiny386 on %s   %s   ESP-IDF %s",
		 BOARD_NAME, app->version, esp_get_idf_version());
	console_set_title(title);
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

/* The card, tried every two seconds until it is there.  No card detect
   line on the Korvo-1: so a card can go in without a RESET. */
static void mount_storage(void)
{
	int got = storage_init();
	if (got & 1) {
		console_puts(CON_GOOD, "Card mounted at /sdcard.");
		return;
	}
#ifdef SD_CLK
	if (got & 2) {
		console_puts(CON_INFO, "No card; using the flash's storage "
			     "partition (/spiflash).");
		return;
	}
	console_puts(CON_ERROR, "No microSD card could be read. Put in a FAT32 "
		     "card with tiny386.ini, bios.bin, vgabios.bin and the disk "
		     "images in its top folder. Trying again every 2 seconds.");
	for (int tries = 1; ; tries++) {
		vTaskDelay(pdMS_TO_TICKS(2000));
		/* the drivers' own error lines once, not every two seconds */
		esp_log_level_set("*", ESP_LOG_NONE);
		bool ok = storage_sd_mount();
		esp_log_level_set("*", ESP_LOG_INFO);
		if (ok) {
			console_puts(CON_GOOD, "Card mounted at /sdcard.");
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
#endif
}

void app_main(void)
{
	global_event_group = xEventGroupCreate();

	/* the panel first, so that everything after it can be seen */
#ifdef LCD_ZERO_COPY
	void *fb = lcd_init();
	console_init(fb, LCD_WIDTH, LCD_HEIGHT, lcd_flush);
#else
	console_init(NULL, 0, 0, NULL);
#endif
	console_hooks_install();
	banner();

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
	// the largest PSRAM block, less what the panel and ESP-IDF need
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

	const static char *files[] = {
		"/sdcard/tiny386.ini",
		"/spiflash/tiny386.ini",
		NULL,
	};
	for (int i = 0; files[i]; i++) {
		if (access(files[i], R_OK) == 0) {
			config.filename = files[i];
			break;
		}
	}
	if (!config.filename)
		console_fatal("There is no tiny386.ini in the top folder of the card. "
			      "Put one there (FLASH.txt has a sample) and press RESET.");

	memset(&conf, 0, sizeof(conf));
	conf.mem_size = 8 * 1024 * 1024;
	conf.vga_mem_size = 256 * 1024;
	conf.cpu_gen = 4;
	conf.fpu = 0;
	console_printf(CON_INFO, "Settings from %s:", config.filename);
	int err = ini_parse(config.filename, parse_ini, NULL);
	if (err == -1)
		console_fatal("%s could not be opened.", config.filename);
	if (err == -2)
		console_fatal("Out of memory while reading %s.", config.filename);
	if (err > 0)
		console_printf(CON_WARN, "tiny386.ini line %d could not be read and "
			       "was skipped (and any others like it). A line is "
			       "[section], name = value, or a comment after ;", err);

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
	if (bad)
		console_fatal("%d problem%s in tiny386.ini, shown above in red.", bad,
			      bad == 1 ? "" : "s");

#ifdef LCD_ZERO_COPY
	lcd_set_invalidate(config.lcd_invalidate);
#endif

	if (config.enable_usb) {
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		usb_setup();
	} else {
		console_puts(CON_WARN, "USB is off, so there is no keyboard: write "
			     "enable_usb = 1 under [esp] in tiny386.ini.");
	}

	if (config.ssid[0]) {
#ifdef NO_WIFI
		console_puts(CON_WARN, "Wi-Fi is not in this build: [esp] ssid is "
			     "ignored.");
#else
		wifi_main(config.ssid, config.pass);
#endif
	}

	xTaskCreatePinnedToCore(i386_task, "i386_main", 4096, &conf, 3, NULL, 1);
	xTaskCreatePinnedToCore(vga_task, "vga_task", 4096, NULL, 0, NULL, 0);
}
