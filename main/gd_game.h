// main/gd_game.h —— 一局关卡的会话:输入队列、按歌曲时钟追步、尝试计数、练习检查点(纯 C)。
//
// 时间基准是"歌曲时间":第 tick 步对应歌曲 tick / 240 秒(PRD §7.5)。应用把按键时间换算成
// 步数后入队;每帧调用 gd_game_advance() 追到当前歌曲时间对应的步数。单次追步有上限,
// 落后太多时分几帧追上(不会瞬移,也不会让音乐倒退)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_sim.h"

#define GD_INPUT_RING     32
#define GD_MAX_CKPT       32
#define GD_CATCHUP_MAX    40        // 单次最多追 40 步(约 167 ms)
#define GD_AUTO_CKPT_GROUND 24      // 自动检查点:稳定着地 ≥ 0.1 s
#define GD_AUTO_CKPT_GAP  480       // 自动检查点:距上一个 ≥ 2 s

typedef enum { GD_IN_PRESS = 0, GD_IN_RELEASE, GD_IN_CHECKPOINT } gd_in_kind_t;

typedef struct {
    uint32_t tick;
    uint8_t kind;   // gd_in_kind_t
} gd_input_t;

typedef struct {
    int32_t x, y, vy;
    int8_t grav;
    uint8_t mode;
    bool grounded;
    int16_t rot;
    uint32_t tick;
} gd_ckpt_t;

typedef enum { GD_RUN_PLAYING = 0, GD_RUN_DEAD, GD_RUN_DONE } gd_run_t;

// gd_game_advance() 返回的事件位。
#define GD_EV_DIED       0x01u
#define GD_EV_FINISHED   0x02u
#define GD_EV_CHECKPOINT 0x04u

typedef struct {
    const gd_level_t *lv;
    gd_sim_t sim;
    bool practice;
    uint8_t run;                 // gd_run_t
    bool key_down;               // 跳跃键当前是否按住(跨尝试保留)
    uint32_t attempt;            // 当前是第几次尝试(从 1 开始)
    uint32_t jumps_done;         // 已结束尝试的跳跃数之和
    uint32_t attempt_start_tick; // 本次尝试的起点(练习模式可能是检查点)
    int32_t attempt_start_x;
    gd_ckpt_t ckpt[GD_MAX_CKPT];
    uint8_t ckpt_n;              // 有效检查点数(环形,最新的在 ckpt_head − 1)
    uint8_t ckpt_head;
    uint32_t last_ckpt_tick;
    gd_input_t in[GD_INPUT_RING];
    uint8_t in_head, in_count;
} gd_game_t;

// 开始一局(尝试次数从 1 开始)。
void gd_game_begin(gd_game_t *g, const gd_level_t *lv, bool practice);
// 输入入队(按 tick 顺序到达)。早于当前步的输入在下一步立即生效。队列满时丢弃最旧的。
void gd_game_input(gd_game_t *g, gd_in_kind_t kind, uint32_t tick);
// 追到 target_tick(不含),返回 GD_EV_* 事件位。死亡或通关后不再前进。
uint32_t gd_game_advance(gd_game_t *g, uint32_t target_tick);
// 死亡后重新开始下一次尝试:普通模式回到开头,练习模式回到最近的检查点。
// 返回本次尝试起点对应的歌曲毫秒(音乐从这里开始播放)。
int32_t gd_game_restart(gd_game_t *g);
// 切换练习模式:从关卡开头重新开始,清空检查点(尝试次数照常累加)。
int32_t gd_game_set_practice(gd_game_t *g, bool practice);
// 累计跳跃次数(含当前尝试)。
uint32_t gd_game_jumps(const gd_game_t *g);
// 最近检查点的世界 x(没有时返回 INT32_MIN),供界面绘制。
int32_t gd_game_ckpt_x(const gd_game_t *g, int idx_from_latest);
