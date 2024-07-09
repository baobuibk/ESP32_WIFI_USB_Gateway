/*********************
 *      INCLUDES
 *********************/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_sntp.h"
#include "esp_mac.h" // for MACSTR
#include "esp_partition.h"
#include "esp_check.h"

#include "lwip/dns.h"

#include "mdns.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
// #include "usb_descriptors.h"
#include "tusb.h"

#include <sys/unistd.h>
#include <sys/stat.h>

#include "ftp.h"
#include "wifi.h"
#include "nvs_rw.h"
#include "sd_card.h"

/*********************
 *      DEFINES
 *********************/
#define MAIN_TAG "MAIN"
#define FTP_TASK_FINISH_BIT 1 << 0

#define esp_vfs_fat_spiflash_mount esp_vfs_fat_spiflash_mount_rw_wl
#define esp_vfs_fat_spiflash_unmount esp_vfs_fat_spiflash_unmount_rw_wl
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init

#define CONFIG_MDNS_HOSTNAME "ftp-server"
#define CONFIG_NTP_SERVER	"pool.ntp.org"

#define CONFIG_FTP_USER "esp32"
#define CONFIG_FTP_PASSWORD "esp32"

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN  = 0x80,

    EDPT_MSC_OUT  = 0x01,
    EDPT_MSC_IN   = 0x81,
};


/***********************************
 *           DATA
 ***********************************/

extern char ftp_user[FTP_USER_PASS_LEN_MAX + 1];
extern char ftp_pass[FTP_USER_PASS_LEN_MAX + 1];

/***********************************
 *   PRIVATE DATA
 ***********************************/

static EventGroupHandle_t xEventTask;

static volatile uint8_t ssid[32] = "ptn209b3";
static volatile uint8_t pass[32] = "ptn209b3@";

// static volatile uint8_t ssid[32] = "SONG CA PHE";
// static volatile uint8_t pass[32] = "123456songcaphe";

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // This is Espressif VID. This needs to be changed according to Users / Customers
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
    "TinyUSB",                      // 1: Manufacturer
    "TinyUSB Device",               // 2: Product
    "123456",                       // 3: Serials
    "Example MSC",                  // 4. MSC
};

static uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, TUD_OPT_HIGH_SPEED ? 512 : 64),
};

/***********************************
 *   PRIVATE FUNCTIONS PROTOTYPE
 **********************************/

static uint64_t mp_hal_ticks_ms();
static void initialise_mDNS(void);
static void initialize_sNTP(void);
static esp_err_t obtain_time(void);
static void time_sync_notification_cb(struct timeval *tv);

static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    ESP_LOGI("[usb]", "Storage mounted to application: %s", event->mount_changed_data.is_mounted ? "Yes" : "No");
}

static void _mount(void)
{
    ESP_LOGI(MAIN_TAG, "Mount storage...");
    ESP_ERROR_CHECK(tinyusb_msc_storage_mount(MOUNT_POINT));

    // List all the files in this directory
    ESP_LOGI(MAIN_TAG, "\nls command output:");
    struct dirent *d;
    DIR *dh = opendir(MOUNT_POINT);
    if (!dh) {
        if (errno == ENOENT) {
            //If the directory is not found
            ESP_LOGE(MAIN_TAG, "Directory doesn't exist %s", MOUNT_POINT);
        } else {
            //If the directory is not readable then throw error and exit
            ESP_LOGE(MAIN_TAG, "Unable to read directory %s", MOUNT_POINT);
        }
        return;
    }
    //While the next entry is not readable we will print directory files
    while ((d = readdir(dh)) != NULL) {
        printf("%s\n", d->d_name);
    }
    return;
}

static esp_err_t storage_init_sdmmc(sdmmc_card_t **card)
{
    esp_err_t ret = ESP_OK;
    bool host_init = false;
    sdmmc_card_t *sd_card;

    ESP_LOGI(MAIN_TAG, "Initializing SDCard");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.max_freq_khz = 4000;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // For SD Card, set bus width to use
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.width = 4;
#else
    slot_config.width = 1;
#endif  // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4

    // On chips where the GPIOs used for SD card can be configured, set the user defined values
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = CONFIG_EXAMPLE_PIN_CLK;
    slot_config.cmd = CONFIG_EXAMPLE_PIN_CMD;
    slot_config.d0 = CONFIG_EXAMPLE_PIN_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.d1 = CONFIG_EXAMPLE_PIN_D1;
    slot_config.d2 = CONFIG_EXAMPLE_PIN_D2;
    slot_config.d3 = CONFIG_EXAMPLE_PIN_D3;
#endif  // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4

#endif  // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    sd_card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    ESP_GOTO_ON_FALSE(sd_card, ESP_ERR_NO_MEM, clean, MAIN_TAG, "could not allocate new sdmmc_card_t");

    ESP_GOTO_ON_ERROR((*host.init)(), clean, MAIN_TAG, "Host Config Init fail");
    host_init = true;

    ESP_GOTO_ON_ERROR(sdmmc_host_init_slot(host.slot, (const sdmmc_slot_config_t *) &slot_config),
                      clean, MAIN_TAG, "Host init slot fail");

    while (sdmmc_card_init(&host, sd_card)) {
        ESP_LOGE(MAIN_TAG, "The detection pin of the slot is disconnected(Insert uSD card). Retrying...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, sd_card);
    *card = sd_card;

    return ESP_OK;

clean:
    if (host_init) {
        if (host.flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
            host.deinit_p(host.slot);
        } else {
            (*host.deinit)();
        }
    }
    if (sd_card) {
        free(sd_card);
        sd_card = NULL;
    }
    return ret;
}



/**********************************
 *   PUBLIC FUNCTIONS
 **********************************/


void ftp_task(void *pvParameters)
{

	ESP_LOGI("[Ftp]", "ftp_task start");
	strcpy(ftp_user, CONFIG_FTP_USER);
	strcpy(ftp_pass, CONFIG_FTP_PASSWORD);
	ESP_LOGI("[Ftp]", "ftp_user:[%s] ftp_pass:[%s]", ftp_user, ftp_pass);

	uint64_t elapsed, time_ms = mp_hal_ticks_ms();
	// Initialize ftp, create rx buffer and mutex
	if (!ftp_init())
	{
		ESP_LOGE("[Ftp]", "Init Error");
		xEventGroupSetBits(xEventTask, FTP_TASK_FINISH_BIT);
		vTaskDelete(NULL);
	}

	// We have network connection, enable ftp
	ftp_enable();

	time_ms = mp_hal_ticks_ms();
	while (1)
	{
		// Calculate time between two ftp_run() calls
		elapsed = mp_hal_ticks_ms() - time_ms;
		time_ms = mp_hal_ticks_ms();

		int res = ftp_run(elapsed);
		if (res < 0)
		{
			if (res == -1)
			{
				ESP_LOGE("[Ftp]", "\nRun Error");
			}
			// -2 is returned if Ftp stop was requested by user
			break;
		}

		vTaskDelay(1);

	} // end while

	ESP_LOGW("[Ftp]", "\nTask terminated!");
	xEventGroupSetBits(xEventTask, FTP_TASK_FINISH_BIT);
	vTaskDelete(NULL);
}

void app_main(void)
{
	esp_err_t ret;

	NVS_Init();
	WIFI_StaInit();
	WIFI_Connect((uint8_t *)ssid, (uint8_t *)pass);

	initialise_mDNS();

	// obtain time over NTP
	ESP_LOGI(MAIN_TAG, "Getting time over NTP.");
	ret = obtain_time();
	if (ret != ESP_OK)
	{
		ESP_LOGE(MAIN_TAG, "Fail to getting time over NTP.");
		while (1)
		{
			vTaskDelay(10);
		}
	}

	// Show current date & time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (0 * 60 * 60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(MAIN_TAG, "The local date/time is: %s", strftime_buf);
	ESP_LOGW(MAIN_TAG, "This server manages file timestamps in GMT.");

	// Mount FAT File System on SDCARD
	static sdmmc_card_t *card = NULL;

	ESP_ERROR_CHECK(storage_init_sdmmc(&card));

    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = card,
        .callback_mount_changed = storage_mount_changed_cb,  /* First way to register the callback. This is while initializing the storage. */
        .mount_config.max_files = 5,
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_sdmmc(&config_sdmmc));
    ESP_ERROR_CHECK(tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb));

	_mount();

    ESP_LOGI("[usb]", "USB MSC initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI("[usb]", "USB MSC initialization DONE");

	xEventTask = xEventGroupCreate();
	xTaskCreate(ftp_task, "FTP", 1024*6, NULL, 6, NULL);
	xEventGroupWaitBits( xEventTask,
		FTP_TASK_FINISH_BIT, /* The bits within the event group to wait for. */
		pdTRUE, /* BIT_0 should be cleared before returning. */
		pdFALSE, /* Don't wait for both bits, either bit will do. */
		portMAX_DELAY);/* Wait forever. */
	ESP_LOGE(MAIN_TAG, "ftp_task finish");

	// esp_vfs_fat_sdcard_unmount(MOUNT_POINT, &card);
	// ESP_LOGI(MAIN_TAG, "SDCARD unmounted");
}

/***********************************
 *   PRIVATE FUNCTIONS
 **********************************/

static uint64_t mp_hal_ticks_ms()
{
	uint64_t time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	return time_ms;
}

static void initialise_mDNS(void)
{
	// initialize mDNS
	ESP_ERROR_CHECK(mdns_init());
	// set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_MDNS_HOSTNAME));
	ESP_LOGI(MAIN_TAG, "mdns hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

}

static void initialize_sNTP(void)
{
	ESP_LOGI(MAIN_TAG, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	// sntp_setservername(0, "pool.ntp.org");
	ESP_LOGI(MAIN_TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
	sntp_setservername(0, CONFIG_NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();
}

static esp_err_t obtain_time(void)
{
	initialize_sNTP();
	// wait for time to be set
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
	{
		ESP_LOGI(MAIN_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

	if (retry == retry_count)
		return ESP_FAIL;
	return ESP_OK;
}

static void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(MAIN_TAG, "Notification of a time synchronization event");
}