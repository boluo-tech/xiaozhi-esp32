#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 下载指定URL的资产镜像并写入到指定分区标签（如 "assets_B"）。
// 返回0表示成功，非0表示失败。
int asset_ota_download_and_write(const char* url, const char* partition_label);

// 设置 NVS 键 "asset_slot" 为 'A' 或 'B'，返回0成功
int asset_ota_set_active_slot(char slot);

#ifdef __cplusplus
}
#endif 