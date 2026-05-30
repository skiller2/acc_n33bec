#include "esp_littlefs.h"
#include "esp_log.h"
#include <dirent.h>

static const char *TAG = "fs";

void fs_init()
{
    esp_vfs_littlefs_conf_t c = {.base_path = "/fs", .partition_label = "storage", .format_if_mount_failed = true};
    esp_vfs_littlefs_register(&c);
    
    ESP_LOGI(TAG, "LittleFS mounted at /fs");
    
    // List files in /fs
    DIR *dir = opendir("/fs");
    if (dir == NULL) {
        ESP_LOGW(TAG, "Failed to open /fs directory");
        return;
    }
    
    struct dirent *entry;
    ESP_LOGI(TAG, "Files in /fs:");
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            ESP_LOGI(TAG, "  - %s", entry->d_name);
        }
    }
    closedir(dir);
}
