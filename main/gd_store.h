// main/gd_store.h —— 存档的 NVS 读写(ESP-IDF;格式见 gd_save.h)。
#pragma once

#include <stdbool.h>

#include "gd_save.h"

// 初始化 NVS(必要时擦除重建)。失败时存档功能不可用,游戏照常运行。
bool gd_store_init(void);
// 读取存档;没有或损坏时返回 false,out 为默认值。
bool gd_store_load(gd_save_t *out);
// 写入存档(调用方须保证此时没有音乐在播放,见 gd_audio_quiet())。
bool gd_store_save(const gd_save_t *s);
