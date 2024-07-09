/*********************
 *      INCLUDES
 *********************/

#include "wifi.h"
#include "nvs_rw.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>

/***********************************
 *           DATA
 ***********************************/

extern QueueHandle_t WIFI_Queue_VacantPosition;

/***********************************
 *   PRIVATE DATA
 ***********************************/

static uint8_t volatile num_wifi = 0;

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

static uint8_t WIFI_GetNumSSID(void);
static void WIFI_SetNumSSID(uint8_t num);
static esp_err_t WIFI_ScanSSID(uint8_t *ssid, uint8_t id, uint8_t len);
static esp_err_t WIFI_DeleteSSID(uint8_t id);
static esp_err_t WIFI_ScanPass(uint8_t *pass, uint8_t id, uint8_t len);
static esp_err_t WIFI_DeletePass(uint8_t id);
static esp_err_t WIFI_SetSSID(uint8_t *ssid, uint8_t id);
static esp_err_t WIFI_SetPass(uint8_t *pass, uint8_t id);

/***********************************
 *   PUBLIC FUNCTIONS
 ***********************************/

/**
 * The function WIFI_ScanNVS scans for a specific WiFi network SSID and retrieves its password if
 * found.
 *
 * @param ssid The `ssid` parameter is a pointer to an array of characters representing the SSID
 * (Service Set Identifier) of a Wi-Fi network.
 * @param pass The `pass` parameter in the `WIFI_ScanNVS` function is a pointer to a uint8_t array
 * where the password of the WiFi network will be stored if the corresponding SSID is found during the
 * scan.
 *
 * @return The function `WIFI_ScanNVS` returns an `int8_t` value. If the specified SSID is found during
 * the scan, it returns the index of that SSID. If the SSID is not found or if there are no SSIDs
 * available, it returns -1.
 */
int8_t WIFI_ScanNVS(uint8_t *ssid, uint8_t *pass)
{
    int8_t i;
    uint8_t ssid_temp[32];

    num_wifi = WIFI_GetNumSSID();
    if (num_wifi == 0)
    {
        return -1;
    }

    for (i = 1; i <= num_wifi; i++)
    {
        WIFI_ScanSSID(ssid_temp, i, 32);
        if (memcmp(ssid_temp, ssid, strlen((char *)ssid)) == 0)
        {
            WIFI_ScanPass(pass, i, 32);
            return i;
        }
    }
    return -1;
}

/**
 * The function WIFI_StoreNVS stores a new WiFi SSID and password in non-volatile storage.
 *
 * @param ssid The `ssid` parameter is a pointer to an array of characters that represents the name of
 * the Wi-Fi network (Service Set Identifier).
 * @param password The `password` parameter in the `WIFI_StoreNVS` function is a pointer to an array of
 * `uint8_t` data type, which is typically used to store a password for a Wi-Fi network.
 */
void WIFI_StoreNVS(uint8_t *ssid, uint8_t *password)
{
    if(uxQueueMessagesWaiting(WIFI_Queue_VacantPosition) > 0)
    {
        uint8_t position = 0;
        xQueueReceive( WIFI_Queue_VacantPosition, &position, ( TickType_t ) 10 );
        WIFI_SetSSID(ssid, position);
        WIFI_SetPass(password, position);
    }
    else
    {
        num_wifi = WIFI_GetNumSSID();
        num_wifi++;
        WIFI_SetNumSSID(num_wifi);
        WIFI_SetSSID(ssid, num_wifi);
        WIFI_SetPass(password, num_wifi);
    }
}


/**
 * The function WIFI_DeleteNVS deletes a WiFi SSID and password in non-volatile storage.
 *
 * @param ssid The `ssid` parameter is a pointer to an array of characters that represents the name of
 * the Wi-Fi network (Service Set Identifier).
 */
int8_t WIFI_DeleteNVS (uint8_t *ssid)  
{
    int8_t i;
    uint8_t ssid_temp[32];

    num_wifi = WIFI_GetNumSSID();
    if (num_wifi == 0)
    {
        return -1;
    }

    for (i = 1; i <= num_wifi; i++)
    {
        WIFI_ScanSSID(ssid_temp, i, 32);
        if (memcmp(ssid_temp, ssid, strlen((char *)ssid)) == 0)
        {
            xQueueSend( WIFI_Queue_VacantPosition, &i, ( TickType_t ) 0 );
            WIFI_DeleteSSID(i);
            WIFI_DeletePass(i);
            return i;
        }
    }
    return -1;
}


/**
 * The function WIFI_AutoUpdatePassword stores a WiFi SSID and NEW password in non-volatile storage.
 *
 * @param ssid The `ssid` parameter is a pointer to an array of characters that represents the name of
 * the Wi-Fi network (Service Set Identifier).
 * @param password The `password` parameter in the `WIFI_StoreNVS` function is a pointer to an array of
 * `uint8_t` data type, which is typically used to store a NEW password for a Wi-Fi network.
 */
void WIFI_AutoUpdatePassword(uint8_t *ssid, uint8_t *pass)
{
    WIFI_Status_t reconnection = WIFI_Connect(ssid, pass);
    if (reconnection != CONNECT_OK)
    {
        WIFI_DeleteNVS(ssid);
    }
    else
    {
        WIFI_DeleteNVS(ssid);
        WIFI_StoreNVS(ssid, pass);
    }
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/

static uint8_t WIFI_GetNumSSID(void)
{
    uint8_t num;
    nvs_handle_t nvsHandle;
    nvs_open(NUM_WIFI_NVS, NVS_READWRITE, &nvsHandle);
    esp_err_t err = nvs_get_u8(nvsHandle, NUM_WIFI_KEY, &num);

    if (err == ESP_OK)
    {
        return num;
    }
    else
    {
        nvs_set_u8(nvsHandle, NUM_WIFI_KEY, num);
        return 0;
    }
}

static void WIFI_SetNumSSID(uint8_t num)
{
    nvs_handle_t nvsHandle;
    nvs_open(NUM_WIFI_NVS, NVS_READWRITE, &nvsHandle);
    nvs_set_u8(nvsHandle, NUM_WIFI_KEY, num);
}

static esp_err_t WIFI_ScanSSID(uint8_t *ssid, uint8_t id, uint8_t len)
{
    char ssid_key[32];
    sprintf(ssid_key, "%d ssid", id);
    return NVS_ReadString(SSID_NVS, (const char *)ssid_key,
                          (char *)ssid, 32);
}

static esp_err_t WIFI_DeleteSSID(uint8_t id)
{
    char ssid_key[32];
    sprintf(ssid_key, "%d ssid", id);
    return NVS_DeleteString(SSID_NVS, (const char *)ssid_key);
}

static esp_err_t WIFI_ScanPass(uint8_t *pass, uint8_t id, uint8_t len)
{
    char pass_key[32];
    sprintf(pass_key, "%d pass", id);
    return NVS_ReadString(PASS_NVS, (const char *)pass_key,
                          (char *)pass, 32);
}

static esp_err_t WIFI_DeletePass(uint8_t id)
{
    char pass_key[32];
    sprintf(pass_key, "%d pass", id);
    return NVS_DeleteString(PASS_NVS, (const char *)pass_key);
}

static esp_err_t WIFI_SetSSID(uint8_t *ssid, uint8_t id)
{
    char ssid_key[32];
    sprintf(ssid_key, "%d ssid", id);
    return NVS_WriteString(SSID_NVS, (const char *)ssid_key,
                           (const char *)ssid);
}

static esp_err_t WIFI_SetPass(uint8_t *pass, uint8_t id)
{
    char pass_key[32];
    sprintf(pass_key, "%d pass", id);
    return NVS_WriteString(PASS_NVS, (const char *)pass_key,
                           (const char *)pass);
}
