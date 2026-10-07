// main/gd_ui_play.c —— 游戏页:卷轴场景、角色、死亡碎裂、进度条、暂停菜单与提示横幅。
//
// 每帧在 ui_play_dirty() 里从 gd_game 取一次快照(s_v),绘制回调只读快照。
// 可玩区背景是竖直渐变(同一行颜色相同),卷动时只把物体新旧位置的包围盒标为变化区域。
#include <stdio.h>

#include "gd_fonts.h"
#include "gd_strings.h"
#include "gd_ui_internal.h"

#define PLAY_TOP     GD_CEIL_BAND_H
#define PLAY_H       (GD_GROUND_Y - GD_CEIL_BAND_H)
#define TICK_GAP     40
#define PARTICLES    10
#define BANNER_IN_MS 280

typedef struct {
    int cam_x;            // 屏幕 x = 0 处的世界 x(px)
    int px, py;           // 玩家中心(屏幕)
    int rot;              // 1/16 度
    int vy_pxs;           // 竖直速度(px/s,向上为正)
    uint8_t mode;
    int8_t grav;
    bool held;
    bool dead, done;
    int pct;
    int32_t death_ms;     // 死亡后经过的时间(未死亡为 −1)
    int32_t done_ms;
    int attempt_sx;       // "第 N 次尝试"文字的屏幕 x
    uint32_t attempt;
    bool practice;
} view_t;

static view_t s_v;
static int64_t s_death_us, s_finish_us, s_hint_us, s_ckpt_us;
static int32_t s_hint_ms;
static const char *s_hint;
static int s_last_pct = -1;
static bool s_last_practice;

static const gd_palette_t *pal(void) {
    return ui_palette(g_env->model->level);
}

static int32_t since_ms(int64_t t_us) {
    return t_us ? (int32_t)((g_frame.now_us - t_us) / 1000) : -1;
}

// ---------------------------------------------------------------------------
// 事件
// ---------------------------------------------------------------------------
void gd_ui_play_begin(int64_t now_us) {
    (void)now_us;
    s_death_us = 0;
    s_finish_us = 0;
    gd_ui_refresh();   // 镜头跳回起点:整屏重画
}

void gd_ui_play_died(int64_t now_us) {
    s_death_us = now_us ? now_us : 1;
}

void gd_ui_play_finished(int64_t now_us) {
    s_finish_us = now_us ? now_us : 1;
}

void gd_ui_play_checkpoint(int64_t now_us) {
    s_ckpt_us = now_us ? now_us : 1;
}

void gd_ui_play_hint(const char *text, int64_t now_us, int32_t ms) {
    s_hint = text;
    s_hint_us = now_us ? now_us : 1;
    s_hint_ms = ms;
}

void ui_play_enter(void) {
    s_death_us = s_finish_us = s_ckpt_us = 0;
    s_last_pct = -1;
}

// ---------------------------------------------------------------------------
// 快照
// ---------------------------------------------------------------------------
static void update_view(void) {
    const gd_game_t *g = g_env->game;
    const gd_sim_t *s = &g->sim;
    const int wx = s->x >> GD_FP_SHIFT;
    s_v.cam_x = wx - GD_PLAYER_SX;
    s_v.px = GD_PLAYER_SX;
    s_v.py = GD_GROUND_Y - (s->y >> GD_FP_SHIFT);
    s_v.rot = s->rot;
    s_v.vy_pxs = (int)((int64_t)s->vy * GD_TICK_HZ / GD_FP_ONE);
    s_v.mode = s->mode;
    s_v.grav = s->grav;
    s_v.held = s->held;
    s_v.dead = s->dead;
    s_v.done = s->finished;
    s_v.pct = gd_sim_progress(s, g->lv);
    s_v.death_ms = s->dead ? since_ms(s_death_us) : -1;
    s_v.done_ms = s->finished ? since_ms(s_finish_us) : -1;
    s_v.attempt_sx = (g->attempt_start_x >> GD_FP_SHIFT) + 100 - s_v.cam_x;
    s_v.attempt = g->attempt;
    s_v.practice = g->practice;
    if (s->dead) {
        s_v.px = (s->death_x >> GD_FP_SHIFT) - s_v.cam_x;
        s_v.py = GD_GROUND_Y - (s->death_y >> GD_FP_SHIFT);
    }
}

// 对象在屏幕上的外接矩形;不可见返回 false。
static bool obj_rect(const gd_obj_t *o, int *x, int *y, int *w, int *h) {
    *x = o->x * GD_CELL_PX - s_v.cam_x;
    *y = GD_GROUND_Y - (o->y + 1) * GD_CELL_PX;
    *w = o->len * GD_CELL_PX;
    *h = GD_CELL_PX;
    switch (o->type) {
    case GD_OBJ_ORB_YELLOW:
    case GD_OBJ_ORB_BLUE:
        *x -= 3;
        *y -= 3;
        *w += 6;
        *h += 6;
        break;
    case GD_OBJ_PORTAL_FLIP:
    case GD_OBJ_PORTAL_NORMAL:
    case GD_OBJ_PORTAL_SHIP:
    case GD_OBJ_PORTAL_CUBE:
        *y -= 22;
        *h = 64;
        break;
    default:
        break;
    }
    return *x + *w > -4 && *x < GD_SCREEN_W + 4;
}

typedef void (*obj_fn)(const gd_obj_t *o, int idx, int x, int y, int w, int h);

static void for_visible(obj_fn fn) {
    const gd_level_t *lv = g_env->game->lv;
    const int first_col = s_v.cam_x / GD_CELL_PX - 2;
    for (uint16_t i = gd_level_first_from(lv, first_col); i < lv->obj_count; i++) {
        const gd_obj_t *o = &lv->objs[i];
        if (o->x * GD_CELL_PX - s_v.cam_x > GD_SCREEN_W + GD_CELL_PX) break;
        int x, y, w, h;
        if (obj_rect(o, &x, &y, &w, &h)) fn(o, i, x, y, w, h);
    }
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
static bool orb_used(int idx) {
    return (g_env->game->sim.used[idx >> 5] >> (idx & 31)) & 1u;
}

static void draw_obj(const gd_obj_t *o, int idx, int x, int y, int w, int h) {
    (void)h;
    const gd_palette_t *p = pal();
    switch (o->type) {
    case GD_OBJ_BLOCK:
        gfx_rect(x, y, w, GD_CELL_PX, 2, GD_C_INK, 2, p->edge);
        for (int k = 1; k < o->len; k++) gfx_rect(x + k * GD_CELL_PX, y + 5, 1, 10, 0, p->line, 0, 0);
        break;
    case GD_OBJ_SPIKE_UP:
    case GD_OBJ_SPIKE_DOWN: {
        const bool up = o->type == GD_OBJ_SPIKE_UP;
        const int base = up ? y + GD_CELL_PX : y, tip = up ? y + 2 : y + GD_CELL_PX - 2;
        for (int k = 0; k < o->len; k++) {
            const int sx = x + k * GD_CELL_PX;
            gfx_tri(sx, base, sx + GD_CELL_PX, base, sx + GD_CELL_PX / 2, tip, GD_C_INK);
            gfx_line(sx + 1, base, sx + GD_CELL_PX / 2, tip, 2, p->edge);
            gfx_line(sx + GD_CELL_PX - 1, base, sx + GD_CELL_PX / 2, tip, 2, p->edge);
        }
        break;
    }
    case GD_OBJ_PAD_YELLOW:
    case GD_OBJ_PAD_BLUE: {
        const uint32_t c = o->type == GD_OBJ_PAD_YELLOW ? GD_C_GOLD : GD_C_BLUE;
        const bool ceil = o->flags & GD_OBJF_CEIL;
        gfx_rect(x + 2, ceil ? y : y + GD_CELL_PX - 6, 16, 6, 3, c, 0, 0);
        gfx_opa(110);
        gfx_rect(x + 5, ceil ? y + 6 : y + GD_CELL_PX - 10, 10, 4, 2, c, 0, 0);
        gfx_opa(LV_OPA_COVER);
        break;
    }
    case GD_OBJ_ORB_YELLOW:
    case GD_OBJ_ORB_BLUE: {
        const uint32_t c = o->type == GD_OBJ_ORB_YELLOW ? GD_C_GOLD : GD_C_BLUE;
        const int cx = x + 3 + GD_CELL_PX / 2, cy = y + 3 + GD_CELL_PX / 2;
        if (orb_used(idx)) gfx_opa(90);
        gfx_circle(cx, cy, 11, GD_C_INK, 2, c);
        gfx_circle(cx, cy, 6, c, 0, 0);
        gfx_opa(LV_OPA_COVER);
        break;
    }
    default: {
        static const uint32_t PORTAL_C[] = { GD_C_BLUE, GD_C_GOLD, GD_C_PINK, GD_C_GREEN };
        const uint32_t c = PORTAL_C[o->type - GD_OBJ_PORTAL_FLIP];
        const int cx = x + GD_CELL_PX / 2;
        gfx_frame(cx - 8, y + 2, 16, 60, LV_RADIUS_CIRCLE, 4, c);
        gfx_frame(cx - 3, y + 10, 6, 44, LV_RADIUS_CIRCLE, 2, GD_C_WHITE);
        break;
    }
    }
}

static void rot_pt(int cx, int cy, int x, int y, int deg, bool flip, int16_t *ox, int16_t *oy) {
    if (flip) y = -y;
    const int c = gd_icos(deg), s = gd_isin(deg);
    *ox = (int16_t)(cx + (x * c - y * s) / 1000);
    *oy = (int16_t)(cy + (x * s + y * c) / 1000);
}

static void ship_poly(int cx, int cy, int deg, bool flip, const int8_t pts[8], uint32_t color) {
    int16_t q[8];
    for (int i = 0; i < 4; i++) rot_pt(cx, cy, pts[2 * i], pts[2 * i + 1], deg, flip, &q[2 * i], &q[2 * i + 1]);
    gfx_quad(q, color);
}

static void draw_ship(void) {
    int deg = -s_v.vy_pxs * 35 / 260;
    deg = gd_clamp(deg, -35, 35);
    const bool flip = s_v.grav < 0;
    const int cx = s_v.px, cy = s_v.py;
    if (s_v.held) {
        int16_t a[2], b[2], c[2];
        rot_pt(cx, cy, -15, 0, deg, flip, &a[0], &a[1]);
        rot_pt(cx, cy, -15, 7, deg, flip, &b[0], &b[1]);
        rot_pt(cx, cy, -25, 3, deg, flip, &c[0], &c[1]);
        gfx_tri(a[0], a[1], b[0], b[1], c[0], c[1], GD_C_ORANGE);
    }
    static const int8_t OUTER[8] = { -16, -3, 15, -5, 17, 5, -16, 11 };
    static const int8_t BODY[8] = { -14, -1, 13, -3, 15, 4, -14, 9 };
    static const int8_t STRIPE[8] = { -12, 3, 12, 1, 13, 3, -12, 6 };
    ship_poly(cx, cy, deg, flip, OUTER, GD_C_BLACK);
    ship_poly(cx, cy, deg, flip, BODY, GD_C_PLAYER_1);
    ship_poly(cx, cy, deg, flip, STRIPE, GD_C_PLAYER_2);
    int16_t kx, ky;
    rot_pt(cx, cy, -1, -8, deg, flip, &kx, &ky);
    ui_draw_cube(kx, ky, 11, deg * 16);
}

typedef struct {
    int x, y, size;
    uint32_t color;
} particle_t;

static particle_t particle(int i, int32_t t) {
    const int ang = i * 36 + 12;
    const int spd = 95 + (i * 53) % 75;   // px/s
    particle_t p;
    p.x = s_v.px + gd_icos(ang) * spd / 1000 * t / 1000;
    p.y = s_v.py - gd_isin(ang) * spd / 1000 * t / 1000 + 260 * t / 1000 * t / 2000;
    p.size = 8 - t * 6 / GD_DEATH_MS;
    if (p.size < 2) p.size = 2;
    p.color = (i & 1) ? GD_C_PLAYER_1 : GD_C_PLAYER_2;
    return p;
}

static void draw_death(void) {
    const int32_t t = s_v.death_ms < 0 ? 0 : (s_v.death_ms > GD_DEATH_MS ? GD_DEATH_MS : s_v.death_ms);
    const int r = 12 + t * 60 / GD_DEATH_MS;
    gfx_opa((uint8_t)(255 - t * 230 / GD_DEATH_MS));
    gfx_frame(s_v.px - r, s_v.py - r, 2 * r + 1, 2 * r + 1, LV_RADIUS_CIRCLE, 3, GD_C_WHITE);
    gfx_opa(LV_OPA_COVER);
    for (int i = 0; i < PARTICLES; i++) {
        const particle_t p = particle(i, t);
        gfx_rect(p.x - p.size / 2, p.y - p.size / 2, p.size, p.size, 1, p.color, 1, GD_C_BLACK);
    }
}

static void draw_checkpoints(void) {
    const gd_game_t *g = g_env->game;
    for (int k = 0; k < g->ckpt_n && k < 3; k++) {
        const gd_ckpt_t *c = &g->ckpt[(g->ckpt_head + GD_MAX_CKPT - 1 - k) % GD_MAX_CKPT];
        const int x = (c->x >> GD_FP_SHIFT) - s_v.cam_x, y = GD_GROUND_Y - (c->y >> GD_FP_SHIFT);
        if (x < -10 || x > GD_SCREEN_W + 10) continue;
        const int16_t d[8] = { (int16_t)x, (int16_t)(y - 9), (int16_t)(x + 6), (int16_t)y, (int16_t)x, (int16_t)(y + 9),
                               (int16_t)(x - 6), (int16_t)y };
        // 刚放下的检查点闪一下白色
        const bool fresh = k == 0 && since_ms(s_ckpt_us) >= 0 && since_ms(s_ckpt_us) < 300;
        gfx_quad(d, fresh ? GD_C_WHITE : (k == 0 ? GD_C_GREEN : 0x2E8A4C));
    }
}

static void draw_hud(void) {
    const gd_palette_t *p = pal();
    gfx_rect(0, 0, GD_SCREEN_W, GD_CEIL_BAND_H, 0, p->dark, 0, 0);
    gfx_rect(0, GD_CEIL_BAND_H - 2, GD_SCREEN_W, 2, 0, p->line, 0, 0);
    if (s_v.practice) {
        gfx_rect(36, 2, 40, 15, 7, GD_C_GREEN, 0, 0);
        gfx_text(36, 1, 40, S_PRACTICE_TAG, GD_FONT_SMALL, GD_C_INK, LV_TEXT_ALIGN_CENTER);
    }
    if (g_env->model->save.set.progress_bar) {
        const int bx = 100, bw = 120;
        gfx_rect(bx, 6, bw, 7, 3, GD_C_BLACK, 1, GD_C_WHITE);
        if (s_v.pct > 0) gfx_rect(bx + 1, 7, (bw - 2) * s_v.pct / 100, 5, 2, p->line, 0, 0);
        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", s_v.pct);
        gfx_text(bx + bw + 6, 1, 44, buf, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    }
}

static void draw_ground(void) {
    const gd_palette_t *p = pal();
    gfx_rect(0, GD_GROUND_Y, GD_SCREEN_W, GD_SCREEN_H - GD_GROUND_Y, 0, p->ground, 0, 0);
    gfx_rect(0, GD_GROUND_Y, GD_SCREEN_W, 2, 0, p->line, 0, 0);
    const int off = ((s_v.cam_x % TICK_GAP) + TICK_GAP) % TICK_GAP;
    for (int x = -off; x < GD_SCREEN_W; x += TICK_GAP) gfx_rect(x, GD_GROUND_Y + 12, 3, 18, 1, p->dark, 0, 0);
}

static void banner(const char *text, const lv_font_t *font, int y, uint32_t color, int32_t t_ms) {
    const int e = gd_ease_out(t_ms * 1000 / BANNER_IN_MS);
    const int dy = (1000 - e) * 24 / 1000;
    gfx_text_outline(0, y - dy, GD_SCREEN_W, text, font, color, GD_C_BLACK, LV_TEXT_ALIGN_CENTER);
}

static void draw_pause(void) {
    const gd_model_t *m = g_env->model;
    const gd_palette_t *p = pal();
    gfx_opa(170);
    gfx_rect(0, 0, GD_SCREEN_W, GD_SCREEN_H, 0, GD_C_BLACK, 0, 0);
    gfx_opa(LV_OPA_COVER);
    gfx_rect(56, 44, 208, 182, 14, GD_C_PANEL, 2, p->line);
    gfx_text(56, 46, 208, S_PAUSE_TITLE, GD_FONT_HEAD, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_text(56, 74, 208, ui_level()->name, GD_FONT_SMALL, p->edge, LV_TEXT_ALIGN_CENTER);
    char buf[32];
    const gd_level_rec_t *rec = &m->save.lv[m->level];
    snprintf(buf, sizeof buf, S_PROGRESS_FMT, s_v.pct);
    gfx_text(66, 93, 92, buf, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof buf, S_BEST_FMT, m->practice ? rec->best_practice : rec->best);
    gfx_text(162, 93, 92, buf, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_RIGHT);
    const char *items[GD_PAUSE_COUNT] = { S_PAUSE_RESUME, m->practice ? S_PAUSE_PRACTICE_ON : S_PAUSE_PRACTICE_OFF,
                                          S_PAUSE_RESTART, S_PAUSE_EXIT };
    for (int i = 0; i < GD_PAUSE_COUNT; i++) {
        const int y = 116 + i * 26;
        const bool sel = m->pause_cursor == i;
        if (sel) gfx_rect(70, y - 1, 180, 24, 8, 0x22306E, 2, GD_C_GOLD);
        gfx_text(70, y + 1, 180, items[i], GD_FONT_BODY, sel ? GD_C_GOLD : GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    }
    ui_key_tags(S_KEY_UP, S_KEY_DOWN, S_KEY_OK, GD_C_PANEL, GD_C_WHITE);
    ui_battery();
}

void ui_play_draw(void) {
    const gd_palette_t *p = pal();
    gfx_vgrad(0, PLAY_TOP, GD_SCREEN_W, PLAY_H, p->bg_top, p->bg_bottom);
    // 终点线
    const int fx = g_env->game->lv->finish_col * GD_CELL_PX - s_v.cam_x;
    if (fx > -8 && fx < GD_SCREEN_W + 8) {
        gfx_opa(120);
        gfx_rect(fx - 6, PLAY_TOP, 12, PLAY_H, 0, GD_C_WHITE, 0, 0);
        gfx_opa(LV_OPA_COVER);
        gfx_rect(fx - 1, PLAY_TOP, 3, PLAY_H, 0, GD_C_WHITE, 0, 0);
    }
    for_visible(draw_obj);
    if (s_v.practice) draw_checkpoints();
    if (s_v.attempt_sx > -200 && s_v.attempt_sx < GD_SCREEN_W) {
        char buf[32];
        snprintf(buf, sizeof buf, S_ATTEMPT_FMT, (unsigned)s_v.attempt);
        gfx_text_outline(s_v.attempt_sx, 66, 200, buf, GD_FONT_HEAD, GD_C_WHITE, p->dark, LV_TEXT_ALIGN_LEFT);
    }
    draw_hud();
    draw_ground();
    if (s_v.dead) {
        if (s_v.death_ms < GD_DEATH_MS) draw_death();
        const int nb = g_env->model->new_best;
        if (nb >= 0) {
            char buf[32];
            snprintf(buf, sizeof buf, S_NEW_BEST_FMT, nb);
            banner(buf, GD_FONT_HEAD, 84, GD_C_GOLD, s_v.death_ms);
        }
    } else if (s_v.mode == GD_MODE_SHIP) {
        draw_ship();
    } else {
        ui_draw_cube(s_v.px, s_v.py, GD_CELL_PX, s_v.rot);
    }
    if (s_v.done && s_v.done_ms >= 0) {
        banner(g_env->model->practice ? S_PRACTICE_COMPLETE : S_COMPLETE, GD_FONT_DISPLAY, 70, GD_C_GOLD, s_v.done_ms);
    }
    const int32_t ht = since_ms(s_hint_us);
    if (s_hint && ht >= 0 && ht < s_hint_ms) {
        const int32_t left = s_hint_ms - ht;
        gfx_opa(left < 500 ? (uint8_t)(left * 255 / 500) : LV_OPA_COVER);
        gfx_text_outline(0, 30, GD_SCREEN_W, s_hint, GD_FONT_BODY, GD_C_WHITE, GD_C_BLACK, LV_TEXT_ALIGN_CENTER);
        gfx_opa(LV_OPA_COVER);
    }
    if (g_env->model->paused) draw_pause();
}

// ---------------------------------------------------------------------------
// 变化区域
// ---------------------------------------------------------------------------
static gd_dirty_t *s_dirty;

static void dirty_obj(const gd_obj_t *o, int idx, int x, int y, int w, int h) {
    (void)o;
    (void)idx;
    gd_dirty_add(s_dirty, x, y, w, h);
}

void ui_play_dirty(gd_dirty_t *d) {
    update_view();
    if (g_env->model->paused) return;   // 暂停:画面冻结,菜单变化走整屏刷新
    s_dirty = d;
    for_visible(dirty_obj);
    const int fx = g_env->game->lv->finish_col * GD_CELL_PX - s_v.cam_x;
    if (fx > -8 && fx < GD_SCREEN_W + 8) gd_dirty_add(d, fx - 7, PLAY_TOP, 14, PLAY_H);
    gd_dirty_add(d, 0, GD_GROUND_Y + 10, GD_SCREEN_W, 22);
    if (s_v.practice) {
        const gd_game_t *g = g_env->game;
        for (int k = 0; k < g->ckpt_n && k < 3; k++) {
            const gd_ckpt_t *c = &g->ckpt[(g->ckpt_head + GD_MAX_CKPT - 1 - k) % GD_MAX_CKPT];
            const int x = (c->x >> GD_FP_SHIFT) - s_v.cam_x, y = GD_GROUND_Y - (c->y >> GD_FP_SHIFT);
            if (x > -10 && x < GD_SCREEN_W + 10) gd_dirty_add(d, x - 7, y - 10, 14, 20);
        }
    }
    if (s_v.attempt_sx > -200 && s_v.attempt_sx < GD_SCREEN_W) gd_dirty_add(d, s_v.attempt_sx - 3, 63, 206, 34);
    if (s_v.pct != s_last_pct || s_v.practice != s_last_practice) {
        gd_dirty_add(d, 30, 0, 250, GD_CEIL_BAND_H);
        s_last_pct = s_v.pct;
        s_last_practice = s_v.practice;
    }
    if (s_v.dead) {
        if (s_v.death_ms <= GD_DEATH_MS + 40) {
            int x1 = s_v.px - 76, x2 = s_v.px + 76, y1 = s_v.py - 76, y2 = s_v.py + 76;
            const int32_t t = s_v.death_ms > GD_DEATH_MS ? GD_DEATH_MS : (s_v.death_ms < 0 ? 0 : s_v.death_ms);
            for (int i = 0; i < PARTICLES; i++) {
                const particle_t p = particle(i, t);
                if (p.x - 6 < x1) x1 = p.x - 6;
                if (p.x + 6 > x2) x2 = p.x + 6;
                if (p.y - 6 < y1) y1 = p.y - 6;
                if (p.y + 6 > y2) y2 = p.y + 6;
            }
            gd_dirty_add(d, x1, y1, x2 - x1, y2 - y1);
        }
        if (g_env->model->new_best >= 0 && s_v.death_ms < BANNER_IN_MS + 60) gd_dirty_add(d, 0, 56, GD_SCREEN_W, 64);
    } else if (s_v.mode == GD_MODE_SHIP) {
        gd_dirty_add(d, s_v.px - 28, s_v.py - 26, 50, 50);
    } else {
        gd_dirty_add(d, s_v.px - 16, s_v.py - 16, 32, 32);
    }
    if (s_v.done && s_v.done_ms >= 0 && s_v.done_ms < BANNER_IN_MS + 60) gd_dirty_add(d, 0, 40, GD_SCREEN_W, 90);
    const int32_t ht = since_ms(s_hint_us);
    if (s_hint && ht >= 0 && ht < s_hint_ms + 60) gd_dirty_add(d, 0, 26, GD_SCREEN_W, 30);
}
