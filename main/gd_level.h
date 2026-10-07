// main/gd_level.h —— 几何冲刺的世界坐标、物理常量与关卡数据结构(纯 C,主机可测)。
//
// 世界坐标:x 向右,y 向上;地面顶面 y = 0,天花板底面 y = GD_CEIL_PX。
// 关卡是 GD_ROWS 行的网格,1 格 = GD_CELL_PX 像素;第 0 行紧贴地面。
// 位置用 Q12 定点(1 px = 4096),速度单位为"Q12 像素 / 物理步",物理固定 240 Hz。
// ESP32-C3 没有 FPU:全部整数运算,主机、预览与设备逐位一致(求解器的结论在设备上成立)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GD_FP_SHIFT 12
#define GD_FP_ONE   (1 << GD_FP_SHIFT)
#define GD_PX(v)    ((int32_t)(v) * GD_FP_ONE)

#define GD_TICK_HZ   240
#define GD_CELL_PX   20
#define GD_ROWS      9                          // 地面与天花板之间 9 格 = 180 px
#define GD_CEIL_PX   (GD_ROWS * GD_CELL_PX)
#define GD_START_X_PX 10                        // 第 0 步时玩家中心的世界 x

// 由 px/s、px/s² 换算到每步的 Q12 值(编译期整数运算,四舍五入)。
#define GD_PER_TICK(px_per_s)   (((int32_t)(px_per_s) * GD_FP_ONE + GD_TICK_HZ / 2) / GD_TICK_HZ)
#define GD_PER_TICK2(px_per_s2) (((int32_t)(px_per_s2) * GD_FP_ONE + GD_TICK_HZ * GD_TICK_HZ / 2) / \
                                 (GD_TICK_HZ * GD_TICK_HZ))

// —— 物理参数(PRD §7.2;调参只改这里) ——
#define GD_SPEED          GD_PER_TICK(208)      // 水平速度 ≈ 10.4 格/s
#define GD_JUMP_V         GD_PER_TICK(400)      // 起跳速度:跳高 ≈ 42 px
#define GD_GRAVITY        GD_PER_TICK2(1905)    // 平地滞空 ≈ 0.42 s
#define GD_MAX_FALL       GD_PER_TICK(650)
#define GD_PAD_YELLOW_V   (GD_JUMP_V * 135 / 100)
#define GD_ORB_YELLOW_V   GD_JUMP_V
#define GD_BLUE_V         (GD_JUMP_V * 40 / 100) // 蓝环/蓝板/重力门:朝新"下方"的初速度
#define GD_SHIP_UP        GD_PER_TICK2(1100)
#define GD_SHIP_DOWN      GD_PER_TICK2(1000)
#define GD_SHIP_VMAX      GD_PER_TICK(260)

// —— 判定框(px) ——
#define GD_HALF_PX        10                    // 玩家外框 20×20
#define GD_CORE_HALF_PX   3                     // 玩家内核 6×6:碰到实心方块即死亡
#define GD_LAND_TOL_PX    7                     // 着陆吸附容差
#define GD_ORB_R_PX       12
#define GD_ORB_BUFFER     12                    // 跳环按住缓冲:50 ms = 12 步

// 关卡对象。PAD/ORB/PORTAL 的 len 恒为 1;BLOCK/SPIKE 可以是水平连续的一段。
typedef enum {
    GD_OBJ_BLOCK = 0,      // '#' 实心方块
    GD_OBJ_SPIKE_UP,       // '^' 朝上的尖刺(底边贴格子底部)
    GD_OBJ_SPIKE_DOWN,     // 'v' 朝下的尖刺(底边贴格子顶部)
    GD_OBJ_PAD_YELLOW,     // 'y' 黄色跳板
    GD_OBJ_PAD_BLUE,       // 'B' 蓝色跳板(翻转重力)
    GD_OBJ_ORB_YELLOW,     // 'o' 黄色跳环
    GD_OBJ_ORB_BLUE,       // 'b' 蓝色跳环(翻转重力)
    GD_OBJ_PORTAL_FLIP,    // 'G' 蓝色重力门:变为倒立重力
    GD_OBJ_PORTAL_NORMAL,  // 'g' 黄色重力门:恢复正常重力
    GD_OBJ_PORTAL_SHIP,    // 'S' 粉色形态门:飞船
    GD_OBJ_PORTAL_CUBE,    // 'C' 绿色形态门:方块
    GD_OBJ_COUNT,
} gd_obj_type_t;

#define GD_OBJF_CEIL 0x01  // 跳板贴在格子顶部(朝下弹射)

typedef struct {
    uint16_t x;       // 起始列
    uint8_t y;        // 行(0 = 紧贴地面)
    uint8_t type;     // gd_obj_type_t
    uint8_t len;      // 水平连续格数(1..GD_MAX_RUN)
    uint8_t flags;
} gd_obj_t;

#define GD_MAX_RUN 16

typedef enum { GD_DIFF_EASY = 0, GD_DIFF_NORMAL, GD_DIFF_HARD } gd_difficulty_t;

typedef struct {
    const char *name;          // 英文曲名(界面直接显示)
    const char *artist;
    uint8_t difficulty;        // gd_difficulty_t
    uint8_t theme;             // 配色主题编号(gd_theme.h)
    uint8_t track;             // 音乐包中的曲目号(1 起)
    uint16_t bpm_x100;
    uint16_t first_beat_ms;
    uint16_t finish_col;       // 终点线所在列
    uint16_t obj_count;
    const gd_obj_t *objs;      // 按 x 升序
} gd_level_t;

#define GD_LEVEL_COUNT 6
extern const gd_level_t GD_LEVELS[GD_LEVEL_COUNT];

// 第一个"可能覆盖第 col 列及以后"的对象下标(对象按起始列排序,长度不超过 GD_MAX_RUN)。
uint16_t gd_level_first_from(const gd_level_t *lv, int32_t col);

// 世界 x(Q12)对应的步数、步数对应的玩家中心 x。
static inline int32_t gd_tick_x(uint32_t tick) {
    return GD_PX(GD_START_X_PX) + (int32_t)tick * GD_SPEED;
}
static inline int32_t gd_finish_x(const gd_level_t *lv) {
    return GD_PX((int32_t)lv->finish_col * GD_CELL_PX);
}
// 到达终点线所需的步数。
uint32_t gd_level_finish_tick(const gd_level_t *lv);
// 步数 ↔ 歌曲毫秒(向下取整)。
static inline int32_t gd_tick_ms(uint32_t tick) {
    return (int32_t)((int64_t)tick * 1000 / GD_TICK_HZ);
}
static inline uint32_t gd_ms_tick(int32_t ms) {
    return ms <= 0 ? 0u : (uint32_t)((int64_t)ms * GD_TICK_HZ / 1000);
}
