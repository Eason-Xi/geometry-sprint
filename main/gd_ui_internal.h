// main/gd_ui_internal.h —— 界面各页面之间共享的内部接口(只在 gd_ui*.c 中使用)。
#pragma once

#include "gd_gfx.h"
#include "gd_theme.h"
#include "gd_ui.h"

// 一帧的时间快照:同一帧的多次分块绘制必须看到相同的时间。
typedef struct {
    int64_t now_us;
    int32_t ui_ms;           // 单调界面时间
    int32_t page_ms;         // 进入当前页面后经过的时间
} gd_frame_t;

extern const gd_ui_env_t *g_env;
extern gd_frame_t g_frame;

const gd_level_t *ui_level(void);
const gd_palette_t *ui_palette(int level);

// —— 通用控件 ——
// 顶边三个按键标签(对准实体键);text 为 NULL 的键不画。
void ui_key_tags(const char *up, const char *down, const char *ok, uint32_t fill, uint32_t ink);
// 右上角电量(读不到时不画)。
void ui_battery(void);
// 难度标签文字与颜色。
const char *ui_diff_name(uint8_t diff);
uint32_t ui_diff_color(uint8_t diff);
// 方块角色(旋转角 1/16 度)。
void ui_draw_cube(int cx, int cy, int size, int deg16);

// —— 各页面 ——
void ui_title_draw(void);
void ui_title_dirty(gd_dirty_t *d);
void ui_select_draw(void);
void ui_select_dirty(gd_dirty_t *d);
void ui_select_changed(int from, int to);
void ui_result_draw(void);
void ui_settings_draw(void);
void ui_settings_dirty(gd_dirty_t *d);
void ui_stats_draw(void);
void ui_play_enter(void);
void ui_play_draw(void);
void ui_play_dirty(gd_dirty_t *d);
