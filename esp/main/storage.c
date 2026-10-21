#include <string.h>
#include <strings.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/sdmmc_host.h"
#ifdef SD_SPI_MOSI
#include "driver/i2c.h"
#endif
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "sdmmc_cmd.h"
#include "common.h"
#include "storage.h"
#include "driver/gpio.h"
#ifdef SD_PWR_CTRL_LDO_IO_ID
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

static const char *TAG = "storage";
void *rawsd;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

#ifdef USE_HOSTED_WIFI
static esp_err_t sdmmc_host_init_dummy(void)
{
	return ESP_OK;
}

static esp_err_t sdmmc_host_deinit_dummy(void)
{
	return ESP_OK;
}
#endif

#if defined(SD_CLK) && !defined(USE_RAWSD)
/* Mounts the card at /sdcard, true if that worked.  May be called again
   after a failure: esp_vfs_fat_sdmmc_mount() undoes what it did when it
   fails. */
bool storage_sd_mount(void)
{
	// Options for mounting the filesystem.
	esp_vfs_fat_sdmmc_mount_config_t sdmount_config = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};
	sdmmc_card_t *card;
	esp_err_t ret;
	ESP_LOGI(TAG, "Initializing SD card");

#ifdef SD_PWR_EN_GPIO
	// The slot's power switch is active low
	gpio_set_direction(SD_PWR_EN_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(SD_PWR_EN_GPIO, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
#endif

	// Use settings defined above to initialize SD card and mount FAT filesystem.
	// Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
	// Please check its source code and implement error recovery when developing
	// production applications.
	ESP_LOGI(TAG, "Using SDMMC peripheral");

	// By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
	// For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
	// Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#ifdef USE_HOSTED_WIFI
	host.slot = SDMMC_HOST_SLOT_0;
	host.init = &sdmmc_host_init_dummy;
	host.deinit = &sdmmc_host_deinit_dummy;
#endif

	// For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
	// When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
	// and the internal LDO power supply, we need to initialize the power supply first.
#ifdef SD_PWR_CTRL_LDO_IO_ID
	sd_pwr_ctrl_ldo_config_t ldo_config = {
		.ldo_chan_id = SD_PWR_CTRL_LDO_IO_ID,
	};
	static sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;    /* once */

	if (!pwr_ctrl_handle) {
		ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
			return false;
		}
	}
	host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

#ifdef SD_D3
	slot_config.width = 4;
	slot_config.clk = SD_CLK;
	slot_config.cmd = SD_CMD;
	slot_config.d0 = SD_D0;
	slot_config.d1 = SD_D1;
	slot_config.d2 = SD_D2;
	slot_config.d3 = SD_D3;
#else
	slot_config.width = 1;
	slot_config.clk = SD_CLK;
	slot_config.cmd = SD_CMD;
	slot_config.d0 = SD_D0;
#endif
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	ESP_LOGI(TAG, "Mounting filesystem");
	ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &sdmount_config, &card);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem. "
				 "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
		} else {
			ESP_LOGE(TAG, "Failed to initialize the card (%s). "
				 "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		}
		return false;
	}
	ESP_LOGI(TAG, "Filesystem mounted");
	rawsd = card;
	return true;
}
#else
bool storage_sd_mount(void)
{
	return false;
}
#endif

/* ====
 * The card's other partitions.  esp_vfs_fat_sdmmc_mount() mounts the first
 * FAT partition of the card's MBR at /sdcard (FatFs's own choice when its
 * partition is "auto").  Each other FAT partition goes at /sdcardN, N its
 * number in the MBR (1 to 4): a second logical drive of FatFs on the same
 * card, bound to that partition through VolToPart[] (FF_MULTI_PARTITION is
 * on in this IDF's FatFs).  So a card laid out for Linux (FAT32 p1, ext4
 * p2, and a FAT32 p3 labelled WIN9X for the Windows image) gives /sdcard and
 * /sdcard3.
 */
#if defined(SD_CLK) && !defined(USE_RAWSD)
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "esp_heap_caps.h"

static struct storage_volume s_vols[4];
static int s_nvols;

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24;
}

/* a FAT boot sector, as FatFs's check_fs() looks for one */
static bool fat_vbr(const uint8_t *s)
{
	if (s[510] != 0x55 || s[511] != 0xAA)
		return false;
	if (s[0] != 0xEB && s[0] != 0xE9 && s[0] != 0xE8)
		return false;
	return !memcmp(s + 0x52, "FAT32", 5) || !memcmp(s + 0x36, "FAT", 3);
}

static uint32_t vbr_serial(const uint8_t *s)
{
	return !memcmp(s + 0x52, "FAT32", 5) ? le32(s + 0x43) : le32(s + 0x27);
}

static void trim_label(char *l)
{
	int n = strlen(l);
	while (n > 0 && l[n - 1] == ' ')
		l[--n] = 0;
}

int storage_sd_mount_others(void)
{
	sdmmc_card_t *card = rawsd;
	s_nvols = 0;
	if (!card)
		return 0;
	uint8_t *mbr = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	uint8_t *vbr = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	if (!mbr || !vbr || sdmmc_read_sectors(card, mbr, 0, 1) != ESP_OK)
		goto out;

	/* which partition FatFs mounted at /sdcard: its serial number */
	BYTE pdrv0 = ff_diskio_get_pdrv_card(card);
	char drv0[3] = { (char) ('0' + pdrv0), ':', 0 };
	char label0[24] = "";
	DWORD serial0 = 0;
	f_getlabel(drv0, label0, &serial0);
	trim_label(label0);

	if (fat_vbr(mbr)) {
		/* no partition table: the whole card is the one volume */
		struct storage_volume *v = &s_vols[s_nvols++];
		memset(v, 0, sizeof(*v));
		strcpy(v->path, "/sdcard");
		snprintf(v->label, sizeof(v->label), "%s", label0);
		v->fat = v->mounted = true;
		v->mib = (uint32_t) ((uint64_t) card->csd.capacity * card->csd.sector_size >> 20);
		goto out;
	}
	if (mbr[510] != 0x55 || mbr[511] != 0xAA)
		goto out;

	bool first_seen = false;
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = mbr + 446 + 16 * i;
		uint8_t type = e[4];
		uint32_t start = le32(e + 8), count = le32(e + 12);
		if (!type || !count)
			continue;
		struct storage_volume *v = &s_vols[s_nvols++];
		memset(v, 0, sizeof(*v));
		v->part = i + 1;
		v->type = type;
		v->mib = count >> 11;
		if (type == 0xEE || sdmmc_read_sectors(card, vbr, start, 1) != ESP_OK ||
		    !fat_vbr(vbr))
			continue;
		v->fat = true;
		if (!first_seen && (vbr_serial(vbr) == serial0 || !serial0)) {
			first_seen = true;
			strcpy(v->path, "/sdcard");
			snprintf(v->label, sizeof(v->label), "%s", label0);
			v->mounted = true;
			continue;
		}

		/* a logical drive of its own, bound to MBR entry i + 1 */
		BYTE ld = FF_DRV_NOT_USED;
		if (ff_diskio_get_drive(&ld) != ESP_OK || ld == FF_DRV_NOT_USED) {
			v->why = "no logical drive left (CONFIG_FATFS_VOLUME_COUNT)";
			continue;
		}
		snprintf(v->path, sizeof(v->path), "/sdcard%d", i + 1);
		char drv[3] = { (char) ('0' + ld), ':', 0 };
		ff_diskio_register_sdmmc(ld, card);
		VolToPart[ld].pd = ld;
		VolToPart[ld].pt = i + 1;
		FATFS *fs = NULL;
		esp_vfs_fat_conf_t conf = {
			.base_path = v->path,
			.fat_drive = drv,
			.max_files = 4,
		};
		if (esp_vfs_fat_register(&conf, &fs) != ESP_OK) {
			v->why = "could not be registered";
		} else if (f_mount(fs, drv, 1) != FR_OK) {
			v->why = "FatFs could not mount it";
			f_mount(NULL, drv, 0);
			esp_vfs_fat_unregister_path(v->path);
		} else {
			char label[24] = "";
			f_getlabel(drv, label, NULL);
			trim_label(label);
			snprintf(v->label, sizeof(v->label), "%s", label);
			v->mounted = true;
			continue;
		}
		ff_diskio_unregister(ld);
		VolToPart[ld].pd = ld;
		VolToPart[ld].pt = 0;
		v->path[0] = 0;
	}
out:
	free(mbr);
	free(vbr);
	return s_nvols;
}

const struct storage_volume *storage_volumes(int *n)
{
	*n = s_nvols;
	return s_vols;
}
#else
int storage_sd_mount_others(void)
{
	return 0;
}

const struct storage_volume *storage_volumes(int *n)
{
	*n = 0;
	return NULL;
}
#endif

/* the volume the Linux route calls /mnt/win: the one labelled WIN9X, else
   the first FAT partition after /sdcard; NULL if none is mounted */
const char *storage_win_path(void)
{
	int n;
	const struct storage_volume *v = storage_volumes(&n);
	for (int i = 0; i < n; i++)
		if (v[i].mounted && !strcasecmp(v[i].label, "WIN9X"))
			return v[i].path;
	for (int i = 0; i < n; i++)
		if (v[i].mounted && strcmp(v[i].path, "/sdcard"))
			return v[i].path;
	return NULL;
}

/* Bit 0: the card is mounted at /sdcard; bit 1: the flash's storage
   partition at /spiflash (tried only without a card). */
int storage_init(void)
{
	bool sd_mount_ok = false;
	bool flash_ok = false;
#ifdef SD_CLK
#ifndef USE_RAWSD
	sd_mount_ok = storage_sd_mount();
#else
	esp_err_t ret;
	sdmmc_card_t *card = malloc(sizeof(sdmmc_card_t));
	memset(card, 0, sizeof(sdmmc_card_t));
	ESP_LOGI(TAG, "Initializing SD card");
	ESP_LOGI(TAG, "Using SDMMC peripheral");
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#ifdef USE_HOSTED_WIFI
	host.slot = SDMMC_HOST_SLOT_0;
	host.init = &sdmmc_host_init_dummy;
	host.deinit = &sdmmc_host_deinit_dummy;
#endif

#ifdef SD_PWR_CTRL_LDO_IO_ID
	sd_pwr_ctrl_ldo_config_t ldo_config = {
		.ldo_chan_id = SD_PWR_CTRL_LDO_IO_ID,
	};
	sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

	ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
		return 0;
	}
	host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
#ifdef SD_D3
	slot_config.width = 4;
	slot_config.clk = SD_CLK;
	slot_config.cmd = SD_CMD;
	slot_config.d0 = SD_D0;
	slot_config.d1 = SD_D1;
	slot_config.d2 = SD_D2;
	slot_config.d3 = SD_D3;
#else
	slot_config.width = 1;
	slot_config.clk = SD_CLK;
	slot_config.cmd = SD_CMD;
	slot_config.d0 = SD_D0;
#endif
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	ret = host.init();
	assert(ret == 0);
	ret = sdmmc_host_init_slot(host.slot, &slot_config);
	assert(ret == 0);
	ret = sdmmc_card_init(&host, card);
	assert(ret == 0);
	sdmmc_card_print_info(stderr, card);
	rawsd = card;
#endif
#elif defined(SD_SPI_MOSI)
	/*
	 * elecrow7s3: SPI-mode SD card via SPI2_HOST.
	 *
	 * Before touching the SPI bus, we must enable the TF card power/mux
	 * path through the CH422G I/O expander over I2C.  After sending the
	 * enable command we delete the I2C driver so that lcd_elecrow7s3.c can
	 * re-initialise it cleanly for backlight control.
	 */

	/* ---- I2C pre-init: enable TF card power ---- */
	ESP_LOGI(TAG, "Enabling TF card power via I2C");
	{
		i2c_config_t i2c_cfg = {
			.mode             = I2C_MODE_MASTER,
			.sda_io_num       = LCD_I2C_SDA,
			.scl_io_num       = LCD_I2C_SCL,
			.sda_pullup_en    = GPIO_PULLUP_ENABLE,
			.scl_pullup_en    = GPIO_PULLUP_ENABLE,
			.master.clk_speed = 400000,
		};
		esp_err_t r = i2c_param_config(I2C_NUM_0, &i2c_cfg);
		if (r == ESP_OK)
			r = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
		if (r == ESP_OK) {
			uint8_t cmd = 0x10;
			i2c_master_write_to_device(I2C_NUM_0, 0x30, &cmd, 1,
			                           pdMS_TO_TICKS(100));
			ESP_LOGI(TAG, "TF card power enabled");
		} else {
			ESP_LOGW(TAG, "I2C init failed (%s), skipping TF power-on",
			         esp_err_to_name(r));
		}
		/* 500 ms for power path to settle before first CMD0/CMD8 */
		vTaskDelay(pdMS_TO_TICKS(500));
		/* Release I2C so vga_task can reinit it for backlight */
		i2c_driver_delete(I2C_NUM_0);
	}

	/* ---- SPI bus init ---- */
	ESP_LOGI(TAG, "Initializing SPI2 bus for SD card");
	{
		spi_bus_config_t bus_cfg = {
			.mosi_io_num     = SD_SPI_MOSI,
			.miso_io_num     = SD_SPI_MISO,
			.sclk_io_num     = SD_SPI_SCK,
			.quadwp_io_num   = -1,
			.quadhd_io_num   = -1,
			.max_transfer_sz = 4096,
		};
		ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
	}

	/* ---- Mount SD card via FAT/sdspi ---- */
	{
		esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
			.format_if_mount_failed = false,
			.max_files              = 3,
			.allocation_unit_size   = 16 * 1024,
		};

		sdmmc_host_t spi_host = SDSPI_HOST_DEFAULT();
		sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
		slot_cfg.gpio_cs = SD_SPI_CS;
		slot_cfg.host_id = SPI2_HOST;

		/*
		 * Try mounting at normal speed first, then at 400 kHz as last
		 * resort.  The README notes 40 MHz often fails on first cold boot.
		 */
		static const int freqs_khz[] = {
			SD_SPI_FREQ_KHZ, SD_SPI_FREQ_KHZ, 400
		};
		sdmmc_card_t *card = NULL;
		for (int i = 0;
		     i < (int)(sizeof(freqs_khz) / sizeof(freqs_khz[0]));
		     i++) {
			spi_host.max_freq_khz = freqs_khz[i];
			ESP_LOGI(TAG, "SD mount attempt %d: CS=%d freq=%d kHz",
			         i + 1, (int)slot_cfg.gpio_cs, freqs_khz[i]);
			esp_err_t ret = esp_vfs_fat_sdspi_mount(
				"/sdcard", &spi_host, &slot_cfg, &mount_cfg, &card);
			if (ret == ESP_OK) {
				ESP_LOGI(TAG, "SD card mounted");
				sd_mount_ok = true;
				rawsd = card;
				break;
			}
			ESP_LOGW(TAG, "  failed: %s", esp_err_to_name(ret));
		}
		if (!sd_mount_ok) {
			ESP_LOGE(TAG, "All SD mount attempts failed");
		}
	}
#endif /* SD_CLK / SD_SPI_MOSI */

	// mount spiflash, only if sdcard is not mounted
	// to save memory
	// (only if the flash has one: the Korvo-1's table shared with Linux
	// has none, and the attempt would only log an error)
	if (!sd_mount_ok &&
	    esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
				     ESP_PARTITION_SUBTYPE_DATA_FAT, "storage")) {
		const esp_vfs_fat_mount_config_t spiflash_cfg = {
			.max_files = 4,
			.format_if_mount_failed = false,
			.allocation_unit_size = CONFIG_WL_SECTOR_SIZE
		};
		flash_ok = esp_vfs_fat_spiflash_mount_rw_wl("/spiflash", "storage",
							    &spiflash_cfg, &s_wl_handle) == ESP_OK;
	}
	return (sd_mount_ok ? 1 : 0) | (flash_ok ? 2 : 0);
}
