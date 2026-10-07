// main/gd_sim.h —— 一次尝试的物理模拟:240 Hz 固定步长、整数定点、确定性(纯 C)。
//
// 输入只有"跳跃键是否按住"。gd_sim_set_held() 在两步之间调用,按下沿记为本步的 press;
// gd_sim_step() 前进一步:速度 → 位移 → 地面/天花板 → 方块与尖刺 → 跳板/跳环/传送门 → 起跳。
// 同一输入序列总是得到同一条轨迹,与调用频率无关(PRD §7.2 不变量 3)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_level.h"

#define GD_MAX_OBJS 1024     // 每关对象上限(gen_gd_levels.py 同样检查)

typedef enum { GD_MODE_CUBE = 0, GD_MODE_SHIP } gd_mode_t;

typedef struct {
    int32_t x, y;            // 玩家中心(Q12 px)
    int32_t vy;              // 竖直速度(Q12 px/步,向上为正)
    int8_t grav;             // +1 正常重力,−1 倒立
    uint8_t mode;            // gd_mode_t
    bool grounded;           // 本步结束时贴着"脚下"的表面
    bool held;               // 跳跃键按住
    bool press;              // 待处理的按下沿(下一步消费)
    bool dead;
    bool finished;
    uint32_t tick;           // 已完成的步数(歌曲时间 = tick / 240 s)
    uint32_t press_tick;     // 最近一次按下沿所在步
    uint32_t jumps;          // 本次尝试的起跳次数(地面起跳 + 跳环)
    uint16_t grounded_ticks; // 连续着地步数
    int16_t rot;             // 方块的视觉旋转角(1/16 度,0..5759)
    int16_t orb_prev;        // 上一步重叠的跳环下标(−1 = 无)
    int16_t pad_prev;
    int16_t portal_prev;
    int32_t death_x, death_y;
    uint32_t used[GD_MAX_OBJS / 32];   // 本次尝试已触发的跳环
} gd_sim_t;

// 关卡开头的初始状态。
void gd_sim_reset(gd_sim_t *s);
// 设置跳跃键状态(两步之间调用)。从松开变为按下时记一次按下沿。
void gd_sim_set_held(gd_sim_t *s, bool down);
// 前进一步。死亡或到达终点后不再变化。
void gd_sim_step(gd_sim_t *s, const gd_level_t *lv);
// 进度百分比 0..100(按玩家 x 相对终点线)。
int gd_sim_progress(const gd_sim_t *s, const gd_level_t *lv);
// 清除"跳环已触发"标记(练习模式回到检查点时调用)。
void gd_sim_clear_used(gd_sim_t *s);
