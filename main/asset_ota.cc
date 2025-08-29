#include "asset_ota.h"

#include <string.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_partition.h>
#include <esp_http_client.h>
#include <nvs.h>
#include <nvs_flash.h>

static const char* TAG = "asset_ota";

int asset_ota_set_active_slot(char slot)
{
    if (slot != 'A' && slot != 'B') return -1;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -2;
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    nvs_handle_t nvs;
    err = nvs_open("sys", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return -3;
    }
    char slot_str[2] = {slot, 0};
    err = nvs_set_str(nvs, "asset_slot", slot_str);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        return -4;
    }
    return 0;
}

int asset_ota_download_and_write(const char* url, const char* partition_label)
{
    if (!url || !partition_label) return -1;

    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, partition_label);
    if (!part) {
        ESP_LOGE(TAG, "partition %s not found", partition_label);
        return -2;
    }
    ESP_LOGI(TAG, "write to %s offset=0x%lx size=%u", partition_label, part->address, part->size);

    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 15000 };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -3;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -4;
    }

    int64_t content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0 || content_len > part->size) {
        ESP_LOGE(TAG, "invalid content length: %lld", (long long)content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -5;
    }

    const size_t buf_size = 2048;
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -6;
    }

    // erase partition region
    err = esp_partition_erase_range(part, 0, (size_t)content_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -7;
    }

    size_t written = 0;
    while (written < (size_t)content_len) {
        int r = esp_http_client_read(client, (char*)buf, buf_size);
        if (r < 0) {
            ESP_LOGE(TAG, "read failed");
            free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -8;
        }
        if (r == 0) break;
        err = esp_partition_write(part, written, buf, (size_t)r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "write failed at %u: %s", (unsigned)written, esp_err_to_name(err));
            free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -9;
        }
        written += (size_t)r;
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (written != (size_t)content_len) {
        ESP_LOGE(TAG, "size mismatch: %u != %lld", (unsigned)written, (long long)content_len);
        return -10;
    }

    ESP_LOGI(TAG, "asset OTA done: %u bytes", (unsigned)written);
    return 0;
} 