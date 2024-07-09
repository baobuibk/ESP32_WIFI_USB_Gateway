/*********************
 *      INCLUDES
 *********************/

#include "sd_card.h"

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/sdmmc_host.h"

/*********************
 *      DEFINES
 *********************/

const char *MOUNT_POINT = "/data";