#ifdef IDE_BOARD_DISK
/*
 * The ESP-Mosaico's SPI NAND, a GigaDevice GD5F1GM7UE (1 Gbit: 1,024
 * blocks of 64 pages of 2,048 bytes and a 128-byte spare area, ECC on the
 * chip), on SPI3 through the GPIO matrix, one wire at 40 MHz for now (HOLD
 * and WP held high).
 *
 * The guest's disk: ESP-IDF's spi_nand_flash 1.4.4, whose Dhara map does
 * the wear levelling and bad blocks (gc factor 45, its default), under
 * nand_vol.c.  Dhara fills a page never written with FFh; here it reads as
 * zeros (__wrap_dhara_map_read() below), so that nand_vol.c can trim zero
 * pages.  The disk is a fixed NAND_DISK_PAGES - 1 pages, whatever the chip:
 * Dhara's room with 22 bad blocks, beyond the 20 GigaDevice's 1 Gbit parts
 * allow, so the size never changes and a chip with fewer leaves the map a
 * little more room.
 *
 * The chip raw, for USB disk mode (usb_disk.c): every page with its spare
 * area, read and written by the chip's own commands while the map is
 * stopped, for a backup of what was on it before (the factory files) and
 * putting it back.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "spi_nand_flash.h"
#include "dhara/map.h"
#include "dhara/error.h"

#include "esp_timer.h"
#include "console.h"
#include "board_disk.h"
#include "disk_image.h"
#include "nand_disk.h"
#include "nand_vol.h"

static const char *TAG = "nand";

#define NAND_MHZ 40
#define NAND_HOST SPI3_HOST
#define BLOCKS 1024
#define PPB 64
#define RAW_PAGE (2048 + 128)
#define RAW_BLOCK (RAW_PAGE * PPB)              /* 139,264 bytes, 272 sectors */
#define RAW_BYTES ((uint64_t) RAW_BLOCK * BLOCKS)
/* Dhara with 22 bad blocks: (1024 - 22 - 1) * 60 = 60,060 pages, less
   60,060 / 46 and 8 blocks' worth: 58,243.  The last is the signature. */
#define NAND_DISK_PAGES 58243u

static spi_device_handle_t s_dev;
static spi_nand_flash_device_t *s_nand;
static enum nand_state s_state = NAND_ABSENT;
static bool s_started;
static char s_why[120];
static uint32_t s_map_pages;
static struct nand_vol s_vol;
static uint8_t *s_vol_page;
static SemaphoreHandle_t s_lock;

/* ==== a page never written reads as zeros ==== */

int __real_dhara_map_read(struct dhara_map *m, dhara_sector_t s, uint8_t *data,
			  dhara_error_t *err);
int __wrap_dhara_map_read(struct dhara_map *m, dhara_sector_t s, uint8_t *data,
			  dhara_error_t *err)
{
	const struct dhara_nand *n = m->journal.nand;
	dhara_error_t my_err;
	dhara_page_t p;
	if (dhara_map_find(m, s, &p, &my_err) < 0) {
		if (my_err == DHARA_E_NOT_FOUND) {
			memset(data, 0, (size_t) 1 << n->log2_page_size);
			return 0;
		}
		dhara_set_error(err, my_err);
		return -1;
	}
	return dhara_nand_read(n, p, 0, (size_t) 1 << n->log2_page_size, data, err);
}

/* ==== the bus and the map ==== */

static esp_err_t bus_up(void)
{
	const gpio_config_t ctl = {
		.pin_bit_mask = (1ULL << MOSAICO_NAND_HOLD) | (1ULL << MOSAICO_NAND_WP),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};
	gpio_set_level(MOSAICO_NAND_HOLD, 1);
	gpio_set_level(MOSAICO_NAND_WP, 1);
	gpio_config(&ctl);
	gpio_set_level(MOSAICO_NAND_HOLD, 1);
	gpio_set_level(MOSAICO_NAND_WP, 1);
	const spi_bus_config_t bus = {
		.mosi_io_num = MOSAICO_NAND_D,
		.miso_io_num = MOSAICO_NAND_Q,
		.sclk_io_num = MOSAICO_NAND_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4096,
	};
	esp_err_t err = spi_bus_initialize(NAND_HOST, &bus, SPI_DMA_CH_AUTO);
	if (err != ESP_OK)
		return err;
	const spi_device_interface_config_t dev = {
		.clock_speed_hz = NAND_MHZ * 1000 * 1000,
		.mode = 0,
		.spics_io_num = MOSAICO_NAND_CS,
		.queue_size = 10,
		.flags = SPI_DEVICE_HALFDUPLEX,
	};
	return spi_bus_add_device(NAND_HOST, &dev, &s_dev);
}

static esp_err_t map_up(void)
{
	spi_nand_flash_config_t cfg = {
		.device_handle = s_dev,
		.gc_factor = 0,                 /* the default, 45 */
		.io_mode = SPI_NAND_IO_MODE_SIO,
		.flags = SPI_DEVICE_HALFDUPLEX,
	};
	esp_err_t err = spi_nand_flash_init_device(&cfg, &s_nand);
	if (err != ESP_OK) {
		s_nand = NULL;
		return err;
	}
	uint32_t n = 0, size = 0;
	spi_nand_flash_get_page_count(s_nand, &n);
	spi_nand_flash_get_page_size(s_nand, &size);
	if (size != NAND_VOL_PAGE) {
		ESP_LOGE(TAG, "pages of %" PRIu32 " bytes, not 2048", size);
		spi_nand_flash_deinit_device(s_nand);
		s_nand = NULL;
		return ESP_ERR_NOT_SUPPORTED;
	}
	s_map_pages = n;
	return ESP_OK;
}

static void map_down(void)
{
	if (s_nand) {
		spi_nand_flash_deinit_device(s_nand);
		s_nand = NULL;
	}
}

static int pg_read(void *c, uint32_t p, uint8_t *b)
{
	(void) c;
	return spi_nand_flash_read_page(s_nand, b, p) == ESP_OK ? 0 : -1;
}

static int pg_write(void *c, uint32_t p, const uint8_t *b)
{
	(void) c;
	return spi_nand_flash_write_page(s_nand, b, p) == ESP_OK ? 0 : -1;
}

static int pg_trim(void *c, uint32_t p)
{
	(void) c;
	return spi_nand_flash_trim(s_nand, p) == ESP_OK ? 0 : -1;
}

static int pg_sync(void *c)
{
	(void) c;
	return spi_nand_flash_sync(s_nand) == ESP_OK ? 0 : -1;
}

static struct nand_pages s_pages = {
	.read = pg_read, .write = pg_write, .trim = pg_trim, .sync = pg_sync,
};

enum nand_state nand_disk_start(char *why, size_t size)
{
	if (!s_started) {
		s_started = true;
		s_lock = xSemaphoreCreateMutex();
		s_vol_page = heap_caps_malloc(NAND_VOL_PAGE, MALLOC_CAP_SPIRAM);
		esp_err_t err = (s_lock && s_vol_page) ? bus_up() : ESP_ERR_NO_MEM;
		if (err != ESP_OK) {
			snprintf(s_why, sizeof(s_why), "its SPI bus would not start (%s)",
				 esp_err_to_name(err));
		} else if ((err = map_up()) != ESP_OK) {
			snprintf(s_why, sizeof(s_why), "the chip did not answer as a "
				 "GD5F1GM7 (%s)", esp_err_to_name(err));
		} else if (s_map_pages < NAND_DISK_PAGES) {
			snprintf(s_why, sizeof(s_why), "it has room for %" PRIu32 " pages, "
				 "fewer than the %u the disk needs: too many bad blocks",
				 s_map_pages, NAND_DISK_PAGES);
		} else {
			s_pages.pages = NAND_DISK_PAGES;
			int r = nand_vol_open(&s_vol, &s_pages, s_vol_page);
			if (r == NAND_VOL_OK)
				s_state = NAND_OURS;
			else if (r == NAND_VOL_NOT_OURS)
				s_state = NAND_OTHER;
			else
				snprintf(s_why, sizeof(s_why), "its last page could not be read");
		}
		if (s_state == NAND_ABSENT)
			ESP_LOGE(TAG, "%s", s_why);
	}
	if (why && size)
		snprintf(why, size, "%s", s_why);
	return s_state;
}

uint64_t nand_disk_bytes(void)
{
	return s_state == NAND_OURS ? nand_vol_sectors(&s_vol) * 512 : 0;
}

uint32_t nand_disk_map_pages(void)
{
	return s_map_pages;
}

/* ==== the chip raw ==== */

static uint8_t *s_raw;                  /* a page and its spare area, DMA */
static uint32_t s_raw_page = UINT32_MAX;        /* what s_raw holds */
static uint32_t s_raw_blocks, s_raw_skipped, s_raw_ecc;
static int s_bad = -1;

static esp_err_t raw_tx(uint8_t cmd, uint32_t addr, int abits, int dbits,
			const void *tx, size_t txn, void *rx, size_t rxn)
{
	spi_transaction_ext_t t;
	memset(&t, 0, sizeof(t));
	t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
	t.base.cmd = cmd;
	t.base.addr = addr;
	t.command_bits = 8;
	t.address_bits = abits;
	t.dummy_bits = dbits;
	if (txn) {
		t.base.length = txn * 8;
		if (txn <= 4) {
			t.base.flags |= SPI_TRANS_USE_TXDATA;
			memcpy(t.base.tx_data, tx, txn);
		} else {
			t.base.tx_buffer = tx;
		}
	}
	if (rxn) {
		t.base.rxlength = rxn * 8;
		if (rxn <= 4)
			t.base.flags |= SPI_TRANS_USE_RXDATA;
		else
			t.base.rx_buffer = rx;
	}
	esp_err_t err = spi_device_polling_transmit(s_dev, &t.base);
	if (err == ESP_OK && rxn && rxn <= 4)
		memcpy(rx, t.base.rx_data, rxn);
	return err;
}

/* the status register once the chip is no longer busy (C0h; bit 0 OIP) */
static int raw_wait(uint8_t *st)
{
	for (int i = 0; i < 20000; i++) {
		uint8_t v;
		if (raw_tx(0x0F, 0xC0, 8, 0, NULL, 0, &v, 1) != ESP_OK)
			return -1;
		if (!(v & 1)) {
			*st = v;
			return 0;
		}
		if (i > 8)
			esp_rom_delay_us(20);
	}
	return -1;
}

static int raw_start(void)
{
	if (!s_raw)
		s_raw = heap_caps_malloc(RAW_PAGE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	if (!s_raw)
		return -1;
	/* the map lets go of the chip; every block unprotected (A0h) */
	map_down();
	uint8_t zero = 0, st;
	if (raw_tx(0x1F, 0xA0, 8, 0, &zero, 1, NULL, 0) != ESP_OK || raw_wait(&st))
		return -1;
	return 0;
}

static int raw_read_page(uint32_t page)
{
	if (s_raw_page == page)
		return 0;
	s_raw_page = UINT32_MAX;
	uint8_t st;
	if (raw_tx(0x13, page, 24, 0, NULL, 0, NULL, 0) != ESP_OK || raw_wait(&st))
		return -1;
	if (((st >> 4) & 3) == 2)
		s_raw_ecc++;            /* uncorrectable: kept as read */
	if (raw_tx(0x0B, 0, 16, 8, NULL, 0, s_raw, RAW_PAGE) != ESP_OK)
		return -1;
	s_raw_page = page;
	return 0;
}

/* a bad block: the first two spare bytes of its first page not FFh, as
   spi_nand_flash reads it (nand_impl.c, nand_is_bad()) */
static bool raw_block_bad(uint32_t block)
{
	if (raw_read_page(block * PPB))
		return true;
	return s_raw[2048] != 0xFF || s_raw[2049] != 0xFF;
}

int nand_raw_bad_blocks(void)
{
	return s_bad;
}

static int raw_erase(uint32_t block)
{
	uint8_t st;
	s_raw_page = UINT32_MAX;
	if (raw_tx(0x06, 0, 0, 0, NULL, 0, NULL, 0) != ESP_OK ||
	    raw_tx(0xD8, block * PPB, 24, 0, NULL, 0, NULL, 0) != ESP_OK || raw_wait(&st))
		return -1;
	return (st & 0x04) ? -1 : 0;
}

static int raw_program(uint32_t page, const uint8_t *src)
{
	uint8_t st;
	s_raw_page = UINT32_MAX;
	memcpy(s_raw, src, RAW_PAGE);
	if (raw_tx(0x06, 0, 0, 0, NULL, 0, NULL, 0) != ESP_OK ||
	    raw_tx(0x02, 0, 16, 0, s_raw, RAW_PAGE, NULL, 0) != ESP_OK ||
	    raw_tx(0x10, page, 24, 0, NULL, 0, NULL, 0) != ESP_OK || raw_wait(&st))
		return -1;
	return (st & 0x08) ? -1 : 0;
}

/* ==== formatting ==== */

int nand_disk_format(char *why, size_t size)
{
	int r = -1;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	if (!s_nand && map_up() != ESP_OK) {
		snprintf(why, size, "the chip did not answer");
		goto out;
	}
	ESP_LOGW(TAG, "erasing the whole NAND");
	if (spi_nand_erase_chip(s_nand) != ESP_OK) {
		snprintf(why, size, "the chip could not be erased");
		goto out;
	}
	/* a fresh map over the erased chip */
	map_down();
	if (map_up() != ESP_OK || s_map_pages < NAND_DISK_PAGES) {
		snprintf(why, size, "the map would not start again after the erase");
		goto out;
	}
	s_pages.pages = NAND_DISK_PAGES;
	if (nand_vol_format(&s_vol, &s_pages, s_vol_page) != 0) {
		snprintf(why, size, "the signature could not be written");
		goto out;
	}
	s_state = NAND_OURS;
	r = 0;
out:
	xSemaphoreGive(s_lock);
	return r;
}

/* ==== the guest's disk (board_disk.h) ==== */

bool board_disk_name(const char *name)
{
	return strcasecmp(name, "nand:") == 0;
}

int board_disk_check(int drive, bool iscd, char *info, size_t isize, char *why, size_t wsize)
{
	if (iscd || drive >= 4) {
		snprintf(why, wsize, "the NAND can only be a hard disk (hda to hdd)");
		return -1;
	}
	char w[120];
	switch (nand_disk_start(w, sizeof(w))) {
	case NAND_OURS:
		snprintf(info, isize, "the NAND, %" PRIu64 " sectors (%" PRIu64 " KiB)",
			 nand_vol_sectors(&s_vol), nand_vol_sectors(&s_vol) / 2);
		return 0;
	case NAND_OTHER:
		snprintf(why, wsize, "the NAND is not set up for tiny386 yet. Hold the AI "
			 "button while switching the board on, for USB disk mode: it "
			 "saves what is on the NAND and then makes it the Windows disk");
		return -1;
	default:
		snprintf(why, wsize, "the NAND cannot be used: %s", w);
		return -1;
	}
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* A partition that DOS reads by cylinder, head and sector but that runs
   past what the BIOS reaches that way.  ide.c gives the disk 16 heads of
   63 sectors and as many whole cylinders as fit, 231 here (232,848 of its
   232,968 sectors), so its last sectors cannot be read: ScanDisk stops at
   the last cluster, at every start it runs.  mkimage-mosaico.py ends the
   partition on a cylinder since 16 November 2026.  LBA partitions (0Ch,
   0Eh, 0Fh) are read past the cylinders and are left alone. */
static void check_partitions(void)
{
	uint8_t *mbr = malloc(512);
	if (!mbr)
		return;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = nand_vol_read(&s_vol, 0, mbr, 1);
	xSemaphoreGive(s_lock);
	uint64_t total = nand_vol_sectors(&s_vol);
	uint64_t cyl = total / (16 * 63);
	if (cyl > 16383)
		cyl = 16383;
	uint64_t reach = cyl * 16 * 63;
	for (int i = 0; r == 0 && mbr[510] == 0x55 && mbr[511] == 0xAA && i < 4; i++) {
		const uint8_t *e = mbr + 446 + 16 * i;
		uint8_t type = e[4];
		uint64_t end = (uint64_t) le32(e + 8) + le32(e + 12);
		if (type == 0 || type == 0x0C || type == 0x0E || type == 0x0F || end <= reach)
			continue;
		console_printf(CON_WARN, "The NAND disk's partition %d runs to sector %" PRIu64 ", past "
			       "the %" PRIu64 " sectors the BIOS reaches (%u cylinders of 16 heads and 63 "
			       "sectors): DOS cannot read its last %" PRIu64 " sectors, and ScanDisk stops at "
			       "its last cluster. Images from mkimage-mosaico.py since 16 November 2026 "
			       "end within reach.",
			       i + 1, end, reach, (unsigned) cyl, end - reach);
	}
	free(mbr);
}

void *board_disk_open(const char *name, char *why, size_t size)
{
	int info_ok;
	char info[120];
	(void) name;
	info_ok = board_disk_check(0, false, info, sizeof(info), why, size);
	if (info_ok != 0)
		return NULL;
	check_partitions();
	return &s_vol;
}

uint64_t board_disk_sectors(void *d)
{
	return nand_vol_sectors(d);
}

/* The guest's reads and writes are counted with the card's
   (disk_image_count()): the speed screen's Disk line and the information
   port's disk figures (perfport.h) are the NAND's on this board. */
int board_disk_read(void *d, uint64_t lba, void *buf, unsigned n)
{
	int64_t t0 = esp_timer_get_time();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = nand_vol_read(d, lba, buf, n);
	xSemaphoreGive(s_lock);
	disk_image_count(false, n, (uint32_t) (esp_timer_get_time() - t0), r != 0, false);
	return r;
}

int board_disk_write(void *d, uint64_t lba, const void *buf, unsigned n)
{
	int64_t t0 = esp_timer_get_time();
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = nand_vol_write(d, lba, buf, n);
	xSemaphoreGive(s_lock);
	disk_image_count(true, n, (uint32_t) (esp_timer_get_time() - t0), r != 0, false);
	return r;
}

void nand_disk_sync(void)
{
	if (s_state == NAND_OURS && s_lock)
		board_disk_sync(&s_vol);
}

int board_disk_sync(void *d)
{
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = nand_vol_sync(d);
	xSemaphoreGive(s_lock);
	return r;
}

/* ==== block devices for USB disk mode ==== */

static esp_err_t vol_read(esp_blockdev_handle_t h, uint8_t *dst, size_t dst_size,
			  uint64_t addr, size_t len)
{
	(void) h; (void) dst_size;
	return board_disk_read(&s_vol, addr / 512, dst, len / 512) ? ESP_FAIL : ESP_OK;
}

static esp_err_t vol_write(esp_blockdev_handle_t h, const uint8_t *src, uint64_t addr,
			   size_t len)
{
	(void) h;
	return board_disk_write(&s_vol, addr / 512, src, len / 512) ? ESP_FAIL : ESP_OK;
}

static esp_err_t no_erase(esp_blockdev_handle_t h, uint64_t a, size_t n)
{
	(void) h; (void) a; (void) n;
	return ESP_OK;
}

static esp_err_t vol_sync(esp_blockdev_handle_t h)
{
	(void) h;
	return board_disk_sync(&s_vol) ? ESP_FAIL : ESP_OK;
}

static esp_err_t no_ioctl(esp_blockdev_handle_t h, const uint8_t cmd, void *args)
{
	(void) h; (void) cmd; (void) args;
	return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t no_release(esp_blockdev_handle_t h)
{
	(void) h;
	return ESP_OK;
}

static const esp_blockdev_ops_t s_vol_ops = {
	.read = vol_read, .write = vol_write, .erase = no_erase,
	.sync = vol_sync, .ioctl = no_ioctl, .release = no_release,
};
static esp_blockdev_t s_vol_bd;

esp_blockdev_handle_t nand_disk_blockdev(void)
{
	memset(&s_vol_bd, 0, sizeof(s_vol_bd));
	s_vol_bd.ops = &s_vol_ops;
	s_vol_bd.geometry.disk_size = nand_vol_sectors(&s_vol) * 512;
	s_vol_bd.geometry.read_size = 512;
	s_vol_bd.geometry.write_size = 512;
	s_vol_bd.geometry.erase_size = 512;
	return &s_vol_bd;
}

/* The raw chip.  A write gathers a whole block (272 sectors) before the
   block is erased and its pages programmed (all-FFh pages left erased, a
   block the chip marks bad left alone); a block left part written when
   another is started, or at a sync, is filled from the chip first. */
static uint8_t *s_rblk;                 /* a block, data and spare areas */
static uint32_t s_rblk_no = UINT32_MAX;
static uint8_t s_rgot[RAW_BLOCK / 512 / 8];     /* its sectors written */

static esp_err_t raw_read(esp_blockdev_handle_t h, uint8_t *dst, size_t dst_size,
			  uint64_t addr, size_t len)
{
	(void) h; (void) dst_size;
	esp_err_t err = ESP_OK;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	while (len) {
		uint32_t page = (uint32_t) (addr / RAW_PAGE);
		uint32_t off = (uint32_t) (addr % RAW_PAGE);
		size_t k = RAW_PAGE - off;
		if (k > len)
			k = len;
		if (s_rblk_no == page / PPB) {
			/* a block being written back: what it will hold */
			memcpy(dst, s_rblk + (size_t) (page % PPB) * RAW_PAGE + off, k);
		} else if (raw_read_page(page) == 0) {
			memcpy(dst, s_raw + off, k);
		} else {
			err = ESP_FAIL;
			break;
		}
		dst += k;
		addr += k;
		len -= k;
	}
	xSemaphoreGive(s_lock);
	return err;
}

static bool all_ff(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != 0xFF)
			return false;
	return true;
}

/* the buffered block to the chip, its missing sectors from the chip first */
static int raw_put_block(void)
{
	uint32_t b = s_rblk_no;
	if (b == UINT32_MAX)
		return 0;
	s_rblk_no = UINT32_MAX;
	int r = 0;
	for (uint32_t p = 0; p < PPB; p++) {
		/* any sector touching page p not written: the page from the chip
		   for those bytes */
		uint32_t s0 = p * RAW_PAGE / 512, s1 = ((p + 1) * RAW_PAGE - 1) / 512;
		bool missing = false;
		for (uint32_t s = s0; s <= s1; s++)
			if (!(s_rgot[s / 8] & (1 << (s % 8))))
				missing = true;
		if (!missing)
			continue;
		if (raw_read_page(b * PPB + p))
			return -1;
		for (uint32_t s = s0; s <= s1; s++) {
			if (s_rgot[s / 8] & (1 << (s % 8)))
				continue;
			uint32_t lo = s * 512 > p * RAW_PAGE ? s * 512 : p * RAW_PAGE;
			uint32_t hi = (s + 1) * 512 < (p + 1) * RAW_PAGE ? (s + 1) * 512 : (p + 1) * RAW_PAGE;
			memcpy(s_rblk + lo, s_raw + (lo - p * RAW_PAGE), hi - lo);
		}
	}
	s_raw_blocks++;
	if (raw_block_bad(b)) {
		s_raw_skipped++;
		return 0;
	}
	if (raw_erase(b))
		return -1;
	for (uint32_t p = 0; p < PPB; p++) {
		const uint8_t *src = s_rblk + (size_t) p * RAW_PAGE;
		if (!all_ff(src, RAW_PAGE) && raw_program(b * PPB + p, src))
			r = -1;
	}
	return r;
}

/* the chip raw takes writes only in restore mode (nand_raw_blockdev()) */
static bool s_raw_rw;

static esp_err_t raw_write(esp_blockdev_handle_t h, const uint8_t *src, uint64_t addr,
			   size_t len)
{
	(void) h;
	esp_err_t err = ESP_OK;
	if (!s_raw_rw)
		return ESP_ERR_INVALID_STATE;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	if (!s_rblk)
		s_rblk = heap_caps_malloc(RAW_BLOCK, MALLOC_CAP_SPIRAM);
	if (!s_rblk) {
		xSemaphoreGive(s_lock);
		return ESP_ERR_NO_MEM;
	}
	while (len && err == ESP_OK) {
		uint32_t b = (uint32_t) (addr / RAW_BLOCK);
		uint32_t off = (uint32_t) (addr % RAW_BLOCK);
		size_t k = RAW_BLOCK - off;
		if (k > len)
			k = len;
		if (b != s_rblk_no) {
			if (raw_put_block())
				err = ESP_FAIL;
			s_rblk_no = b;
			memset(s_rgot, 0, sizeof(s_rgot));
		}
		memcpy(s_rblk + off, src, k);
		for (uint32_t s = off / 512; s < (off + k) / 512; s++)
			s_rgot[s / 8] |= 1 << (s % 8);
		bool whole = true;
		for (size_t i = 0; i < sizeof(s_rgot); i++)
			if (s_rgot[i] != 0xFF)
				whole = false;
		if (whole && raw_put_block())
			err = ESP_FAIL;
		src += k;
		addr += k;
		len -= k;
	}
	xSemaphoreGive(s_lock);
	return err;
}

static esp_err_t raw_sync(esp_blockdev_handle_t h)
{
	(void) h;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = raw_put_block();
	xSemaphoreGive(s_lock);
	return r ? ESP_FAIL : ESP_OK;
}

static const esp_blockdev_ops_t s_raw_ops = {
	.read = raw_read, .write = raw_write, .erase = no_erase,
	.sync = raw_sync, .ioctl = no_ioctl, .release = no_release,
};
static esp_blockdev_t s_raw_bd;

esp_blockdev_handle_t nand_raw_blockdev(bool writable)
{
	if (!s_started)
		nand_disk_start(NULL, 0);
	xSemaphoreTake(s_lock, portMAX_DELAY);
	int r = raw_start();
	if (r == 0 && s_bad < 0) {
		s_bad = 0;
		for (uint32_t b = 0; b < BLOCKS; b++)
			if (raw_block_bad(b))
				s_bad++;
	}
	xSemaphoreGive(s_lock);
	if (r)
		return NULL;
	memset(&s_raw_bd, 0, sizeof(s_raw_bd));
	s_raw_bd.ops = &s_raw_ops;
	s_raw_bd.geometry.disk_size = RAW_BYTES;
	s_raw_bd.geometry.read_size = 512;
	/* esp_tinyusb 3.0.1 refuses a read-only block device
	   (storage_blockdev_open_medium()), so the backup drive is offered
	   as writable and raw_write() refuses its writes */
	s_raw_bd.geometry.write_size = 512;
	s_raw_bd.geometry.erase_size = 512;
	s_raw_rw = writable;
	return &s_raw_bd;
}

void nand_raw_counts(uint32_t *blocks, uint32_t *skipped, uint32_t *ecc_errors)
{
	*blocks = s_raw_blocks;
	*skipped = s_raw_skipped;
	*ecc_errors = s_raw_ecc;
}

#endif /* IDE_BOARD_DISK */
