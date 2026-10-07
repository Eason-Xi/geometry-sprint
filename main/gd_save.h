// main/gd_save.h —— 存档内容与二进制格式(纯 C,主机可测;NVS 读写在 gd_store.c)。
//
// 格式(小端):u8 版本 | u8 关卡数 | 每关 12 字节(最佳进度、练习最佳、标志、保留、尝试数 u32、跳跃数 u32)|
//              u32 累计游戏秒数 | 设置 6 字节 | u8 标志 | u32 CRC32(之前所有字节)。
// 读取时校验版本、长度与 CRC,字段越界一律钳位到合法范围(PRD §10)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gd_level.h"

#define GD_SAVE_VERSION 1
#define GD_SAVE_BYTES   (2 + GD_LEVEL_COUNT * 12 + 4 + 6 + 1 + 4)

#define GD_VOLUME_MAX     10
#define GD_VOLUME_DEFAULT 6
#define GD_BRIGHT_LEVELS  3
#define GD_OFFSET_LIMIT   100   // 音画偏移 ±100 ms
#define GD_OFFSET_STEP    10

typedef struct {
    uint8_t best;            // 普通模式最佳进度 0..100
    uint8_t best_practice;   // 练习模式最佳进度
    bool completed;          // 正式通关
    bool practice_done;      // 练习模式通关
    uint32_t attempts;
    uint32_t jumps;
} gd_level_rec_t;

typedef struct {
    uint8_t volume;          // 0..10
    bool sfx;                // 音效开关
    bool progress_bar;       // 游戏中显示进度条
    uint8_t brightness;      // 0 低 / 1 中 / 2 高
    int16_t offset_ms;       // 音画偏移:正值 = 画面提前(判定按更晚的歌曲时间)
} gd_settings_t;

typedef struct {
    gd_level_rec_t lv[GD_LEVEL_COUNT];
    uint32_t play_seconds;
    gd_settings_t set;
    bool hint_shown;         // 首次引导已经显示过
} gd_save_t;

void gd_save_defaults(gd_save_t *s);
// 清除进度与统计,保留设置。
void gd_save_clear_progress(gd_save_t *s);
// 编码到 buf(至少 GD_SAVE_BYTES),返回字节数。
size_t gd_save_encode(const gd_save_t *s, uint8_t *buf);
// 解码;格式不对或 CRC 错误时返回 false 并把 out 置为默认值。
bool gd_save_decode(const uint8_t *buf, size_t len, gd_save_t *out);
// 背光百分比(按亮度档位)。
uint8_t gd_save_backlight(uint8_t brightness);
