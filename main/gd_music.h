// main/gd_music.h —— "music" 数据分区上的音乐包(ESP-IDF)。
//
// 启动时读取并校验包头与曲目表(CRC);分区是擦除态或包无效时进入静音模式(PRD §8.4)。
// 随后由低优先级后台任务逐曲校验数据 CRC,校验失败的曲目按静音处理。
// 读取用 esp_partition_read() 小块进行,不映射整个分区。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gd_mpack.h"

// 挂载音乐包;返回是否得到有效的包(失败时仍可安全调用其余函数)。
bool gd_music_init(void);
// 启动后台数据 CRC 校验(低优先级任务)。
void gd_music_verify_async(void);
// 该曲目可以播放:包有效、曲目存在、数据 CRC 未判定为错误。
bool gd_music_track_ok(uint8_t track);
const gd_mtrack_t *gd_music_track(uint8_t track);
esp_err_t gd_music_read(uint32_t offset, void *buf, size_t len);
// 状态描述(日志用)。
const char *gd_music_status(void);
