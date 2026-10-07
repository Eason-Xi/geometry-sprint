// tools/gd_preview/preview_main.c —— 在主机上用真实 LVGL 渲染几何冲刺的各个页面。
// 由 tools/render_gd_preview.py 编译运行:链接固件里的界面、对局控制器、物理、关卡与录像代码,
// 按 20 ms 一帧推进(与固件相同的分块局部刷新,帧缓冲逐块累积,和 LCD 一样),逐页输出 PPM;
// 用通关录像把 6 关各自动玩一遍,检查:能进入结算页、局部刷新与整屏重画逐像素一致(脏矩形没漏标)、
// LVGL 内存池峰值,并统计每帧需要推送到屏幕的像素量(估算 SPI 负载)。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "gd_fonts.h"
#include "gd_gfx.h"
#include "gd_model.h"
#include "gd_play.h"
#include "gd_replay.h"
#include "gd_sfx.h"
#include "gd_ui.h"

#define W GD_SCREEN_W
#define H GD_SCREEN_H
#define DRAW_LINES 30            // 固件:240 × 40 像素的单缓冲 = 横屏 30 行
#define FRAME_MS 20

static uint16_t s_frame[W * H];
static uint16_t s_check[W * H];
static uint8_t s_render_buf[W * DRAW_LINES * 2];
static uint32_t s_tick_ms = 1000;
static const char *s_out_dir = ".";
static size_t s_peak;
static unsigned long s_flush_px;
static bool s_counting = true;

static gd_model_t g_m;
static gd_game_t g_g;
static gd_play_t g_p;
static gd_ui_env_t g_env;
static uint8_t s_missing_track;   // 模拟音乐包里缺这一首(0 = 不缺)

// —— 模拟音频:歌曲时钟随 tick 前进,暂停时冻结 ——
static bool s_music_on, s_music_paused;
static int32_t s_music_base;
static uint32_t s_music_tick;
static int s_sfx_count[GD_SFX_COUNT];
static int s_saves;

static int32_t song_ms(void) {
    if (!s_music_on) return GD_NO_CLOCK;
    return s_music_base + (s_music_paused ? 0 : (int32_t)(s_tick_ms - s_music_tick));
}
static void io_play(uint8_t track, int32_t start_ms) {
    (void)track;
    s_music_on = true;
    s_music_paused = false;
    s_music_base = start_ms;
    s_music_tick = s_tick_ms;
}
static void io_stop(void) {
    s_music_on = false;
}
static void io_pause(bool paused) {
    if (paused && !s_music_paused) s_music_base = song_ms();
    if (!paused && s_music_paused) s_music_tick = s_tick_ms;
    s_music_paused = paused;
}
static void io_sfx(uint8_t id) {
    if (id < GD_SFX_COUNT) s_sfx_count[id]++;
}
static void io_save(void) {
    s_saves++;
}
static const gd_play_io_t IO = { io_play, io_stop, io_pause, io_sfx, io_save };

static bool track_ok(uint8_t track) {
    return track != s_missing_track;
}
static int battery(void) {
    return 76;
}

static uint32_t tick_cb(void) {
    return s_tick_ms;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    if (s_counting) s_flush_px += (unsigned long)(w * lv_area_get_height(area));
    lv_display_flush_ready(display);
}

static void track_mem(void) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    if (mon.max_used > s_peak) s_peak = mon.max_used;
}

// —— 模拟应用:按键经状态机或直接交给对局控制器 ——
static void apply(uint32_t fx) {
    const int64_t now = (int64_t)s_tick_ms * 1000;
    if (fx & GD_FX_STOP) gd_play_stop(&g_p);
    if (fx & GD_FX_START) gd_play_start(&g_p, now);
    if (fx & GD_FX_PAUSE) gd_play_pause(&g_p, true, now);
    if (fx & GD_FX_RESUME) gd_play_pause(&g_p, false, now);
    if (fx & GD_FX_RESTART) gd_play_restart(&g_p, now);
    if (fx & GD_FX_PRACTICE) gd_play_set_practice(&g_p, now);
    if (fx & GD_FX_SCREEN) gd_ui_show();
    else if (fx & GD_FX_REFRESH) gd_ui_refresh();
}

static void key(gd_key_t k, gd_kev_t ev) {
    apply(gd_model_key(&g_m, k, ev, s_tick_ms));
}

// 录像回放:把按键沿按歌曲时间交给对局控制器
static const gd_replay_t *s_replay;
static uint16_t s_replay_next;

static int32_t edge_ms(uint32_t tick) {
    return (int32_t)((tick * 1000 + GD_TICK_HZ - 1) / GD_TICK_HZ) + GD_INPUT_GRACE_MS;
}

static void feed_replay(void) {
    if (!s_replay || !s_music_on || g_m.screen != GD_SCR_PLAY) return;
    const int32_t now = song_ms();
    while (s_replay_next < s_replay->count && edge_ms(s_replay->edges[s_replay_next].tick) <= now) {
        const gd_edge_t *e = &s_replay->edges[s_replay_next++];
        gd_play_key(&g_p, e->down, edge_ms(e->tick));
    }
}

static void frame(void) {
    s_tick_ms += FRAME_MS;
    feed_replay();
    const int64_t now = (int64_t)s_tick_ms * 1000;
    if (g_m.screen == GD_SCR_PLAY) apply(gd_play_frame(&g_p, now, song_ms()));
    apply(gd_model_tick(&g_m, s_tick_ms));
    gd_ui_frame(now);
    lv_timer_handler();
    track_mem();
}

static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += FRAME_MS) frame();
}

// 局部刷新累积出的画面必须与整屏重画完全一致,否则就是漏标了脏矩形(板上会留下残影)。
static long check_partial(const char *where) {
    memcpy(s_check, s_frame, sizeof s_frame);
    s_counting = false;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    s_counting = true;
    long diff = 0;
    for (int i = 0; i < W * H; i++) diff += s_check[i] != s_frame[i];
    if (diff) fprintf(stderr, "PARTIAL MISMATCH %s: %ld pixels\n", where, diff);
    return diff;
}

static void shot(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_out_dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        const uint16_t c = s_frame[i];
        const int r = (c >> 11) & 0x1F, gg = (c >> 5) & 0x3F, b = c & 0x1F;
        fputc((r << 3) | (r >> 2), file);
        fputc((gg << 2) | (gg >> 4), file);
        fputc((b << 3) | (b >> 2), file);
    }
    fclose(file);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("MEM %-28s used=%5zu peak=%5zu free=%5zu\n", name, (size_t)(mon.total_size - mon.free_size),
           (size_t)mon.max_used, (size_t)mon.free_size);
}

static void reset_model(void) {
    gd_model_init(&g_m);
    gd_play_init(&g_p, &g_m, &g_g, &IO);
    s_music_on = false;
    s_replay = NULL;
}

static void goto_screen(gd_screen_t s) {
    g_m.screen = s;
    gd_ui_show();
    run(60);
}

// 进入第 level 关(普通模式),可选用录像自动游玩。
static void play_level(int level, bool autoplay) {
    goto_screen(GD_SCR_SELECT);
    g_m.level = (uint8_t)level;
    gd_ui_refresh();
    run(40);
    s_replay = autoplay ? &GD_REPLAYS[level] : NULL;
    s_replay_next = 0;
    key(GD_KEY_OK, GD_KEV_CLICK);
}

static void run_until_song_ms(int32_t ms) {
    for (int guard = 0; guard < 10000 && (!s_music_on || song_ms() < ms); guard++) frame();
}

static long s_mismatch;

// 自动通关一关:检查进入结算页,统计刷新像素与局部刷新一致性。
static void soak_level(int level) {
    reset_model();
    play_level(level, true);
    const unsigned long px0 = s_flush_px;
    unsigned long max_px = 0, frames = 0, full = 0;
    long mismatch = 0;
    for (int f = 0; f < 6000 && g_m.screen == GD_SCR_PLAY; f++) {
        const unsigned long before = s_flush_px;
        frame();
        const unsigned long px = s_flush_px - before;
        if (px > max_px) max_px = px;
        if (px >= (unsigned long)W * H) full++;
        frames++;
        if (f % 97 == 50 && !g_p.death_us) mismatch += check_partial(GD_LEVELS[level].name);
    }
    const bool cleared = g_m.screen == GD_SCR_RESULT;
    printf("SOAK level %d %-16s %s frames=%lu avg_flush=%lu px (%.0f%% of screen) max=%lu full=%lu attempts=%u "
           "deaths_sfx=%d\n",
           level + 1, GD_LEVELS[level].name, cleared ? "CLEARED" : "NOT CLEARED", frames,
           frames ? (s_flush_px - px0) / frames : 0,
           frames ? 100.0 * (double)(s_flush_px - px0) / (double)frames / (W * H) : 0.0, max_px, full,
           (unsigned)g_m.result.attempts, s_sfx_count[GD_SFX_DEATH]);
    if (!cleared || g_m.result.attempts != 1) {
        fprintf(stderr, "level %d autoplay did not clear on the first attempt\n", level + 1);
        exit(1);
    }
    s_mismatch += mismatch;
}

int main(int argc, char **argv) {
    if (argc > 1) s_out_dir = argv[1];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_render_buf, NULL, sizeof s_render_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    const int missing = gd_fonts_selfcheck();
    printf("FONTS missing=%d\n", missing);
    if (missing) return 1;

    reset_model();
    g_env = (gd_ui_env_t){ .model = &g_m, .game = &g_g, .track_ok = track_ok, .battery = battery };
    gd_ui_start(&g_env);

    // —— 标题 ——
    run(800);
    shot("01_title");
    run(1900);
    shot("02_title_jump");
    s_mismatch += check_partial("title");

    // —— 选关 ——
    key(GD_KEY_OK, GD_KEV_CLICK);
    run(300);
    shot("03_select_level1");
    g_m.save.lv[2].best = 63;
    g_m.save.lv[2].best_practice = 88;
    s_missing_track = 3;
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    run(80);
    shot("04_select_sliding");
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    run(300);
    shot("05_select_level3_nomusic");
    s_mismatch += check_partial("select");
    s_missing_track = 0;
    g_m.save.lv[5].completed = true;
    g_m.save.lv[5].best = 100;
    g_m.level = 5;
    gd_ui_refresh();
    run(300);
    shot("06_select_level6_cleared");

    // —— 设置 ——
    reset_model();
    goto_screen(GD_SCR_TITLE);
    key(GD_KEY_UP, GD_KEV_PRESS);
    run(200);
    shot("07_settings");
    key(GD_KEY_OK, GD_KEV_CLICK);
    key(GD_KEY_UP, GD_KEV_PRESS);
    run(200);
    shot("08_settings_edit_volume");
    key(GD_KEY_OK, GD_KEV_CLICK);
    for (int i = 0; i < 4; i++) key(GD_KEY_DOWN, GD_KEV_PRESS);
    key(GD_KEY_OK, GD_KEV_CLICK);
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    run(200);
    shot("09_settings_offset");
    key(GD_KEY_OK, GD_KEV_CLICK);
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    key(GD_KEY_OK, GD_KEV_PRESS);
    run(800);
    shot("10_settings_clear_holding");
    s_mismatch += check_partial("settings clear");
    run(900);
    key(GD_KEY_OK, GD_KEV_RELEASE);
    run(200);
    shot("11_settings_cleared");
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    key(GD_KEY_OK, GD_KEV_CLICK);
    run(200);
    shot("12_settings_about");

    // —— 统计 ——
    reset_model();
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        g_m.save.lv[i].attempts = (uint32_t)(i * 17 + 3);
        g_m.save.lv[i].jumps = (uint32_t)(i * 230 + 40);
        g_m.save.lv[i].best = (uint8_t)(100 - i * 13);
    }
    g_m.save.lv[0].completed = true;
    g_m.save.lv[0].best = 100;
    g_m.save.play_seconds = 4321;
    goto_screen(GD_SCR_STATS);
    run(100);
    shot("13_stats");

    // —— 游戏:第 1 关自动游玩 ——
    reset_model();
    play_level(0, true);
    run(450);
    shot("14_play_l1_start_hint");
    run_until_song_ms(14000);
    shot("15_play_l1_14s");
    s_mismatch += check_partial("play l1");
    for (int guard = 0; guard < 4000 && g_g.sim.mode != GD_MODE_SHIP; guard++) frame();
    run(1500);
    shot("16_play_l1_ship");
    s_mismatch += check_partial("ship");
    // 暂停菜单
    apply(gd_model_pause(&g_m));
    run(100);
    shot("17_pause");
    key(GD_KEY_DOWN, GD_KEV_PRESS);
    run(60);
    shot("18_pause_practice_item");
    // 切到练习模式:从头开始,自动检查点
    key(GD_KEY_OK, GD_KEV_CLICK);
    s_replay_next = 0;
    run(400);
    shot("19_practice_hint");
    run_until_song_ms(11000);
    shot("20_practice_checkpoints");
    s_mismatch += check_partial("practice");

    // —— 死亡:第 2 关不按键,撞上第一个尖刺 ——
    reset_model();
    play_level(1, false);
    for (int guard = 0; guard < 3000 && !g_g.sim.dead; guard++) frame();
    run(260);
    shot("21_death_newbest");
    run(800);
    shot("22_retry_attempt2");

    // —— 其余关卡的中段画面 ——
    static const struct {
        int level;
        int32_t ms;
        const char *name;
    } MID[] = {
        { 1, 30000, "23_play_l2_pads" }, { 2, 21000, "24_play_l3_orbs" }, { 3, 15500, "25_play_l4_gravity" },
        { 4, 40000, "26_play_l5" }, { 5, 50000, "27_play_l6" },
    };
    for (unsigned i = 0; i < sizeof MID / sizeof MID[0]; i++) {
        reset_model();
        play_level(MID[i].level, true);
        run_until_song_ms(MID[i].ms);
        shot(MID[i].name);
    }

    // —— 通关与结算 ——
    reset_model();
    play_level(0, true);
    for (int guard = 0; guard < 8000 && !g_g.sim.finished; guard++) frame();
    run(500);
    shot("28_complete_banner");
    run(1200);
    shot("29_result");

    // —— 全部关卡自动通关 ——
    for (int l = 0; l < GD_LEVEL_COUNT; l++) soak_level(l);

    printf("PARTIAL total mismatched pixels: %ld\n", s_mismatch);
    printf("SFX death=%d complete=%d checkpoint=%d saves=%d\n", s_sfx_count[GD_SFX_DEATH],
           s_sfx_count[GD_SFX_COMPLETE], s_sfx_count[GD_SFX_CHECKPOINT], s_saves);
    printf("PEAK used=%zu of %u\n", s_peak, (unsigned)LV_MEM_SIZE);
    return s_mismatch ? 2 : 0;
}
