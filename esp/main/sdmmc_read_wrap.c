/*
 * sdmmc_read_wrap.c: v5.3, no status command after a card read (the S31
 * boards; [esp] sd_wait_fix, as the wait after writes in sdmmc_wait_wrap.c).
 *
 * ESP-IDF's sdmmc_read_sectors_dma() (sdmmc_cmd.c, 188e3e55bb, which the
 * boards' two ESP-IDF fixes on top, e00861c3f497 and 007945125db7, leave
 * as it is) follows every good read with CMD13 and sdmmc_wait_for_idle()
 * on its answer: a second card command, with all of the host's per-command
 * work, after each read.  Nothing reads that answer but the wait, and the
 * wait looks only at READY_FOR_DATA and the tran state, not at the card's
 * error bits.  After a read the card is in tran again by itself: CMD17's
 * block ends it, and for CMD18 the host sends CMD12 (auto-stop) by itself.
 * Nor can the card be programming then: a write that worked comes back
 * only once its programming has ended, and after one that failed the read
 * command itself fails on a card still programming, CMD13 after it or not.
 * So the answer never makes the wait wait, and Linux's mmc block driver
 * sends no CMD13 after reads either.  An error the card found inside a read
 * and did not report in the read itself (the spec's "X" error bits) would
 * show in the answer to its next command, as it does here; ESP-IDF read
 * none of them before.
 *
 * So for a buffer the host takes as it is, this sends the read command as
 * sdmmc_read_sectors_dma() does, the same command, address and checks, and
 * stops there.  A failed read sends CMD13 still, for the same log line as
 * ESP-IDF's.  Any other buffer (ESP-IDF's bounce buffer path), a card in
 * SPI mode and sd_wait_fix = 0 go to ESP-IDF's own sdmmc_read_sectors().
 *
 * One case does change: an auto-stop CMD12 that the card misses.  This
 * ESP-IDF does not report one.  sd_trans_sdmmc.c's process_events() looks
 * at response errors (RTO, RCRC, RESP_ERR) only while the command itself
 * goes out, so one that comes with the auto-stop is dropped and the read
 * comes back good.  Its data is right, as its blocks passed their CRC, but
 * the card is left in its data state.  The CMD13 after the read would find
 * it there, out of tran, and the read would fail after the wait's 5 s.
 * Now the read is good and the card's next read or write command fails
 * instead, as a card in its data state does not answer one.  Either way
 * nothing sends the CMD12 that would free it.  (Seen in the driver's code,
 * not on a board.)  To drop the CMD13 in ESP-IDF itself, the auto-stop's
 * own result would want checking first.
 *
 * Linked by -Wl,--wrap=sdmmc_read_sectors (CMakeLists.txt, the S31
 * boards): diskio_sdmmc.c's ff_sdmmc_read() calls it from another
 * component.  In internal RAM with the rest of the card's read path
 * (linker_hot.lf), in place of sdmmc_read_sectors() and
 * sdmmc_read_sectors_dma(), which go back to flash.  tests/host/sdmmc holds
 * it against ESP-IDF's own on a pretend card.
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/sdmmc_common.h"
#include "sdmmc_wait_wrap.h"

uint32_t sd_read_direct;

esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_block,
				    size_t block_count);
esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_block,
				    size_t block_count);

/* as sdmmc_read_sectors_dma() does after a read that failed: the card's
   status, for the log line */
static esp_err_t __attribute__((noinline)) read_failed(sdmmc_card_t *card, esp_err_t err)
{
	uint32_t status = 0;
	esp_err_t err13 = sdmmc_send_cmd_send_status(card, &status);
	if (err13 == ESP_OK)
		ESP_LOGE("sdmmc_read_wrap", "sdmmc_read_sectors: sdmmc_send_cmd returned 0x%x, "
			 "status 0x%x", err, (unsigned) status);
	else
		ESP_LOGE("sdmmc_read_wrap", "sdmmc_read_sectors: sdmmc_send_cmd returned 0x%x, "
			 "failed to get status (0x%x)", err, err13);
	return err;
}

esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_block,
				    size_t block_count)
{
	size_t block_size = card->csd.sector_size;
	if (!sd_wait_fix || block_count == 0 || host_is_spi(card) ||
	    !card->host.check_buffer_alignment(card->host.slot, dst, block_size * block_count))
		return __real_sdmmc_read_sectors(card, dst, start_block, block_count);
	/* sdmmc_read_sectors_dma(card, dst, start_block, block_count,
	   block_size * block_count), without its CMD13 */
	if (start_block + block_count > card->csd.capacity)
		return ESP_ERR_INVALID_SIZE;
	sdmmc_command_t cmd = {
		.flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1,
		.blklen = block_size,
		.data = dst,
		.datalen = block_count * block_size,
		.buflen = block_count * block_size,
	};
	cmd.opcode = block_count == 1 ? MMC_READ_BLOCK_SINGLE : MMC_READ_BLOCK_MULTIPLE;
	cmd.arg = (card->ocr & SD_OCR_SDHC_CAP) ? start_block : start_block * block_size;
	sd_read_direct++;
	esp_err_t err = sdmmc_send_cmd(card, &cmd);
	if (err != ESP_OK)
		return read_failed(card, err);
	return ESP_OK;
}

#endif
