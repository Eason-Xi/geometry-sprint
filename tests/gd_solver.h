// tests/gd_solver.h —— 关卡可通关证明与容错窗口测量(只在主机上运行)。
//
// 求解器用与固件相同的 gd_sim 物理,每 4 步(60 Hz)决定一次"按住/松开",深度优先搜索,
// 先试"松开";完全相同的模拟状态只展开一次。找到的输入序列以按键沿(按下/松开所在步)表示,
// 既是通关证明,也是可回放的录像(gd_replays_data.c、设备自动游玩与主机预览都用它)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_replay.h"
#include "gd_sim.h"

typedef struct {
    gd_edge_t *edges;
    int count;
    uint32_t finish_tick;
    long nodes;      // 搜索展开的节点数
} gd_solution_t;

// 搜索一条通关输入。成功时 out->edges 为 malloc 的数组(调用方 free)。
bool gd_solve(const gd_level_t *lv, gd_solution_t *out, long max_nodes);
// 回放按键沿直到通关、死亡或 max_tick;返回是否通关。final 可为 NULL。
bool gd_replay(const gd_level_t *lv, const gd_edge_t *edges, int count, uint32_t max_tick, gd_sim_t *final);

typedef struct {
    int edge;            // 窗口最小的按下沿下标
    uint32_t tick;       // 该按下沿所在步
    int min_ticks;       // 最小容错窗口(步)
    int median_ticks;    // 方块形态下按下沿窗口的中位数
    int measured;        // 参与统计的按下沿数
} gd_window_t;

// 逐个平移方块形态下的每个按下沿(其余输入不变),统计仍能存活并与原轨迹会合的连续平移范围。
// 把每组方块按键平移到其可行范围的中点(逐组进行,保证整关仍可通关),返回移动的组数。
// 求解器总是"能晚就晚",居中后的录像更接近真人操作,窗口统计也更有代表性。
int gd_center_edges(const gd_level_t *lv, gd_edge_t *edges, int count);
// per_edge 非空时写入每个按键沿的窗口(步;未统计的沿为 −1),长度为 count。
gd_window_t gd_measure_windows(const gd_level_t *lv, const gd_edge_t *edges, int count, int *per_edge);
