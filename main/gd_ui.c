// main/gd_ui.c —— 界面核心:画布、逐帧调度与通用控件。说明见 gd_ui.h。
#include "gd_ui.h"

#include <stdio.h>

#include "gd_fonts.h"
#include "gd_strings.h"
#include "gd_ui_internal.h"

const gd_ui_env_t *g_env;
gd_frame_t g_frame;

static lv_obj_t *s_canvas;
static gd_screen_t s_screen;
static int32_t s_page_start_ms;
static int s_last_level;
static gd_dirty_t s_prev, s_cur;
static bool s_dimmed;
static bool s_force_full;
static int s_battery = -1;
static int64_t s_battery_poll_us = -10000000;   // 首帧立即读一次电量

const gd_level_t *ui_level(void) {
    return &GD_LEVELS[g_env->model->level % GD_LEVEL_COUNT];
}

const gd_palette_t *ui_palette(int level) {
    return &GD_PALETTES[GD_LEVELS[level % GD_LEVEL_COUNT].theme % 6];
}

// ---------------------------------------------------------------------------
// 通用控件
// ---------------------------------------------------------------------------
static int tag_width(const char *text) {
    const int w = gfx_text_width(text, GD_FONT_SMALL) + 18;
    return w < 34 ? 34 : w;
}

static void key_tag(int x, const char *text, uint32_t fill, uint32_t ink) {
    if (!text) return;
    const int w = tag_width(text);
    gfx_rect(x - w / 2, -10, w, 30, 9, fill, 2, ink);
    gfx_text(x - w / 2, 1, w, text, GD_FONT_SMALL, ink, LV_TEXT_ALIGN_CENTER);
}

void ui_key_tags(const char *up, const char *down, const char *ok, uint32_t fill, uint32_t ink) {
    key_tag(GD_KEY_X_UP, up, fill, ink);
    key_tag(GD_KEY_X_DOWN, down, fill, ink);
    key_tag(GD_KEY_X_OK, ok, fill, ink);
}

void ui_battery(void) {
    if (s_battery < 0) return;
    const int x = 270, y = 26;
    const uint32_t col = s_battery < 20 ? GD_C_RED : GD_C_WHITE;
    gfx_frame(x, y + 2, 18, 10, 2, 2, col);
    gfx_rect(x + 18, y + 5, 2, 4, 0, col, 0, 0);
    gfx_rect(x + 3, y + 5, 12 * s_battery / 100 + 1, 4, 1, col, 0, 0);
    char buf[16];
    snprintf(buf, sizeof buf, "%d%%", s_battery);
    gfx_text(x - 46, y - 1, 44, buf, GD_FONT_SMALL, col, LV_TEXT_ALIGN_RIGHT);
}

const char *ui_diff_name(uint8_t diff) {
    return diff == GD_DIFF_EASY ? S_DIFF_EASY : (diff == GD_DIFF_NORMAL ? S_DIFF_NORMAL : S_DIFF_HARD);
}

uint32_t ui_diff_color(uint8_t diff) {
    return diff == GD_DIFF_EASY ? GD_C_GREEN : (diff == GD_DIFF_NORMAL ? GD_C_GOLD : GD_C_RED);
}

void ui_draw_cube(int cx, int cy, int size, int deg16) {
    int16_t q[8];
    gfx_square_pts(cx, cy, size, deg16, q);
    gfx_quad(q, GD_C_BLACK);
    gfx_square_pts(cx, cy, size * 4 / 5, deg16, q);
    gfx_quad(q, GD_C_PLAYER_1);
    gfx_square_pts(cx, cy, size * 2 / 5, deg16, q);
    gfx_quad(q, GD_C_BLACK);
    gfx_square_pts(cx, cy, size / 4, deg16, q);
    gfx_quad(q, GD_C_PLAYER_2);
}

// ---------------------------------------------------------------------------
// 画布
// ---------------------------------------------------------------------------
static void draw_cb(lv_event_t *e) {
    gfx_begin(lv_event_get_layer(e));
    switch (s_screen) {
    case GD_SCR_TITLE: ui_title_draw(); break;
    case GD_SCR_SELECT: ui_select_draw(); break;
    case GD_SCR_PLAY: ui_play_draw(); break;
    case GD_SCR_RESULT: ui_result_draw(); break;
    case GD_SCR_SETTINGS: ui_settings_draw(); break;
    case GD_SCR_STATS: ui_stats_draw(); break;
    }
}

void gd_ui_start(const gd_ui_env_t *env) {
    g_env = env;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_canvas = lv_obj_create(scr);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_size(s_canvas, GD_SCREEN_W, GD_SCREEN_H);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    gd_ui_show();
}

void gd_ui_show(void) {
    s_screen = g_env->model->screen;
    s_page_start_ms = g_frame.ui_ms;
    g_frame.page_ms = 0;
    s_last_level = g_env->model->level;
    if (s_screen == GD_SCR_PLAY) ui_play_enter();
    s_force_full = true;
    if (s_canvas) lv_obj_invalidate(s_canvas);
}

void gd_ui_refresh(void) {
    if (s_screen != g_env->model->screen) {
        gd_ui_show();
        return;
    }
    if (s_screen == GD_SCR_SELECT && g_env->model->level != s_last_level) {
        ui_select_changed(s_last_level, g_env->model->level);
        s_last_level = g_env->model->level;
    }
    s_force_full = true;
    if (s_canvas) lv_obj_invalidate(s_canvas);
}

void gd_ui_set_dimmed(bool dimmed) {
    s_dimmed = dimmed;
    if (!dimmed) s_force_full = true;
}

void gd_ui_frame(int64_t now_us) {
    g_frame.now_us = now_us;
    g_frame.ui_ms = (int32_t)(now_us / 1000);
    g_frame.page_ms = g_frame.ui_ms - s_page_start_ms;
    if (s_dimmed || !s_canvas) return;

    gd_dirty_reset(&s_cur);
    if (g_env->battery && now_us - s_battery_poll_us > 5000000) {
        s_battery_poll_us = now_us;
        const int b = g_env->battery();
        if (b != s_battery) {
            s_battery = b;
            gd_dirty_add(&s_cur, 220, 22, 92, 18);
        }
    }
    if (s_force_full) {
        gd_dirty_full(&s_cur);
        s_force_full = false;
        // 游戏页的快照也要在整屏重画前更新
        if (s_screen == GD_SCR_PLAY) {
            gd_dirty_t scratch;
            gd_dirty_reset(&scratch);
            ui_play_dirty(&scratch);
        }
    } else {
        switch (s_screen) {
        case GD_SCR_TITLE: ui_title_dirty(&s_cur); break;
        case GD_SCR_SELECT: ui_select_dirty(&s_cur); break;
        case GD_SCR_PLAY: ui_play_dirty(&s_cur); break;
        case GD_SCR_SETTINGS: ui_settings_dirty(&s_cur); break;
        default: break;
        }
    }
    gd_dirty_flush(s_canvas, &s_prev, &s_cur);
}
