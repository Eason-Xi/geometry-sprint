// main/gd_model.h —— 页面导航与菜单状态机(纯 C,主机可测;PRD §4、§5)。
//
// 菜单按键规则:上/下键在 PRESS 时动作(低延迟,且忽略随后的 CLICK),确定键在 CLICK 时动作、
// LONG 为返回,所以一次物理按下只触发一次动作。游戏进行中的跳跃键由应用直接交给 gd_game,
// 不经过这里;这里只处理暂停、结算与菜单。
// 每个操作返回 GD_FX_* 位,应用据此刷新界面、控制音乐与存档。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_save.h"

typedef enum {
    GD_SCR_TITLE = 0,
    GD_SCR_SELECT,
    GD_SCR_PLAY,
    GD_SCR_RESULT,
    GD_SCR_SETTINGS,
    GD_SCR_STATS,
} gd_screen_t;

typedef enum { GD_KEY_UP = 0, GD_KEY_DOWN, GD_KEY_OK } gd_key_t;
typedef enum { GD_KEV_PRESS = 0, GD_KEV_CLICK, GD_KEV_LONG, GD_KEV_RELEASE } gd_kev_t;

typedef enum { GD_PAUSE_RESUME = 0, GD_PAUSE_PRACTICE, GD_PAUSE_RESTART, GD_PAUSE_EXIT, GD_PAUSE_COUNT } gd_pause_item_t;

typedef enum {
    GD_SET_VOLUME = 0,
    GD_SET_SFX,
    GD_SET_BAR,
    GD_SET_BRIGHT,
    GD_SET_OFFSET,
    GD_SET_CLEAR,
    GD_SET_ABOUT,
    GD_SET_COUNT,
} gd_set_item_t;

#define GD_CLEAR_HOLD_MS 1500

// 操作结果
#define GD_FX_SCREEN    0x0001u   // 页面切换(整屏重画)
#define GD_FX_REFRESH   0x0002u   // 当前页面内容变化
#define GD_FX_START     0x0004u   // 开始一局(选中的关卡,model->practice 决定模式)
#define GD_FX_STOP      0x0008u   // 结束这局(停音乐,写入局内统计)
#define GD_FX_PAUSE     0x0010u
#define GD_FX_RESUME    0x0020u
#define GD_FX_RESTART   0x0040u   // 从开头重新开始(尝试 +1)
#define GD_FX_PRACTICE  0x0080u   // 切换练习模式并从开头开始
#define GD_FX_SAVE      0x0100u
#define GD_FX_VOLUME    0x0200u
#define GD_FX_BRIGHT    0x0400u
#define GD_FX_SND_MOVE  0x1000u
#define GD_FX_SND_OK    0x2000u
#define GD_FX_SND_BACK  0x4000u

typedef struct {
    uint32_t attempts;
    uint32_t jumps;
    uint32_t time_ms;        // 通关那一次尝试的用时
    bool first_clear;        // 第一次正式通关
    bool practice;
} gd_result_t;

typedef struct {
    gd_screen_t screen;
    uint8_t level;           // 选中的关卡
    bool paused;
    uint8_t pause_cursor;
    bool practice;           // 当前这局是否练习模式
    uint8_t set_cursor;
    bool set_editing;
    bool about_open;
    bool clear_holding;
    uint32_t clear_hold_start;
    uint32_t cleared_at;     // 刚清除存档的时刻(界面提示用,0 = 无)
    int8_t new_best;         // 本次死亡刷新的最佳进度(−1 = 无),界面提示后清除
    gd_result_t result;
    gd_save_t save;
} gd_model_t;

void gd_model_init(gd_model_t *m);
uint32_t gd_model_key(gd_model_t *m, gd_key_t key, gd_kev_t ev, uint32_t now_ms);
// 游戏中按下暂停(普通模式中键 PRESS,练习模式中键 LONG)。
uint32_t gd_model_pause(gd_model_t *m);
// 新的一次尝试开始(进入关卡、死亡后重开、从暂停菜单重开或切换模式)。
void gd_model_attempt(gd_model_t *m);
// 死亡:progress 为本次尝试到达的百分比。刷新最佳进度时返回 GD_FX_SAVE。
uint32_t gd_model_died(gd_model_t *m, int progress);
// 到达终点:记录结算数据(页面在完成动画后由 gd_model_show_result() 切换)。
uint32_t gd_model_finished(gd_model_t *m, uint32_t attempts, uint32_t jumps, uint32_t time_ms);
uint32_t gd_model_show_result(gd_model_t *m);
// 累加跳跃次数与游戏时长(结束一局或暂停时调用)。
void gd_model_add_stats(gd_model_t *m, uint32_t jumps, uint32_t seconds);
// 周期调用:处理"长按 1.5 秒清除存档"。
uint32_t gd_model_tick(gd_model_t *m, uint32_t now_ms);
