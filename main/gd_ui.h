// main/gd_ui.h —— 几何冲刺的界面:一个全屏自绘画布 + 各页面的绘制与脏矩形(PRD §5、§9)。
//
// 只在 LVGL 任务里调用(或在其他任务里持有 bsp_lvgl_lock() 时调用)。
// 画布在 LV_EVENT_DRAW_MAIN 中按"本帧快照"立即模式绘制;gd_ui_frame() 每帧先算出本帧快照与
// 变化区域(上一帧 ∪ 这一帧),只让 LVGL 重绘这些区域。游戏背景只用竖直渐变,卷动时只需重绘
// 物体的包围盒,而不是整屏。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_game.h"
#include "gd_model.h"

typedef struct {
    gd_model_t *model;
    gd_game_t *game;                     // 当前这一局(PLAY 页使用)
    bool (*track_ok)(uint8_t track);     // 该曲目是否在音乐包里;NULL = 全部静音
    int (*battery)(void);                // 电量 0..100,−1 = 无;NULL = 不显示
} gd_ui_env_t;

void gd_ui_start(const gd_ui_env_t *env);
// 页面切换(整屏重画)。
void gd_ui_show(void);
// 当前页面内容变化(整屏重画)。
void gd_ui_refresh(void);
// 每帧调用:更新快照并标记变化区域。
void gd_ui_frame(int64_t now_us);
// 屏幕变暗时停止重绘。
void gd_ui_set_dimmed(bool dimmed);

// —— 游戏页事件(由应用在对应时刻调用) ——
void gd_ui_play_begin(int64_t now_us);          // 新的一次尝试开始
void gd_ui_play_died(int64_t now_us);
void gd_ui_play_finished(int64_t now_us);
void gd_ui_play_checkpoint(int64_t now_us);
void gd_ui_play_hint(const char *text, int64_t now_us, int32_t ms);

#define GD_DEATH_MS     600    // 死亡碎裂动画时长,结束后自动重开
#define GD_COMPLETE_MS  1500   // 关卡完成动画时长,结束后进入结算
