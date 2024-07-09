/*********************
 *      INCLUDES
 *********************/

#include "wifi.h"
#include "nvs_rw.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_wifi.h"

/***********************************
 *           DATA
 ***********************************/

QueueHandle_t WIFI_Queue_VacantPosition;

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

static void WIFI_ResetNumSSID(void);

/***********************************
 *   PUBLIC FUNCTIONS
 ***********************************/

/**
 * The function `WIFI_StaInit` initializes the WiFi station mode on an ESP32 device.
 */
void WIFI_StaInit(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_stop();

    WIFI_ResetNumSSID();/// erase NumSSID in Flash(NVS) 
    WIFI_Queue_VacantPosition = xQueueCreate(NUM_WIFI_VACANT_POSITON, sizeof(uint8_t)); // init Queue to push vacant position when deleting SSID and password
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/
/**
 * The function `WIFI_ResetNumSSID` resets the number of saved WiFi SSIDs to 0 in non-volatile storage.
 */

static void WIFI_ResetNumSSID(void)
{
    nvs_handle_t nvsHandle;
    nvs_open(NUM_WIFI_NVS, NVS_READWRITE, &nvsHandle);
    nvs_set_u8(nvsHandle, NUM_WIFI_KEY, 0);
}