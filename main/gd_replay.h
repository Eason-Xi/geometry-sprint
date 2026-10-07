// main/gd_replay.h —— 关卡通关录像(按键沿序列)。
//
// 由 tools/gen_gd_levels.py 调用主机求解器生成(main/gd_replays_data.c),是每关可通关的证明;
// 主机预览与设备"自动游玩"调试模式(GD_AUTOPLAY)回放它,在固定场景下测量性能。
#pragma once

#include <stdint.h>

#include "gd_level.h"

typedef struct {
    uint32_t tick;   // 在第 tick 步之前设置按键状态
    uint8_t down;    // 1 = 按下,0 = 松开
} gd_edge_t;

typedef struct {
    const gd_edge_t *edges;
    uint16_t count;
    uint32_t finish_tick;
} gd_replay_t;

extern const gd_replay_t GD_REPLAYS[GD_LEVEL_COUNT];
