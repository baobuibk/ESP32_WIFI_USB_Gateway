#ifndef WIFI_H_
#define WIFI_H_

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*********************
 *      DEFINES
 *********************/

#define SCAN_LIST_SIZE      50
#define LIMIT_STORE_WIFI    10
#define NUM_WIFI_VACANT_POSITON 50

#define WIFI_TAG            "[wifi]"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAXIMUM_RETRY 10

#define NUM_WIFI_NVS "Num_ssid_nvs"
#define NUM_WIFI_KEY "Num_ssid_key"
#define SSID_NVS "ssid_nvs"
#define PASS_NVS "pass_nvs"

#define SEND_CONNECT_WIFI_UNSUCCESSFUL_BIT      (1 << 2)

/**********************
 *      TYPEDEFS
 **********************/

typedef enum
{
    CONNECT_OK = 1,
    CONNECT_FAIL,
    UNEXPECTED_EVENT,
}WIFI_Status_t;

extern WIFI_Status_t state_connected_wifi;

/**********************
 *   PUBLIC FUNCTIONS
 **********************/

void WIFI_StaInit(void);
uint8_t WIFI_Scan(char * data_name);
WIFI_Status_t WIFI_Connect(uint8_t *ssid, uint8_t *password);
int8_t WIFI_ScanNVS(uint8_t * ssid, uint8_t * pass);
void WIFI_StoreNVS(uint8_t * ssid, uint8_t *password);
void WIFI_AutoUpdatePassword(uint8_t *ssid, uint8_t *pass);

static inline WIFI_Status_t WIFI_state_connect(void)
{
    return state_connected_wifi;
}

#ifdef __cplusplus
}
#endif

#endif