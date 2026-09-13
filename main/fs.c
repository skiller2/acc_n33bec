#include "esp_littlefs.h"
#include "esp_log.h"
#include <dirent.h>

static const char *TAG = "fs";

void fs_init()
{
    esp_vfs_littlefs_conf_t c = {.base_path = "/fs", .partition_label = "storage", .format_if_mount_failed = true};
    esp_err_t err = esp_vfs_littlefs_register(&c);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "LittleFS mount failed (%s), attempting format", esp_err_to_name(err));
        esp_vfs_littlefs_unregister("/fs");
        c.format_if_mount_failed = true;
        err = esp_vfs_littlefs_register(&c);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "LittleFS format/recover failed: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "LittleFS formatted and mounted");
    }

    ESP_LOGI(TAG, "LittleFS mounted at /fs");

    DIR *dir = opendir("/fs");
    if (dir == NULL)
    {
        ESP_LOGW(TAG, "Failed to open /fs directory");
        return;
    }

    struct dirent *entry;
    ESP_LOGI(TAG, "Files in /fs:");
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            ESP_LOGI(TAG, "  - %s", entry->d_name);
        }
    }
    closedir(dir);
}

void fs_format(void)
{
    ESP_LOGI(TAG, "Formatting LittleFS...");
    esp_err_t err = esp_vfs_littlefs_unregister("/fs");
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW(TAG, "unregister: %s", esp_err_to_name(err));
    }
    esp_vfs_littlefs_conf_t c = {.base_path = "/fs", .partition_label = "storage", .format_if_mount_failed = true};
    err = esp_vfs_littlefs_register(&c);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "LittleFS formatted successfully");
}
