// main/gd_theme.h —— 配色、版面常量与按键位置(横屏 320 × 240,侧键朝上)。
#pragma once

#include <stdint.h>

#include "bsp_pins.h"

// —— 版面:天花板带 20 px(内含进度条),可玩区 180 px,地面带 40 px ——
#define GD_CEIL_BAND_H   20
#define GD_GROUND_Y      200          // 地面顶面(世界 y = 0)在屏幕上的 y
#define GD_PLAYER_SX     90           // 玩家中心的屏幕 x(左缘在第 4 格)

// —— 通用色 ——
#define GD_C_BLACK       0x000000
#define GD_C_WHITE       0xFFFFFF
#define GD_C_INK         0x0A0A14     // 方块、尖刺的填充
#define GD_C_NIGHT       0x0C1230     // 菜单深色背景
#define GD_C_NIGHT_2     0x1B2A6B
#define GD_C_MUTED       0x9AA6D6
#define GD_C_PANEL       0x111A3F
#define GD_C_GOLD        0xFFD400
#define GD_C_CYAN        0x00E5FF
#define GD_C_GREEN       0x4CE07A
#define GD_C_PINK        0xFF4FC3
#define GD_C_BLUE        0x3BA7FF
#define GD_C_RED         0xFF4B4B
#define GD_C_ORANGE      0xFF8A1F

// 玩家图标
#define GD_C_PLAYER_1    GD_C_GOLD
#define GD_C_PLAYER_2    GD_C_CYAN

typedef struct {
    uint32_t bg_top, bg_bottom;   // 可玩区竖直渐变
    uint32_t dark;                // 天花板带与卡片暗部
    uint32_t ground;              // 地面带
    uint32_t line;                // 地面线、进度条
    uint32_t edge;                // 方块与尖刺的描边
} gd_palette_t;

// 每关一个主题(关卡头里的 theme 字段)。
static const gd_palette_t GD_PALETTES[6] = {
    { 0x2F6BF0, 0x1C3FA6, 0x0F2466, 0x1A3896, 0x8FD3FF, 0xC9EBFF },   // 蓝
    { 0x9447E8, 0x5B21B6, 0x2E0F5E, 0x4C1D95, 0xE5B8FF, 0xF2DDFF },   // 紫
    { 0x22B05A, 0x12703A, 0x08371C, 0x15703A, 0xA8F5C2, 0xD6FFE3 },   // 绿
    { 0xF06A20, 0xA8380F, 0x4E1A07, 0x8A2E0C, 0xFFD3A8, 0xFFE8D1 },   // 橙
    { 0x109CBD, 0x0B5E74, 0x05303C, 0x0F6B82, 0x9DEBFF, 0xD2F7FF },   // 青
    { 0xDB2F83, 0x8C1650, 0x44081F, 0x9D174D, 0xFFB3D6, 0xFFDCEB },   // 洋红
};

// —— 侧键在顶边的位置(横屏侧键朝上时 x = 竖屏 y,见 bsp_pins.h) ——
#define GD_KEY_X_UP   (((const int[3])BSP_BTN_EDGE_POS_TABLE)[0])
#define GD_KEY_X_DOWN (((const int[3])BSP_BTN_EDGE_POS_TABLE)[1])
#define GD_KEY_X_OK   (((const int[3])BSP_BTN_EDGE_POS_TABLE)[2])
