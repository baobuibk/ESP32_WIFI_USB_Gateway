#ifndef SPI_H_
#define SPI_H_

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**********************
 *      TYPEDEFS
 **********************/

typedef struct 
{
    int16_t MOSI;
    int16_t SCLK;
    int16_t MISO;
    int16_t CS;
    
} spi_config_t;

/**********************
 *   PUBLIC FUNCTIONS
 **********************/

esp_err_t SPI_Master_Init(spi_config_t *spi_cfg);

#ifdef __cplusplus
}
#endif

#endif /* SPI_H_ */