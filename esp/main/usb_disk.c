#ifdef IDE_BOARD_DISK
/*
 * USB disk mode on the ESP-Mosaico: hold the AI button while switching on
 * (or it comes by itself the first time, while the flash has no ini).  The
 * Type-C port is then a USB drive with two disks, through TinyUSB
 * (esp_tinyusb's MSC class), and no PC is emulated:
 *
 *   - TINY386, the FAT volume in the NOR flash's storage partition: the ini
 *     files, bios.bin and vgabios.bin in its tiny386 folder;
 *   - the NAND: once set up for tiny386, the guest's whole hard disk (write
 *     the Windows image onto it with an image writer, or open its C: drive);
 *     before that, the chip as it is, read only, to save first (the factory
 *     files: 142,606,336 bytes, every page with its spare area).
 *
 * The AI button, while in this mode:
 *   - a press of less than a second: start tiny386 (eject both drives on
 *     the PC first);
 *   - held five seconds (the panel says when to let go): with the chip not
 *     set up yet, erase it and set it up (everything on it is gone, so save
 *     it first); with it set up, restore mode, where the NAND drive is the
 *     raw chip again and takes a saved copy written back onto it; in
 *     restore mode, back to the Windows disk.
 * Each of those comes back to USB disk mode by a restart, so that the PC
 * sees the new drive afresh.
 */
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "console.h"
#include "nand_disk.h"
#include "usb_disk.h"

#define NOTE_USB 0x55B0D15Cu            /* back into USB disk mode */
#define NOTE_RESTORE 0x55B0D15Du        /* ... with the NAND raw and writable */
static RTC_NOINIT_ATTR uint32_t s_note, s_note_inv;
static uint32_t s_taken;
static bool s_first;                    /* no ini on the flash yet */

static bool ai_down(void)
{
	return gpio_get_level(MOSAICO_AI_BUTTON) == 0;
}

static void restart_into(uint32_t note)
{
	s_note = note;
	s_note_inv = ~note;
	vTaskDelay(pdMS_TO_TICKS(300));
	esp_restart();
}

static bool is_ini(const char *n)
{
	size_t l = strlen(n);
	return l > 4 && !strcasecmp(n + l - 4, ".ini");
}

/* any ini where esp_main.c looks on the flash; the tiny386 folder made */
static bool ini_on_flash(void)
{
	wl_handle_t wl;
	const esp_vfs_fat_mount_config_t cfg = {
		.max_files = 2,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
	};
	if (esp_vfs_fat_spiflash_mount_rw_wl("/spiflash", "storage", &cfg, &wl) != ESP_OK)
		return true;    /* cannot tell: the normal start says what is wrong */
	mkdir("/spiflash/tiny386", 0777);
	bool any = false;
	struct stat st;
	if (stat("/spiflash/tiny386.ini", &st) == 0)
		any = true;
	DIR *d = opendir("/spiflash/tiny386");
	if (d) {
		struct dirent *e;
		while (!any && (e = readdir(d)))
			if (is_ini(e->d_name))
				any = true;
		closedir(d);
	}
	esp_vfs_fat_spiflash_unmount_rw_wl("/spiflash", wl);
	return any;
}

/* what the PC does with the port, on the panel: with no line at all, the
   PC never set the board up as a USB device */
static void usb_event(tinyusb_event_t *e, void *arg)
{
	(void) arg;
	if (e->id == TINYUSB_EVENT_ATTACHED)
		console_puts(CON_GOOD, "USB: the PC has set the drives up.");
	else if (e->id == TINYUSB_EVENT_DETACHED)
		console_puts(CON_INFO, "USB: the PC has let go of the port.");
}

bool usb_disk_wanted(void)
{
	s_taken = s_note_inv == ~s_note ? s_note : 0;
	s_note = s_note_inv = 0;
	if (s_taken == NOTE_USB || s_taken == NOTE_RESTORE)
		return true;
	if (ai_down()) {
		vTaskDelay(pdMS_TO_TICKS(50));
		if (ai_down())
			return true;
	}
	s_taken = 0;
	if (!ini_on_flash()) {
		s_first = true;
		return true;
	}
	return false;
}

static void say_counts(enum nand_state st, bool raw, bool restore)
{
	if (raw && restore) {
		uint32_t b, sk, ecc;
		nand_raw_counts(&b, &sk, &ecc);
		console_printf(CON_INFO, "Restore: %" PRIu32 " of 1024 blocks written back, "
			       "%" PRIu32 " left alone as bad.", b, sk);
	} else if (raw) {
		uint32_t b, sk, ecc;
		nand_raw_counts(&b, &sk, &ecc);
		if (ecc)
			console_printf(CON_WARN, "%" PRIu32 " pages read with an ECC error "
				       "(kept as read).", ecc);
	}
	(void) st;
}

void usb_disk_mode(void)
{
	bool restore = s_taken == NOTE_RESTORE;
	console_puts(CON_GOOD, "USB disk mode: the Type-C port is a USB drive for "
		     "a PC; tiny386 is not running.");
	if (s_first)
		console_puts(CON_WARN, "The flash has no ini file yet. Copy the tiny386 "
			     "folder of the package onto the TINY386 drive.");

	/* the TINY386 drive: the flash's FAT volume, made if it is not there */
	(void) ini_on_flash();
	const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
		ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
	wl_handle_t wl = WL_INVALID_HANDLE;
	if (!part || wl_mount(part, &wl) != ESP_OK)
		console_fatal("USB disk mode: the flash's storage partition could not be "
			      "opened. Flash the Mosaico image set again.");

	/* the NAND */
	char why[120];
	enum nand_state st = nand_disk_start(why, sizeof(why));
	esp_blockdev_handle_t bd = NULL;
	bool raw = false;
	if (st == NAND_OURS && !restore) {
		bd = nand_disk_blockdev();
		console_printf(CON_GOOD, "NAND: the Windows disk, %" PRIu64 " bytes (%" PRIu64
			       " MiB). Write the disk image onto that drive with an image "
			       "writer, or open its C: drive.", nand_disk_bytes(),
			       nand_disk_bytes() >> 20);
		console_puts(CON_INFO, "Hold the AI button 5 s for restore mode (a saved "
			     "copy of the NAND written back).");
	} else if (st == NAND_OURS || st == NAND_OTHER) {
		raw = true;
		bd = nand_raw_blockdev(restore);
		if (!bd)
			console_fatal("USB disk mode: the NAND could not be read raw.");
		if (restore) {
			console_puts(CON_WARN, "Restore mode: the NAND drive is the raw chip, "
				     "142,606,336 bytes. Writing a saved copy onto it replaces "
				     "the Windows disk with what the copy holds.");
			console_puts(CON_INFO, "Hold the AI button 5 s for the Windows disk again.");
		} else {
			console_puts(CON_WARN, "NAND: not set up for tiny386 yet. The drive is "
				     "the chip as it is, read only, 142,606,336 bytes: save it "
				     "with an image writer first (the factory files, to put "
				     "back later).");
			console_puts(CON_WARN, "Then hold the AI button 5 s: the NAND is "
				     "erased and set up as the Windows disk.");
		}
		console_printf(CON_INFO, "NAND: %d bad blocks.", nand_raw_bad_blocks());
	} else {
		console_printf(CON_ERROR, "NAND: %s. Only the TINY386 drive is offered.", why);
	}

	const tinyusb_msc_driver_config_t mcfg = {
		.user_flags.auto_mount_off = 1,
	};
	esp_err_t err = tinyusb_msc_install_driver(&mcfg);
	if (err != ESP_OK)
		console_fatal("USB disk mode: the drive could not be set up (%s).",
			      esp_err_to_name(err));
	tinyusb_msc_storage_config_t s0 = {
		.medium.wl_handle = wl,
		.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
	};
	if ((err = tinyusb_msc_new_storage_spiflash(&s0, NULL)) != ESP_OK)
		console_fatal("USB disk mode: the TINY386 drive could not be offered (%s).",
			      esp_err_to_name(err));
	if (bd) {
		tinyusb_msc_storage_config_t s1 = {
			.medium.blockdev = bd,
			.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
		};
		if ((err = tinyusb_msc_new_storage_blockdev(&s1, NULL)) != ESP_OK)
			console_printf(CON_ERROR, "USB disk mode: the NAND drive could not be "
				       "offered (%s).", esp_err_to_name(err));
	}
	const tinyusb_config_t tcfg = TINYUSB_DEFAULT_CONFIG(usb_event);
	if ((err = tinyusb_driver_install(&tcfg)) != ESP_OK)
		console_fatal("USB disk mode: USB would not start (%s).", esp_err_to_name(err));
	console_puts(CON_GOOD, "Ready: connect the Type-C port to the PC. When done, "
		     "eject both drives there, then press the AI button to start tiny386.");

	/* the hold that asked for this mode is not a press */
	while (ai_down())
		vTaskDelay(pdMS_TO_TICKS(50));
	int held = 0;                   /* ticks of 50 ms the button is down */
	bool told = false;
	for (int tick = 0; ; tick++) {
		vTaskDelay(pdMS_TO_TICKS(50));
		if (ai_down()) {
			held++;
			if (held == 100 && !told) {
				told = true;
				console_puts(CON_WARN, raw && !restore ?
					     "Let go now to erase the NAND and set it up." :
					     !raw ? "Let go now for restore mode." :
					     "Let go now for the Windows disk.");
			}
			continue;
		}
		if (held) {
			int h = held;
			held = 0;
			told = false;
			if (h < 20) {
				console_puts(CON_INFO, "Starting tiny386.");
				if (bd && bd->ops->sync)
					bd->ops->sync(bd);
				wl_unmount(wl);
				restart_into(0);
			} else if (h >= 100) {
				if (raw && !restore) {
					console_puts(CON_WARN, "Erasing the NAND (about 4 s)...");
					tinyusb_driver_uninstall();
					if (nand_disk_format(why, sizeof(why)) == 0) {
						console_puts(CON_GOOD, "The NAND is the Windows "
							     "disk now. Back to USB disk mode.");
					} else {
						console_printf(CON_ERROR, "The NAND could not be set "
							       "up: %s.", why);
						vTaskDelay(pdMS_TO_TICKS(5000));
					}
					restart_into(NOTE_USB);
				} else if (!raw) {
					if (bd && bd->ops->sync)
						bd->ops->sync(bd);
					restart_into(NOTE_RESTORE);
				} else {
					if (bd && bd->ops->sync)
						bd->ops->sync(bd);
					restart_into(NOTE_USB);
				}
			} else {
				console_puts(CON_INFO, "Held 1 to 5 s: nothing done.");
			}
		}
		if (tick % 100 == 99)
			say_counts(st, raw, restore);
	}
}

#endif /* IDE_BOARD_DISK */
