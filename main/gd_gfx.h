// main/gd_gfx.h —— 自绘画布的绘图辅助(在 LVGL 绘制回调里调用)。
//
// 每个页面只用一个 lv_obj 作画布,在 LV_EVENT_DRAW_MAIN 里按当前时间画出整个画面
// (立即模式)。好处:几乎不占 LVGL 对象池,卡通角色可以用任意形状组合。
// 代价是要自己声明"哪里变了":页面每帧把会动的元素的外接矩形交给 gd_dirty_*,
// 只重绘这些区域。所有 gfx_* 先与当前裁剪区求交,完全不相交的图形不生成绘制任务。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define GD_SCREEN_W 320
#define GD_SCREEN_H 240

// 当前绘制目标(由画布绘制回调设置)。
void gfx_begin(lv_layer_t *layer);
const lv_area_t *gfx_clip(void);
// 后续图形的整体不透明度(0..255),默认 255。
void gfx_opa(uint8_t opa);

// 矩形(可圆角、可描边)。border_w 为 0 时不描边。
void gfx_rect(int x, int y, int w, int h, int radius, uint32_t fill, int border_w, uint32_t border);
// 只描边不填充。
void gfx_frame(int x, int y, int w, int h, int radius, int border_w, uint32_t border);
// 圆/椭圆(椭圆用全圆角矩形近似)。
void gfx_circle(int cx, int cy, int r, uint32_t fill, int border_w, uint32_t border);
void gfx_ellipse(int cx, int cy, int rx, int ry, uint32_t fill, int border_w, uint32_t border);
void gfx_line(int x1, int y1, int x2, int y2, int width, uint32_t color);
void gfx_tri(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);
// 圆弧,角度单位度,0° 指向右、顺时针。
void gfx_arc(int cx, int cy, int r, int start_deg, int end_deg, int width, uint32_t color);
// 竖直渐变矩形(上 → 下)。
void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
// 凸四边形(按顺序给出四个顶点),拆成两个三角形绘制。
void gfx_quad(const int16_t xy[8], uint32_t color);
// 以 (cx, cy) 为中心、边长 size、旋转 deg16(1/16 度)的正方形四个顶点。
void gfx_square_pts(int cx, int cy, int size, int deg16, int16_t out[8]);
// 文本:x/y 为区域左上角,w 为宽度(对齐用),align 为 LV_TEXT_ALIGN_*。
void gfx_text(int x, int y, int w, const char *text, const lv_font_t *font, uint32_t color,
              lv_text_align_t align);
// 带深色描边的文本(四向偏移 1 px 叠画),用于彩色背景上的标题。
void gfx_text_outline(int x, int y, int w, const char *text, const lv_font_t *font, uint32_t color,
                      uint32_t outline, lv_text_align_t align);
// 文本宽度(px)。
int gfx_text_width(const char *text, const lv_font_t *font);

// —— 脏矩形 ——
#define GD_DIRTY_MAX 32
typedef struct {
    lv_area_t a[GD_DIRTY_MAX];
    uint8_t n;
    bool full;
} gd_dirty_t;

void gd_dirty_reset(gd_dirty_t *d);
// 加入一个矩形(自动外扩 2 px 以覆盖抗锯齿边);超出容量时退化为整屏。
void gd_dirty_add(gd_dirty_t *d, int x, int y, int w, int h);
void gd_dirty_full(gd_dirty_t *d);
// 把"上一帧 ∪ 这一帧"的区域交给 LVGL 重绘,然后 prev = cur。
void gd_dirty_flush(lv_obj_t *canvas, gd_dirty_t *prev, const gd_dirty_t *cur);

// —— 小工具 ——
// 0..1000 的缓动(输入 0..1000)。
int gd_ease_out(int t);
int gd_ease_in_out(int t);
// 整数正弦:deg 度 → −1000..1000。
int gd_isin(int deg);
int gd_icos(int deg);
int gd_clamp(int v, int lo, int hi);
