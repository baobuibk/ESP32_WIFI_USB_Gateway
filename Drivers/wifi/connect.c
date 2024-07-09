/*********************
 *      INCLUDES
 *********************/

#include "wifi.h"
#include "nvs_rw.h"

#include "esp_wifi.h"
#include "esp_log.h"

/***********************************
 *           DATA
 ***********************************/

WIFI_Status_t state_connected_wifi = CONNECT_FAIL;

/***********************************
 *   PRIVATE DATA
 ***********************************/

static EventGroupHandle_t s_wifi_event_group;
static uint8_t s_retry_num = 0;

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data);

/***********************************
 *   PUBLIC FUNCTIONS
 ***********************************/

/**
 * The function `WIFI_Connect` attempts to connect to a WiFi network using the provided SSID and
 * password, handling authentication modes and event notifications.
 *
 * @param ssid The `ssid` parameter in the `WIFI_Connect` function is a pointer to an array of
 * characters representing the SSID (Service Set Identifier) of the Wi-Fi network you want to connect
 * to. The SSID is essentially the name of the Wi-Fi network.
 * @param password The code you provided is a function `WIFI_Connect` that attempts to connect to a
 * WiFi network using the provided SSID and password. The function initializes the WiFi configuration,
 * sets up event handlers, and then tries to connect to the network.
 *
 * @return The function `WIFI_Connect` returns a value of type `WIFI_Status_t`, which is an enumeration
 * type. The possible return values are:
 * - `CONNECT_OK` if the connection to the Wi-Fi network was successful.
 * - `CONNECT_FAIL` if the connection to the Wi-Fi network failed.
 * - `UNEXPECTED_EVENT` if an unexpected event occurred during the connection process.
 */
WIFI_Status_t WIFI_Connect(uint8_t *ssid, uint8_t *password)
{
    esp_wifi_stop();

    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&config);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &event_handler,
                                        NULL,
                                        &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &event_handler,
                                        NULL,
                                        &instance_got_ip);

    wifi_config_t wifi_config =
        {
            .sta =
                {
                    /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
                     * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
                     * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
                     * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
                     */
                    .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                },
        };

    for (uint8_t i = 0; i < 32; i++)
    {
        wifi_config.sta.ssid[i] = *(ssid + i);
        wifi_config.sta.password[i] = *(password + i);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(WIFI_TAG, "Wifi_init_station finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        s_retry_num = 0;
        ESP_LOGI(WIFI_TAG, "Connected to ap SSID:%s password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        return CONNECT_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        s_retry_num = 0;
        ESP_LOGE(WIFI_TAG, "Failed to connect to SSID:%s, password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        return CONNECT_FAIL;
    }
    else
    {
        s_retry_num = 0;
        ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
        return UNEXPECTED_EVENT;
    }
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
        }
        else
        {
            state_connected_wifi = CONNECT_FAIL;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);        
        } 
        ESP_LOGE(WIFI_TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        state_connected_wifi = CONNECT_OK;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
