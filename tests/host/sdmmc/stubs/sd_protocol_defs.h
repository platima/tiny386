/* host stand-in: the command numbers of ESP-IDF's sd_protocol_defs.h that
   sdmmc_timeout_wrap.c and its test use */
#pragma once
#define MMC_SEND_STATUS 13
#define MMC_READ_BLOCK_MULTIPLE 18
#define SD_APP_SEND_NUM_WR_BLOCKS 22
#define MMC_WRITE_BLOCK_MULTIPLE 25
#define MMC_APP_CMD 55
