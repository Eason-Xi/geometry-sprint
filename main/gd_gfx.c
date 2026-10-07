// main/gd_gfx.c —— 自绘画布的绘图辅助,说明见 gd_gfx.h。
#include "gd_gfx.h"

#include <string.h>

static lv_layer_t *s_layer;
static uint8_t s_opa = LV_OPA_COVER;

void gfx_begin(lv_layer_t *layer) {
    s_layer = layer;
    s_opa = LV_OPA_COVER;
}

const lv_area_t *gfx_clip(void) {
    return &s_layer->_clip_area;
}

void gfx_opa(uint8_t opa) {
    s_opa = opa;
}

// 与裁剪区不相交的图形直接跳过,避免为屏外元素分配绘制任务。
static bool visible(int x1, int y1, int x2, int y2) {
    if (!s_layer || s_opa <= LV_OPA_MIN) return false;
    const lv_area_t *c = &s_layer->_clip_area;
    return !(x2 < c->x1 || x1 > c->x2 || y2 < c->y1 || y1 > c->y2);
}

void gfx_rect(int x, int y, int w, int h, int radius, uint32_t fill, int border_w, uint32_t border) {
    if (w <= 0 || h <= 0 || !visible(x, y, x + w - 1, y + h - 1)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = lv_color_hex(fill);
    d.bg_opa = s_opa;
    if (border_w > 0) {
        d.border_width = border_w;
        d.border_color = lv_color_hex(border);
        d.border_opa = s_opa;
    }
    const lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(s_layer, &d, &a);
}

void gfx_frame(int x, int y, int w, int h, int radius, int border_w, uint32_t border) {
    if (w <= 0 || h <= 0 || border_w <= 0 || !visible(x, y, x + w - 1, y + h - 1)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_width = border_w;
    d.border_color = lv_color_hex(border);
    d.border_opa = s_opa;
    const lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(s_layer, &d, &a);
}

void gfx_circle(int cx, int cy, int r, uint32_t fill, int border_w, uint32_t border) {
    gfx_rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, LV_RADIUS_CIRCLE, fill, border_w, border);
}

void gfx_ellipse(int cx, int cy, int rx, int ry, uint32_t fill, int border_w, uint32_t border) {
    gfx_rect(cx - rx, cy - ry, 2 * rx + 1, 2 * ry + 1, LV_RADIUS_CIRCLE, fill, border_w, border);
}

void gfx_line(int x1, int y1, int x2, int y2, int width, uint32_t color) {
    const int pad = width / 2 + 1;
    const int lx = x1 < x2 ? x1 : x2, hx = x1 < x2 ? x2 : x1;
    const int ly = y1 < y2 ? y1 : y2, hy = y1 < y2 ? y2 : y1;
    if (!visible(lx - pad, ly - pad, hx + pad, hy + pad)) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.width = width;
    d.color = lv_color_hex(color);
    d.opa = s_opa;
    d.round_start = 1;
    d.round_end = 1;
    lv_draw_line(s_layer, &d);
}

void gfx_tri(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color) {
    int lx = x1, hx = x1, ly = y1, hy = y1;
    if (x2 < lx) lx = x2;
    if (x3 < lx) lx = x3;
    if (x2 > hx) hx = x2;
    if (x3 > hx) hx = x3;
    if (y2 < ly) ly = y2;
    if (y3 < ly) ly = y3;
    if (y2 > hy) hy = y2;
    if (y3 > hy) hy = y3;
    if (!visible(lx, ly, hx, hy)) return;
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0].x = x1;
    d.p[0].y = y1;
    d.p[1].x = x2;
    d.p[1].y = y2;
    d.p[2].x = x3;
    d.p[2].y = y3;
    d.color = lv_color_hex(color);
    d.opa = s_opa;
    lv_draw_triangle(s_layer, &d);
}

void gfx_arc(int cx, int cy, int r, int start_deg, int end_deg, int width, uint32_t color) {
    if (r <= 0 || !visible(cx - r, cy - r, cx + r, cy + r)) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r;
    d.start_angle = start_deg;
    d.end_angle = end_deg;
    d.width = width;
    d.color = lv_color_hex(color);
    d.opa = s_opa;
    d.rounded = 1;
    lv_draw_arc(s_layer, &d);
}

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0 || !visible(x, y, x + w - 1, y + h - 1)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = s_opa;
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = lv_color_hex(top);
    d.bg_grad.stops[0].opa = LV_OPA_COVER;
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].color = lv_color_hex(bottom);
    d.bg_grad.stops[1].opa = LV_OPA_COVER;
    d.bg_grad.stops[1].frac = 255;
    const lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(s_layer, &d, &a);
}

void gfx_quad(const int16_t xy[8], uint32_t color) {
    gfx_tri(xy[0], xy[1], xy[2], xy[3], xy[4], xy[5], color);
    gfx_tri(xy[0], xy[1], xy[4], xy[5], xy[6], xy[7], color);
}

void gfx_square_pts(int cx, int cy, int size, int deg16, int16_t out[8]) {
    // 半对角线向量按角度旋转;角度取整到 1 度,用整数正弦表。
    const int deg = deg16 / 16;
    const int c = gd_icos(deg), s = gd_isin(deg);
    const int h = size;   // 以 1/2 像素为单位的半边长 × 2 = size
    static const int8_t CORNER[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
        const int px = CORNER[i][0] * h, py = CORNER[i][1] * h;   // ×2 精度
        const int rx = (px * c - py * s) / 1000, ry = (px * s + py * c) / 1000;
        out[2 * i] = (int16_t)(cx + (rx >= 0 ? (rx + 1) / 2 : (rx - 1) / 2));
        out[2 * i + 1] = (int16_t)(cy + (ry >= 0 ? (ry + 1) / 2 : (ry - 1) / 2));
    }
}

void gfx_text(int x, int y, int w, const char *text, const lv_font_t *font, uint32_t color,
              lv_text_align_t align) {
    if (!text || !*text || !font) return;
    const int h = lv_font_get_line_height(font);
    if (!visible(x, y, x + w - 1, y + h - 1)) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = text;
    d.text_local = 1;          // 文本可能在栈上:让 LVGL 复制一份
    d.font = font;
    d.color = lv_color_hex(color);
    d.opa = s_opa;
    d.align = align;
    const lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_label(s_layer, &d, &a);
}

void gfx_text_outline(int x, int y, int w, const char *text, const lv_font_t *font, uint32_t color,
                      uint32_t outline, lv_text_align_t align) {
    static const int8_t OFF[8][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 },
                                      { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    for (int i = 0; i < 8; i++) gfx_text(x + OFF[i][0], y + OFF[i][1], w, text, font, outline, align);
    gfx_text(x, y, w, text, font, color, align);
}

int gfx_text_width(const char *text, const lv_font_t *font) {
    if (!text || !font) return 0;
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// ---------------------------------------------------------------------------
// 脏矩形
// ---------------------------------------------------------------------------
void gd_dirty_reset(gd_dirty_t *d) {
    d->n = 0;
    d->full = false;
}

void gd_dirty_full(gd_dirty_t *d) {
    d->full = true;
}

void gd_dirty_add(gd_dirty_t *d, int x, int y, int w, int h) {
    if (d->full || w <= 0 || h <= 0) return;
    lv_area_t a = { x - 2, y - 2, x + w + 1, y + h + 1 };
    if (a.x2 < 0 || a.y2 < 0 || a.x1 >= GD_SCREEN_W || a.y1 >= GD_SCREEN_H) return;
    // 与已有矩形重叠较多时合并,减少 LVGL 的刷新区域数。
    for (uint8_t i = 0; i < d->n; i++) {
        lv_area_t *b = &d->a[i];
        if (a.x1 <= b->x2 + 8 && a.x2 >= b->x1 - 8 && a.y1 <= b->y2 + 8 && a.y2 >= b->y1 - 8) {
            if (a.x1 < b->x1) b->x1 = a.x1;
            if (a.y1 < b->y1) b->y1 = a.y1;
            if (a.x2 > b->x2) b->x2 = a.x2;
            if (a.y2 > b->y2) b->y2 = a.y2;
            return;
        }
    }
    if (d->n >= GD_DIRTY_MAX) {
        d->full = true;
        return;
    }
    d->a[d->n++] = a;
}

void gd_dirty_flush(lv_obj_t *canvas, gd_dirty_t *prev, const gd_dirty_t *cur) {
    if (!canvas) return;
    if (prev->full || cur->full) {
        lv_obj_invalidate(canvas);
    } else {
        for (uint8_t i = 0; i < prev->n; i++) lv_obj_invalidate_area(canvas, &prev->a[i]);
        for (uint8_t i = 0; i < cur->n; i++) lv_obj_invalidate_area(canvas, &cur->a[i]);
    }
    *prev = *cur;
}

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
int gd_clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int gd_ease_out(int t) {
    t = gd_clamp(t, 0, 1000);
    const int u = 1000 - t;
    return 1000 - u * u / 1000 * u / 1000;    // 1 − (1−t)³
}

int gd_ease_in_out(int t) {
    t = gd_clamp(t, 0, 1000);
    if (t < 500) return 4 * t * t / 1000 * t / 1000;     // 4t³
    const int u = 2000 - 2 * t;                           // 1 − (2 − 2t)³ / 2
    return 1000 - u * u / 1000 * u / 2000;
}

// 0..90° 的正弦表(×1000)。
static const int16_t SIN90[91] = {
    0, 17, 35, 52, 70, 87, 105, 122, 139, 156, 174, 191, 208, 225, 242, 259, 276, 292, 309, 326,
    342, 358, 375, 391, 407, 423, 438, 454, 469, 485, 500, 515, 530, 545, 559, 574, 588, 602, 616,
    629, 643, 656, 669, 682, 695, 707, 719, 731, 743, 755, 766, 777, 788, 799, 809, 819, 829, 839,
    848, 857, 866, 875, 883, 891, 899, 906, 914, 921, 927, 934, 940, 946, 951, 956, 961, 966, 970,
    974, 978, 982, 985, 988, 990, 993, 995, 996, 998, 999, 999, 1000, 1000,
};

int gd_isin(int deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    if (deg <= 90) return SIN90[deg];
    if (deg <= 180) return SIN90[180 - deg];
    if (deg <= 270) return -SIN90[deg - 180];
    return -SIN90[360 - deg];
}

int gd_icos(int deg) {
    return gd_isin(deg + 90);
}
