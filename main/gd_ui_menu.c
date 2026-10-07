// main/gd_ui_menu.c —— 标题、选关、结算、设置与统计页面。
#include <stdio.h>

#include "gd_fonts.h"
#include "gd_strings.h"
#include "gd_ui_internal.h"

#define TAG_FILL  0x1B2A6B
#define TAG_INK   0xFFFFFF

static const gd_model_t *model(void) {
    return g_env->model;
}

static void night_bg(void) {
    gfx_vgrad(0, 0, GD_SCREEN_W, GD_SCREEN_H, GD_C_NIGHT_2, GD_C_NIGHT);
}

// 背景里静止的大方块装饰(只描边)。
static void deco_squares(uint32_t color) {
    static const int16_t SQ[][3] = { { 18, 70, 46 }, { 250, 88, 58 }, { 120, 40, 30 }, { 210, 150, 36 },
                                     { 40, 150, 28 } };
    gfx_opa(70);
    for (unsigned i = 0; i < sizeof SQ / sizeof SQ[0]; i++) gfx_frame(SQ[i][0], SQ[i][1], SQ[i][2], SQ[i][2], 4, 2, color);
    gfx_opa(LV_OPA_COVER);
}

// ---------------------------------------------------------------------------
// 标题:方块角色沿地面跑动,在两个尖刺前起跳
// ---------------------------------------------------------------------------
#define TITLE_GROUND 196
static const int TITLE_SPIKES[] = { 112, 236 };

typedef struct {
    int x, y, rot;
} runner_t;

static runner_t title_runner(int32_t t_ms) {
    const int period = 4200;   // 跑过整屏 + 回绕
    const int x = (int)((t_ms % period) * (GD_SCREEN_W + 60) / period) - 30;
    runner_t r = { x, TITLE_GROUND - 11, 0 };
    for (unsigned i = 0; i < sizeof TITLE_SPIKES / sizeof TITLE_SPIKES[0]; i++) {
        const int d = x - (TITLE_SPIKES[i] - 44);   // 起跳点在尖刺前 44 px
        if (d >= 0 && d < 88) {
            // 抛物线:高 32 px,跨 88 px(不遮挡上方提示文字)
            const int h = 32 - (d - 44) * (d - 44) * 32 / (44 * 44);
            r.y -= h;
            r.rot = d * 90 * 16 / 88;
        }
    }
    return r;
}

void ui_title_draw(void) {
    night_bg();
    deco_squares(0x5B7BFF);
    gfx_text_outline(0, 40, GD_SCREEN_W, S_APP_NAME, GD_FONT_DISPLAY, GD_C_GOLD, 0x2A1600, LV_TEXT_ALIGN_CENTER);
    gfx_text(0, 92, GD_SCREEN_W, S_APP_SUB, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_CENTER);
    // 提示文字缓慢呼吸
    const int phase = (int)(g_frame.ui_ms % 1600);
    const int opa = 120 + (phase < 800 ? phase : 1600 - phase) * 135 / 800;
    gfx_opa((uint8_t)opa);
    gfx_text(0, 118, GD_SCREEN_W, S_TITLE_HINT, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_opa(LV_OPA_COVER);
    // 地面与尖刺
    gfx_rect(0, TITLE_GROUND, GD_SCREEN_W, GD_SCREEN_H - TITLE_GROUND, 0, 0x14215A, 0, 0);
    gfx_rect(0, TITLE_GROUND, GD_SCREEN_W, 2, 0, 0x8FD3FF, 0, 0);
    for (unsigned i = 0; i < sizeof TITLE_SPIKES / sizeof TITLE_SPIKES[0]; i++) {
        const int x = TITLE_SPIKES[i] - 10;
        gfx_tri(x, TITLE_GROUND, x + 20, TITLE_GROUND, x + 10, TITLE_GROUND - 18, GD_C_INK);
        gfx_line(x + 1, TITLE_GROUND - 1, x + 10, TITLE_GROUND - 18, 2, 0xC9EBFF);
        gfx_line(x + 19, TITLE_GROUND - 1, x + 10, TITLE_GROUND - 18, 2, 0xC9EBFF);
    }
    const runner_t r = title_runner(g_frame.ui_ms);
    ui_draw_cube(r.x, r.y, 22, r.rot);
    ui_key_tags(S_KEY_SETTINGS, S_KEY_STATS, S_KEY_START, TAG_FILL, TAG_INK);
    ui_battery();
}

void ui_title_dirty(gd_dirty_t *d) {
    const runner_t r = title_runner(g_frame.ui_ms);
    gd_dirty_add(d, r.x - 17, r.y - 17, 34, 34);
    gd_dirty_add(d, 30, 116, GD_SCREEN_W - 60, 26);   // 呼吸提示
}

// ---------------------------------------------------------------------------
// 选关:横向卡片轮播
// ---------------------------------------------------------------------------
#define CARD_X 44
#define CARD_Y 44
#define CARD_W 232
#define CARD_H 160
#define SLIDE_MS 180

static int s_slide_from = -1;
static int s_slide_dir;
static int32_t s_slide_start;

void ui_select_changed(int from, int to) {
    s_slide_from = from;
    s_slide_dir = to > from ? 1 : -1;
    s_slide_start = g_frame.ui_ms;
}

static void progress_row(int x, int y, const char *label, int pct, uint32_t color) {
    char buf[16];
    gfx_text(x, y - 2, 40, label, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    const int bx = x + 40, bw = 120;
    gfx_rect(bx, y + 3, bw, 10, 5, 0x000000, 0, 0);
    if (pct > 0) gfx_rect(bx, y + 3, bw * pct / 100 < 10 ? 10 : bw * pct / 100, 10, 5, color, 0, 0);
    gfx_frame(bx, y + 3, bw, 10, 5, 1, 0xFFFFFF);
    snprintf(buf, sizeof buf, "%d%%", pct);
    gfx_text(bx + bw + 6, y - 2, 46, buf, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
}

static void draw_card(int level, int dx) {
    const gd_level_t *lv = &GD_LEVELS[level];
    const gd_palette_t *pal = ui_palette(level);
    const gd_level_rec_t *rec = &model()->save.lv[level];
    const int x = CARD_X + dx, y = CARD_Y;
    char buf[32];
    gfx_vgrad(x, y, CARD_W, CARD_H, pal->bg_top, pal->bg_bottom);
    gfx_frame(x, y, CARD_W, CARD_H, 14, 3, pal->line);
    snprintf(buf, sizeof buf, S_LEVEL_FMT, level + 1);
    gfx_text(x + 14, y + 10, 80, buf, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    // 难度标签
    const char *dn = ui_diff_name(lv->difficulty);
    const int dw = gfx_text_width(dn, GD_FONT_SMALL) + 16;
    gfx_rect(x + CARD_W - 14 - dw, y + 9, dw, 20, 10, ui_diff_color(lv->difficulty), 0, 0);
    gfx_text(x + CARD_W - 14 - dw, y + 10, dw, dn, GD_FONT_SMALL, GD_C_INK, LV_TEXT_ALIGN_CENTER);
    gfx_text_outline(x, y + 36, CARD_W, lv->name, GD_FONT_HEAD, GD_C_WHITE, pal->dark, LV_TEXT_ALIGN_CENTER);
    gfx_text(x, y + 66, CARD_W, lv->artist, GD_FONT_SMALL, pal->edge, LV_TEXT_ALIGN_CENTER);
    progress_row(x + 16, y + 94, S_MODE_NORMAL, rec->best, GD_C_GREEN);
    progress_row(x + 16, y + 116, S_MODE_PRACTICE, rec->best_practice, GD_C_CYAN);
    if (rec->completed) {
        gfx_text(x + 16, y + 136, 120, S_SYM_STAR " " S_CLEARED, GD_FONT_SMALL, GD_C_GOLD, LV_TEXT_ALIGN_LEFT);
    }
    const bool music = g_env->track_ok && g_env->track_ok(lv->track);
    if (!music) {
        gfx_text(x + 16, y + 136, CARD_W - 32, S_NO_MUSIC, GD_FONT_SMALL, GD_C_ORANGE, LV_TEXT_ALIGN_RIGHT);
    }
}

void ui_select_draw(void) {
    night_bg();
    gfx_text(16, 24, 140, S_SELECT_TITLE, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_LEFT);
    const int cur = model()->level;
    int dx = 0;
    const int32_t t = g_frame.ui_ms - s_slide_start;
    if (s_slide_from >= 0 && t < SLIDE_MS) {
        const int e = gd_ease_out((int)(t * 1000 / SLIDE_MS));
        dx = s_slide_dir * (1000 - e) * (CARD_W + 30) / 1000;
        draw_card(s_slide_from, dx - s_slide_dir * (CARD_W + 30));
    }
    draw_card(cur, dx);
    // 两侧箭头
    if (cur > 0) gfx_text(8, CARD_Y + 66, 30, S_SYM_LEFT, GD_FONT_BODY, GD_C_MUTED, LV_TEXT_ALIGN_CENTER);
    if (cur + 1 < GD_LEVEL_COUNT) {
        gfx_text(GD_SCREEN_W - 38, CARD_Y + 66, 30, S_SYM_RIGHT, GD_FONT_BODY, GD_C_MUTED, LV_TEXT_ALIGN_CENTER);
    }
    // 页码圆点
    const int x0 = GD_SCREEN_W / 2 - (GD_LEVEL_COUNT * 14) / 2;
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const bool on = i == cur;
        gfx_circle(x0 + i * 14 + 7, 220, on ? 4 : 3, on ? GD_C_GOLD : 0x4A5A9A, 0, 0);
    }
    ui_key_tags(cur > 0 ? S_KEY_PREV : NULL, cur + 1 < GD_LEVEL_COUNT ? S_KEY_NEXT : NULL, S_KEY_START, TAG_FILL,
                TAG_INK);
    ui_battery();
}

void ui_select_dirty(gd_dirty_t *d) {
    if (s_slide_from >= 0) {
        if (g_frame.ui_ms - s_slide_start < SLIDE_MS + 40) {
            gd_dirty_add(d, 0, CARD_Y - 2, GD_SCREEN_W, CARD_H + 4);
        } else {
            s_slide_from = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// 结算
// ---------------------------------------------------------------------------
static void stat_row(int y, const char *label, const char *value) {
    gfx_text(70, y, 80, label, GD_FONT_BODY, GD_C_MUTED, LV_TEXT_ALIGN_LEFT);
    gfx_text(150, y, 110, value, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_RIGHT);
}

void ui_result_draw(void) {
    const gd_palette_t *pal = ui_palette(model()->level);
    const gd_result_t *r = &model()->result;
    char buf[32];
    gfx_vgrad(0, 0, GD_SCREEN_W, GD_SCREEN_H, pal->bg_top, pal->dark);
    deco_squares(pal->edge);
    gfx_text_outline(0, 30, GD_SCREEN_W, r->practice ? S_PRACTICE_COMPLETE : S_COMPLETE, GD_FONT_DISPLAY, GD_C_GOLD,
                     0x2A1600, LV_TEXT_ALIGN_CENTER);
    gfx_text(0, 82, GD_SCREEN_W, ui_level()->name, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_rect(56, 110, 208, 92, 12, pal->dark, 2, pal->line);
    snprintf(buf, sizeof buf, S_COUNT_FMT, (unsigned)r->attempts);
    stat_row(118, S_RESULT_ATTEMPTS, buf);
    snprintf(buf, sizeof buf, S_COUNT_FMT, (unsigned)r->jumps);
    stat_row(144, S_RESULT_JUMPS, buf);
    const unsigned s = r->time_ms / 1000;
    snprintf(buf, sizeof buf, S_MIN_SEC_FMT, s / 60, s % 60);
    stat_row(170, S_RESULT_TIME, buf);
    if (r->first_clear) {
        gfx_rect(110, 210, 100, 22, 11, GD_C_GOLD, 0, 0);
        gfx_text(110, 211, 100, S_SYM_STAR " " S_FIRST_CLEAR, GD_FONT_SMALL, GD_C_INK, LV_TEXT_ALIGN_CENTER);
    }
    ui_key_tags(S_KEY_AGAIN, NULL, S_KEY_BACK, pal->dark, GD_C_WHITE);
}

// ---------------------------------------------------------------------------
// 设置
// ---------------------------------------------------------------------------
static const char *const SET_LABELS[GD_SET_COUNT] = {
    S_SET_VOLUME, S_SET_SFX, S_SET_BAR, S_SET_BRIGHT, S_SET_OFFSET, S_SET_CLEAR, S_SET_ABOUT,
};

#define SET_Y0   54
#define SET_ROW  25

static void volume_bar(int x, int y, int v) {
    for (int i = 0; i < GD_VOLUME_MAX; i++) {
        gfx_rect(x + i * 7, y + 4, 5, 12, 1, i < v ? GD_C_GOLD : 0x2B3A7A, 0, 0);
    }
}

static void about_panel(void) {
    gfx_opa(200);
    gfx_rect(0, 0, GD_SCREEN_W, GD_SCREEN_H, 0, GD_C_BLACK, 0, 0);
    gfx_opa(LV_OPA_COVER);
    gfx_rect(20, 44, 280, 160, 14, GD_C_PANEL, 2, 0x5B7BFF);
    gfx_text(20, 54, 280, S_ABOUT_TITLE, GD_FONT_HEAD, GD_C_GOLD, LV_TEXT_ALIGN_CENTER);
    gfx_text(28, 92, 264, S_ABOUT_1, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_text(28, 118, 264, S_ABOUT_2, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_text(28, 144, 264, S_ABOUT_3, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
    gfx_text(28, 170, 264, S_ABOUT_4, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_CENTER);
}

static int32_t clear_progress_ms(void) {
    const gd_model_t *m = model();
    if (!m->clear_holding) return -1;
    const int32_t held = (int32_t)((uint32_t)g_frame.ui_ms - m->clear_hold_start);
    return held < 0 ? 0 : (held > GD_CLEAR_HOLD_MS ? GD_CLEAR_HOLD_MS : held);
}

void ui_settings_draw(void) {
    const gd_model_t *m = model();
    const gd_settings_t *s = &m->save.set;
    char buf[32];
    night_bg();
    gfx_text(16, 24, 120, S_SET_TITLE, GD_FONT_HEAD, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    if (m->set_cursor == GD_SET_OFFSET) {
        gfx_text(110, 30, 150, S_OFFSET_HELP, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_RIGHT);
    }
    for (int i = 0; i < GD_SET_COUNT; i++) {
        const int y = SET_Y0 + i * SET_ROW;
        const bool sel = m->set_cursor == i;
        if (sel) {
            gfx_rect(18, y - 1, 284, SET_ROW - 2, 8, m->set_editing ? 0x3A2F10 : 0x22306E, 2,
                     m->set_editing ? GD_C_GOLD : 0x5B7BFF);
        }
        gfx_text(30, y + 1, 120, SET_LABELS[i], GD_FONT_BODY, sel ? GD_C_WHITE : 0xC8D0F0, LV_TEXT_ALIGN_LEFT);
        const int vx = 150, vw = 140;
        const char *val = NULL;
        switch (i) {
        case GD_SET_VOLUME:
            volume_bar(vx + 54, y, s->volume);
            snprintf(buf, sizeof buf, "%u", (unsigned)s->volume);
            gfx_text(vx, y + 1, 46, buf, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_RIGHT);
            break;
        case GD_SET_SFX: val = s->sfx ? S_ON : S_OFF; break;
        case GD_SET_BAR: val = s->progress_bar ? S_SHOW : S_HIDE; break;
        case GD_SET_BRIGHT:
            val = s->brightness == 0 ? S_BRIGHT_LOW : (s->brightness == 1 ? S_BRIGHT_MID : S_BRIGHT_HIGH);
            break;
        case GD_SET_OFFSET:
            snprintf(buf, sizeof buf, S_OFFSET_FMT, s->offset_ms);
            val = buf;
            break;
        case GD_SET_CLEAR: {
            const int32_t p = clear_progress_ms();
            if (p >= 0) {
                gfx_rect(vx, y + 3, vw, 16, 8, 0x3A1010, 0, 0);
                gfx_rect(vx, y + 3, vw * p / GD_CLEAR_HOLD_MS + 1, 16, 8, GD_C_RED, 0, 0);
                gfx_text(vx, y + 3, vw, S_CLEAR_HOLDING, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_CENTER);
            } else if (m->cleared_at && (uint32_t)g_frame.ui_ms - m->cleared_at < 2500) {
                gfx_text(vx, y + 3, vw, S_CLEAR_DONE, GD_FONT_SMALL, GD_C_GREEN, LV_TEXT_ALIGN_RIGHT);
            } else {
                gfx_text(vx, y + 3, vw, S_CLEAR_HINT, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_RIGHT);
            }
            break;
        }
        default: val = S_SYM_RIGHT; break;
        }
        if (val) {
            if (sel && m->set_editing) {
                char edit[40];
                snprintf(edit, sizeof edit, S_SYM_LEFT " %s " S_SYM_RIGHT, val);
                gfx_text(vx, y + 1, vw, edit, GD_FONT_BODY, GD_C_GOLD, LV_TEXT_ALIGN_RIGHT);
            } else {
                gfx_text(vx, y + 1, vw, val, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_RIGHT);
            }
        }
    }
    if (m->about_open) {
        about_panel();
        ui_key_tags(S_KEY_CLOSE, S_KEY_CLOSE, S_KEY_CLOSE, TAG_FILL, TAG_INK);
    } else if (m->set_editing) {
        ui_key_tags(S_KEY_MINUS, S_KEY_PLUS, S_KEY_DONE, 0x3A2F10, GD_C_GOLD);
    } else {
        ui_key_tags(S_KEY_UP, S_KEY_DOWN, S_KEY_OK, TAG_FILL, TAG_INK);
    }
    if (!m->about_open) ui_battery();
}

void ui_settings_dirty(gd_dirty_t *d) {
    const gd_model_t *m = model();
    const bool flash = m->cleared_at && (uint32_t)g_frame.ui_ms - m->cleared_at < 2600;
    if (m->clear_holding || flash) gd_dirty_add(d, 140, SET_Y0 + GD_SET_CLEAR * SET_ROW, 160, SET_ROW);
}

// ---------------------------------------------------------------------------
// 统计
// ---------------------------------------------------------------------------
void ui_stats_draw(void) {
    const gd_save_t *s = &model()->save;
    char buf[40];
    uint32_t attempts = 0, jumps = 0;
    int cleared = 0;
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        attempts += s->lv[i].attempts;
        jumps += s->lv[i].jumps;
        cleared += s->lv[i].completed;
    }
    night_bg();
    gfx_text(16, 24, 120, S_STATS_TITLE, GD_FONT_HEAD, GD_C_WHITE, LV_TEXT_ALIGN_LEFT);
    struct {
        const char *label;
        char value[24];
    } cells[4];
    cells[0].label = S_STATS_ATTEMPTS;
    snprintf(cells[0].value, sizeof cells[0].value, "%u", (unsigned)attempts);
    cells[1].label = S_STATS_JUMPS;
    snprintf(cells[1].value, sizeof cells[1].value, "%u", (unsigned)jumps);
    cells[2].label = S_STATS_CLEARED;
    snprintf(cells[2].value, sizeof cells[2].value, S_CLEARED_FMT, cleared, GD_LEVEL_COUNT);
    cells[3].label = S_STATS_TIME;
    const unsigned mins = s->play_seconds / 60;
    if (mins >= 60) snprintf(cells[3].value, sizeof cells[3].value, S_HOUR_MIN_FMT, mins / 60, mins % 60);
    else snprintf(cells[3].value, sizeof cells[3].value, S_MIN_FMT, mins);
    for (int i = 0; i < 4; i++) {
        const int x = 20 + (i % 2) * 144, y = 56 + (i / 2) * 40;
        gfx_rect(x, y, 136, 36, 8, 0x16224F, 0, 0);
        gfx_text(x + 8, y + 2, 120, cells[i].label, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_LEFT);
        gfx_text(x + 8, y + 16, 120, cells[i].value, GD_FONT_BODY, GD_C_WHITE, LV_TEXT_ALIGN_RIGHT);
    }
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const int y = 140 + i * 15;
        const gd_level_rec_t *r = &s->lv[i];
        gfx_text(34, y, 150, GD_LEVELS[i].name, GD_FONT_SMALL, r->completed ? GD_C_GOLD : 0xC8D0F0,
                 LV_TEXT_ALIGN_LEFT);
        snprintf(buf, sizeof buf, "%d%%", r->best);
        gfx_text(180, y, 44, buf, GD_FONT_SMALL, GD_C_WHITE, LV_TEXT_ALIGN_RIGHT);
        snprintf(buf, sizeof buf, S_COUNT_FMT, (unsigned)r->attempts);
        gfx_text(226, y, 64, buf, GD_FONT_SMALL, GD_C_MUTED, LV_TEXT_ALIGN_RIGHT);
    }
    ui_key_tags(NULL, NULL, S_KEY_BACK, TAG_FILL, TAG_INK);
    ui_battery();
}
