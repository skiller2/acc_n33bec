#include "esp_littlefs.h"
void fs_init(){ esp_vfs_littlefs_conf_t c={.base_path="/fs",.partition_label="storage",.format_if_mount_failed=true}; esp_vfs_littlefs_register(&c);}