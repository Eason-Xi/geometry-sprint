// main/gd_sfx.h —— 小型音效合成器:方波/三角波/噪声分段 + 包络,叠加到音乐 PCM 上(纯 C)。
//
// 音效不占素材存储:死亡碎裂、通关、检查点与菜单提示音都由这里实时合成(PRD §8.4)。
// 只由音频任务调用;多个音效可以同时发声(最多 GD_SFX_VOICES 个,满了替换最旧的)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GD_SFX_VOICES 4

typedef enum {
    GD_SFX_NONE = 0,
    GD_SFX_MOVE,          // 菜单移动
    GD_SFX_OK,            // 菜单确认
    GD_SFX_BACK,          // 返回
    GD_SFX_DEATH,         // 死亡碎裂
    GD_SFX_CHECKPOINT,    // 放置检查点
    GD_SFX_COMPLETE,      // 关卡完成
    GD_SFX_NEWBEST,       // 新纪录
    GD_SFX_COUNT,
} gd_sfx_id_t;

typedef struct {
    uint8_t id;           // 0 = 空闲
    uint8_t seg;          // 当前段
    uint32_t pos;         // 段内采样位置
    uint32_t phase;
    uint32_t lfsr;
    int16_t noise;
    uint32_t age;
} gd_sfx_voice_t;

typedef struct {
    uint32_t rate;
    uint32_t inc_per_hz;  // 2^32 / rate
    gd_sfx_voice_t v[GD_SFX_VOICES];
    uint32_t clock;
} gd_sfx_t;

void gd_sfx_init(gd_sfx_t *s, uint32_t sample_rate);
void gd_sfx_play(gd_sfx_t *s, uint8_t id);
bool gd_sfx_busy(const gd_sfx_t *s);
void gd_sfx_stop_all(gd_sfx_t *s);
// 把音效叠加到 pcm(饱和截断)。gain 0..256(256 = 原始音量)。
void gd_sfx_mix(gd_sfx_t *s, int16_t *pcm, int n, int gain);
// 音效总时长(ms),用于测试与界面节奏。
uint32_t gd_sfx_duration_ms(uint8_t id);
