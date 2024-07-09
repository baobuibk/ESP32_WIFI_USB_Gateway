#ifndef SD_CARD_H_
#define SD_CARD_H_

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"

#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern const char *MOUNT_POINT;
#define SD_CARD_TAG "[sd_card]"

/**********************
 *   PUBLIC FUNCTIONS
 **********************/

// esp_err_t mountSDCARD(char *mount_point, sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif

#endif /* FTP_H_ */