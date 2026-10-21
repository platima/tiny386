/*
 * The few things under ESP-IDF's FatFs and storage.c that are not on the
 * host: the card's sector reads and writes (an image file), FatFs's memory
 * and lock functions (ffsystem.c's job on the board), and the VFS
 * registration (which here only notes which FatFs drive a path is).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "host_glue.h"

/* ==== the card ==== */

struct host_card_stats host_card;

static void note_write(size_t start, size_t n)
{
	host_card.writes++;
	for (size_t s = start; s < start + n; s++) {
		bool inside = false;
		for (int i = 0; i < host_card.nallowed; i++)
			if (s >= host_card.allowed[i][0] &&
			    s < host_card.allowed[i][0] + host_card.allowed[i][1])
				inside = true;
		if (!inside) {
			if (!host_card.outside)
				host_card.first_outside = s;
			host_card.outside++;
		}
	}
}

esp_err_t sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start, size_t n)
{
	if (start + n > (size_t) card->csd.capacity)
		return ESP_ERR_INVALID_SIZE;
	host_card.reads++;
	ssize_t got = pread(card->fd, dst, n * 512, (off_t) start * 512);
	return got == (ssize_t) (n * 512) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdmmc_write_sectors(sdmmc_card_t *card, const void *src, size_t start, size_t n)
{
	if (start + n > (size_t) card->csd.capacity)
		return ESP_ERR_INVALID_SIZE;
	note_write(start, n);
	ssize_t put = pwrite(card->fd, src, n * 512, (off_t) start * 512);
	return put == (ssize_t) (n * 512) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdmmc_get_status(sdmmc_card_t *card)
{
	(void) card;
	return ESP_OK;
}

const char *esp_err_to_name(esp_err_t err)
{
	return err == ESP_OK ? "ESP_OK" : "an error";
}

/* ==== FatFs's system functions (ffsystem.c on the board) ==== */

void *ff_memalloc(UINT msize)
{
	return malloc(msize);
}

void ff_memfree(void *mblock)
{
	free(mblock);
}

int ff_mutex_create(int vol)
{
	(void) vol;
	return 1;
}

void ff_mutex_delete(int vol)
{
	(void) vol;
}

int ff_mutex_take(int vol)
{
	(void) vol;
	return 1;
}

void ff_mutex_give(int vol)
{
	(void) vol;
}

/* ==== the VFS ==== */

static struct {
	char base[16];
	char drv[4];
	FATFS *fs;
} s_vfs[FF_VOLUMES];

esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *conf, FATFS **out_fs)
{
	int free_i = -1;
	for (int i = 0; i < FF_VOLUMES; i++) {
		if (s_vfs[i].fs && !strcmp(s_vfs[i].base, conf->base_path)) {
			*out_fs = s_vfs[i].fs;
			return ESP_ERR_INVALID_STATE;
		}
		if (!s_vfs[i].fs && free_i < 0)
			free_i = i;
	}
	if (free_i < 0)
		return ESP_ERR_NO_MEM;
	s_vfs[free_i].fs = calloc(1, sizeof(FATFS));
	snprintf(s_vfs[free_i].base, sizeof(s_vfs[free_i].base), "%s", conf->base_path);
	snprintf(s_vfs[free_i].drv, sizeof(s_vfs[free_i].drv), "%s", conf->fat_drive);
	*out_fs = s_vfs[free_i].fs;
	return ESP_OK;
}

esp_err_t esp_vfs_fat_unregister_path(const char *base_path)
{
	for (int i = 0; i < FF_VOLUMES; i++) {
		if (s_vfs[i].fs && !strcmp(s_vfs[i].base, base_path)) {
			free(s_vfs[i].fs);
			memset(&s_vfs[i], 0, sizeof(s_vfs[i]));
			return ESP_OK;
		}
	}
	return ESP_ERR_INVALID_STATE;
}

const char *host_vfs_drive(const char *base_path)
{
	for (int i = 0; i < FF_VOLUMES; i++)
		if (s_vfs[i].fs && !strcmp(s_vfs[i].base, base_path))
			return s_vfs[i].drv;
	return NULL;
}

FATFS *host_vfs_fs(const char *base_path)
{
	for (int i = 0; i < FF_VOLUMES; i++)
		if (s_vfs[i].fs && !strcmp(s_vfs[i].base, base_path))
			return s_vfs[i].fs;
	return NULL;
}

/* ==== no trim or discard (FatFs asks only from f_mkfs, which nothing
   here calls) ==== */

esp_err_t sdmmc_can_trim(sdmmc_card_t *card)
{
	(void) card;
	return ESP_FAIL;
}

esp_err_t sdmmc_can_discard(sdmmc_card_t *card)
{
	(void) card;
	return ESP_FAIL;
}

esp_err_t sdmmc_erase_sectors(sdmmc_card_t *card, size_t start, size_t n,
			      sdmmc_erase_arg_t arg)
{
	(void) card; (void) start; (void) n; (void) arg;
	return ESP_FAIL;
}
