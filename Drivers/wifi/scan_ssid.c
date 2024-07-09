/*********************
 *      INCLUDES
 *********************/

#include "wifi.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include <string.h>

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

static void print_auth_mode(int authmode);
static void print_cipher_type(int pairwise_cipher, int group_cipher);
static uint8_t isDuplicate(char *str, char *substrings[], int count);
static void deleteDuplicateSubstrings(char *str, char *result);

/***********************************
 *   PUBLIC FUNCTIONS
 ***********************************/

/**
 * The function `WIFI_Scan` scans for nearby WiFi networks, retrieves their information, removes
 * duplicates, and returns the total number of unique networks found.
 *
 * @param data_name The `data_name` parameter in the `WIFI_Scan` function is a pointer to a uint8_t
 * array where the scanned Wi-Fi SSID names will be stored after processing. The function scans for
 * available Wi-Fi networks, extracts the SSID names, removes duplicates, and stores the unique
 *
 * @return The function `WIFI_Scan` returns the total number of WiFi networks scanned and stored in the
 * `data_name` buffer after removing any duplicate SSIDs.
 */
uint8_t WIFI_Scan(char *data_name)
{
    int i;
    char ssid_name[1024];
    memset(ssid_name, '\0', sizeof(ssid_name));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint16_t number = SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_scan_start(NULL, true);

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

    uint16_t ssid_name_pos = 0;
    uint8_t buffer[32];
    uint16_t total_number_wifi = 0;

    for (i = 0; i < number; i++)
    {
        uint16_t temp_pos = 0;

        memset(buffer, '\0', sizeof(buffer));
        memcpy(buffer, ap_info[i].ssid, strlen((char *)ap_info[i].ssid) + 1);
        buffer[strlen((char *)buffer)] = '\n';

        while (buffer[temp_pos] != '\n')
        {
            *(ssid_name + ssid_name_pos) = buffer[temp_pos];
            ssid_name_pos++;
            temp_pos++;
        }
        *(ssid_name + ssid_name_pos) = '\n';
        ssid_name_pos++;

        ESP_LOGI(WIFI_TAG, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(WIFI_TAG, "RSSI \t\t%d", ap_info[i].rssi);
        print_auth_mode(ap_info[i].authmode);
        if (ap_info[i].authmode != WIFI_AUTH_WEP)
        {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        ESP_LOGI(WIFI_TAG, "Channel \t\t%d\n", ap_info[i].primary);
    }

    deleteDuplicateSubstrings(ssid_name, data_name);

    for (i = 0; i < strlen(data_name); i++)
    {
        if (data_name[i] == '\n')
        {
            data_name[i] = '\r';
            total_number_wifi++;
        }
    }
    data_name[i] = '\0';

    return total_number_wifi;
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/

/**
 * The function `print_auth_mode` prints out the authentication mode based on the input integer value.
 * 
 * @param authmode It looks like the code you provided is a function that prints out the authentication
 * mode based on the input `authmode` value. The `print_auth_mode` function uses a switch statement to
 * determine the authentication mode and then logs the corresponding mode using ESP_LOGI.
 */
static void print_auth_mode(int authmode)
{
    switch (authmode)
    {
    case WIFI_AUTH_OPEN:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_OPEN");
        break;
    case WIFI_AUTH_OWE:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_OWE");
        break;
    case WIFI_AUTH_WEP:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WEP");
        break;
    case WIFI_AUTH_WPA_PSK:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA_PSK");
        break;
    case WIFI_AUTH_WPA2_PSK:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA2_PSK");
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
        break;
    case WIFI_AUTH_ENTERPRISE:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_ENTERPRISE");
        break;
    case WIFI_AUTH_WPA3_PSK:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
        break;
    case WIFI_AUTH_WPA3_ENT_192:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
        break;
    default:
        ESP_LOGI(WIFI_TAG, "Authmode \tWIFI_AUTH_UNKNOWN");
        break;
    }
}

/**
 * The function `print_cipher_type` prints out the names of pairwise and group cipher types based on
 * the input values.
 * 
 * @param pairwise_cipher The `pairwise_cipher` parameter in the `print_cipher_type` function
 * represents the encryption method used for pairwise (individual) communication between a Wi-Fi client
 * and an access point. The function prints out the type of encryption used for pairwise communication
 * based on the value of this parameter.
 * @param group_cipher The `group_cipher` parameter in the `print_cipher_type` function represents the
 * encryption cipher used for group communication in a Wi-Fi network. The function prints out the type
 * of encryption cipher used for both pairwise (individual) and group communication in the Wi-Fi
 * network based on the values of `pairwise
 */
static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    switch (pairwise_cipher)
    {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_AES_CMAC128:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_AES_CMAC128");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(WIFI_TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }

    switch (group_cipher)
    {
    case WIFI_CIPHER_TYPE_NONE:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
        break;
    case WIFI_CIPHER_TYPE_WEP40:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
        break;
    case WIFI_CIPHER_TYPE_WEP104:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
        break;
    case WIFI_CIPHER_TYPE_TKIP:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
        break;
    case WIFI_CIPHER_TYPE_CCMP:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
        break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
        break;
    case WIFI_CIPHER_TYPE_SMS4:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_SMS4");
        break;
    case WIFI_CIPHER_TYPE_GCMP:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP");
        break;
    case WIFI_CIPHER_TYPE_GCMP256:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP256");
        break;
    default:
        ESP_LOGI(WIFI_TAG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
        break;
    }
}

/**
 * The function `isDuplicate` checks if a given string is a duplicate of any of the substrings in a
 * given array.
 *
 * @param str The `str` parameter is a pointer to a character array (string) that you want to check for
 * duplicates within the array of `substrings`.
 * @param substrings The `substrings` parameter in the `isDuplicate` function is an array of strings.
 * Each element in the `substrings` array is a pointer to a character array (string). The function
 * compares the input string `str` with each element in the `substrings` array to check for
 * @param count The `count` parameter in the `isDuplicate` function represents the number of elements
 * in the `substrings` array. It is used to determine the number of iterations needed in the loop to
 * check for duplicates of the `str` parameter within the `substrings` array.
 *
 * @return The function `isDuplicate` returns an unsigned 8-bit integer (uint8_t) value. It returns 1
 * if the input string `str` is found in the array of `substrings`, indicating that it is a duplicate.
 * Otherwise, it returns 0 to indicate that the input string is not a duplicate.
 */
static uint8_t isDuplicate(char *str, char *substrings[], int count)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(str, substrings[i]) == 0)
        {
            return 1; // Duplicate
        }
    }
    return 0; // Not duplicate
}

/**
 * The function `deleteDuplicateSubstrings` removes duplicate substrings from a given string and
 * constructs a new string with unique substrings separated by newline characters.
 *
 * @param str The `deleteDuplicateSubstrings` function takes two parameters:
 * @param result The `result` parameter in the `deleteDuplicateSubstrings` function is a character
 * array where the non-duplicate substrings will be stored after removing any duplicate substrings from
 * the original string. The function will construct a new string by concatenating these non-duplicate
 * substrings along with newline characters ('\
 */
static void deleteDuplicateSubstrings(char *str, char *result)
{
    char *token;
    char *delim = "\n";
    char *substrings[100]; // Two-dimensional array to store non-duplicate substrings
    int count = 0;         // Number of substrings added to the array

    // Parse the original string into substrings and store them in the array
    token = strtok(str, delim);
    while (token != NULL)
    {
        if (!isDuplicate(token, substrings, count))
        {
            substrings[count++] = token;
        }
        token = strtok(NULL, delim);
    }

    // Reconstruct the new string from the non-duplicate substrings
    result[0] = '\0'; // Initialize the new string
    for (int i = 0; i < count; i++)
    {
        strcat(result, substrings[i]); // Concatenate the substring into the new string
        strcat(result, "\n");          // Add '\n' character after each substring
    }
}