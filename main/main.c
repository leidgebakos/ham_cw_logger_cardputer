#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_system.h"

#define FIRMWARE_VERSION "0.1.0-smoke"

static const char *TAG = "cw_logger";

static void print_chip_info(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;
    uint8_t mac[6] = {0};

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);

    const char *chip_name = "unknown";
    if (chip_info.model == CHIP_ESP32S3) {
        chip_name = "ESP32-S3";
    }

    ESP_LOGI(TAG, "Chip       : %s revision %d", chip_name, chip_info.revision);
    ESP_LOGI(TAG, "CPU        : %d core(s) @ %d MHz", chip_info.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "Features   : Wi-Fi=%s BLE=%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
             (chip_info.features & CHIP_FEATURE_BLE) ? "yes" : "no");
    ESP_LOGI(TAG, "Flash      : %" PRIu32 " bytes", flash_size);
    ESP_LOGI(TAG, "eFuse MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void print_memory_info(void)
{
    ESP_LOGI(TAG, "Heap       : %" PRIu32 " free / %" PRIu32 " minimum",
             (uint32_t)esp_get_free_heap_size(),
             (uint32_t)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Internal   : %" PRIu32 " free",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM      : %" PRIu32 " free",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "HAM CW Logger - Cardputer ADV");
    ESP_LOGI(TAG, "Firmware   : %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "ESP-IDF    : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    print_chip_info();
    print_memory_info();

    ESP_LOGI(TAG, "Platform smoke test completed");
}
