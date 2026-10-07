// main/gd_app.c —— 应用任务:按键 → 对局 / 状态机 → 音频、界面与存档。
//
// 线程模型:
//   * 按键回调(esp_timer 任务):只记下时间戳并入队,立即返回。
//   * 应用任务(本文件,优先级 5):游戏中的跳跃键与检查点键放进无锁环形缓冲(带时间戳),
//     由下一帧换算成物理步;其余按键在 LVGL 锁内交给状态机,锁外处理音量、背光与存档。
//   * LVGL 任务(优先级 4)里的 20 ms 帧定时器:消化环形缓冲、按歌曲时钟推进对局、标记重绘区域。
//   * 音频任务(gd_audio.c,优先级 6)提供歌曲时钟。
// 判定用的是按键回调里记下的时间,与任务何时处理无关(PRD §4)。
#include "gd_app.h"

#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "gd_audio.h"
#include "gd_model.h"
#include "gd_music.h"
#include "gd_play.h"
#include "gd_replay.h"
#include "gd_sfx.h"
#include "gd_store.h"
#include "gd_ui.h"

#ifndef GD_AUTOPLAY
#define GD_AUTOPLAY 0   // 调试:1 = 用通关录像自动游玩(固定场景下测量性能,PRD §12)
#endif

static const char *TAG = "gd_app";

#define APP_STACK        4096
#define APP_PRIORITY     5
#define QUEUE_DEPTH      24
#define FRAME_MS         20
#define RING             32
#define IDLE_DIM_US      (120LL * 1000000)
#define BACKLIGHT_DIM    20
#define BATTERY_POLL_US  (10LL * 1000000)
#define PERF_LOG_US      1000000

typedef struct {
    uint8_t btn;
    uint8_t ev;
    int64_t t_us;
} input_t;

enum { IN_RELEASE = 0, IN_PRESS, IN_CHECKPOINT };

typedef struct {
    uint8_t kind;
    int64_t t_us;
} play_in_t;

static QueueHandle_t s_queue;
static gd_model_t s_model;
static gd_game_t s_game;
static gd_play_t s_play;
static gd_ui_env_t s_env;

static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;
static play_in_t s_ring[RING];
static uint8_t s_ring_head, s_ring_count;

static volatile uint32_t s_expected_song;
static volatile bool s_song_clock;
static volatile bool s_save_pending;
static volatile int s_battery = -1;
static bool s_battery_ok;
static bool s_dimmed;
static int64_t s_last_input_us;
static int64_t s_battery_poll_us;

// —— 性能统计(LVGL 任务写,应用任务读) ——
static uint32_t s_frames;
static int64_t s_last_frame_us;
static uint32_t s_gap_max_us;
static int64_t s_refr_start_us;
static uint64_t s_refr_sum_us;
static uint32_t s_refr_max_us;
static uint32_t s_refr_count;
static int64_t s_perf_log_us;

// ---------------------------------------------------------------------------
// 时钟与音频回调
// ---------------------------------------------------------------------------
static int32_t clock_ms(int64_t t_us) {
    if (!s_song_clock || gd_audio_song_id() != s_expected_song) return GD_NO_CLOCK;   // 新的一次播放尚未开始
    const int32_t ms = gd_audio_song_ms(t_us);
    return ms == INT32_MIN ? GD_NO_CLOCK : ms;
}

static void io_play(uint8_t track, int32_t start_ms) {
    s_expected_song = gd_audio_song_id() + 1;   // 只有本应用调用 gd_audio_play,下一次的编号可预知
    s_song_clock = true;
    gd_audio_play(track, start_ms);
}

static void io_stop(void) {
    s_song_clock = false;
    gd_audio_stop();
}

static void io_pause(bool paused) {
    gd_audio_pause(paused);
}

static void io_sfx(uint8_t id) {
    if (s_model.save.set.sfx) gd_audio_sfx(id);
}

static void io_save(void) {
    s_save_pending = true;
}

static const gd_play_io_t IO = { io_play, io_stop, io_pause, io_sfx, io_save };

static bool track_ok(uint8_t track) {
    return gd_music_track_ok(track);
}

static int battery(void) {
    return s_battery;
}

// ---------------------------------------------------------------------------
// 状态机副作用
// ---------------------------------------------------------------------------
// LVGL 锁内(或 LVGL 任务里):对局操作与界面刷新。
static void apply_ui_fx(uint32_t fx, int64_t now) {
    if (fx & GD_FX_STOP) gd_play_stop(&s_play);
    if (fx & GD_FX_START) gd_play_start(&s_play, now);
    if (fx & GD_FX_PAUSE) gd_play_pause(&s_play, true, now);
    if (fx & GD_FX_RESUME) gd_play_pause(&s_play, false, now);
    if (fx & GD_FX_RESTART) gd_play_restart(&s_play, now);
    if (fx & GD_FX_PRACTICE) gd_play_set_practice(&s_play, now);
    if (fx & GD_FX_SCREEN) gd_ui_show();
    else if (fx & GD_FX_REFRESH) gd_ui_refresh();
}

// 不需要 LVGL 锁、也不阻塞的副作用:音量、背光、提示音、存档请求。
static void apply_side(uint32_t fx) {
    if (fx & GD_FX_VOLUME) {
        gd_audio_set_volume(s_model.save.set.volume);
        gd_audio_sfx(GD_SFX_MOVE);   // 用新音量响一下(不受音效开关影响)
    }
    if (fx & GD_FX_BRIGHT) bsp_display_backlight(gd_save_backlight(s_model.save.set.brightness));
    if (fx & GD_FX_SAVE) s_save_pending = true;
    if (fx & GD_FX_SND_MOVE) io_sfx(GD_SFX_MOVE);
    if (fx & GD_FX_SND_OK) io_sfx(GD_SFX_OK);
    if (fx & GD_FX_SND_BACK) io_sfx(GD_SFX_BACK);
}

typedef uint32_t (*model_op_t)(void *arg);

static void run_model(model_op_t op, void *arg) {
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGW(TAG, "取 LVGL 锁超时,丢弃一次操作");
        return;
    }
    const gd_screen_t before = s_model.screen;
    const uint32_t fx = op(arg);
    apply_ui_fx(fx, esp_timer_get_time());
    bsp_lvgl_unlock();
    if (before != s_model.screen) ESP_LOGI(TAG, "页面 %d → %d", before, s_model.screen);
    apply_side(fx);
}

typedef struct {
    gd_key_t key;
    gd_kev_t ev;
    uint32_t now_ms;
} key_arg_t;

static uint32_t op_key(void *arg) {
    const key_arg_t *a = arg;
    return gd_model_key(&s_model, a->key, a->ev, a->now_ms);
}

static uint32_t op_pause(void *arg) {
    (void)arg;
    return gd_model_pause(&s_model);
}

static uint32_t op_tick(void *arg) {
    return gd_model_tick(&s_model, *(uint32_t *)arg);
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
static void ring_push(uint8_t kind, int64_t t_us) {
    taskENTER_CRITICAL(&s_ring_mux);
    if (s_ring_count == RING) {   // 满了丢弃最旧的(正常游玩不可能积压这么多)
        s_ring_head = (uint8_t)((s_ring_head + 1) % RING);
        s_ring_count--;
    }
    s_ring[(s_ring_head + s_ring_count) % RING] = (play_in_t){ .kind = kind, .t_us = t_us };
    s_ring_count++;
    taskEXIT_CRITICAL(&s_ring_mux);
}

static bool ring_pop(play_in_t *out) {
    bool ok = false;
    taskENTER_CRITICAL(&s_ring_mux);
    if (s_ring_count) {
        *out = s_ring[s_ring_head];
        s_ring_head = (uint8_t)((s_ring_head + 1) % RING);
        s_ring_count--;
        ok = true;
    }
    taskEXIT_CRITICAL(&s_ring_mux);
    return ok;
}

static void wake_screen(void) {
    s_dimmed = false;
    bsp_display_backlight(gd_save_backlight(s_model.save.set.brightness));
    if (bsp_lvgl_lock(500)) {
        gd_ui_set_dimmed(false);
        gd_ui_refresh();
        bsp_lvgl_unlock();
    }
}

static gd_key_t key_of(uint8_t btn) {
    return btn == BSP_BTN_UP ? GD_KEY_UP : (btn == BSP_BTN_DOWN ? GD_KEY_DOWN : GD_KEY_OK);
}

static void handle_input(const input_t *in) {
    s_last_input_us = in->t_us;
    if (s_dimmed) {
        if (in->ev == BSP_BTN_PRESS) wake_screen();   // 唤醒的那一下不执行操作
        return;
    }
    const bool jump_key = in->btn == BSP_BTN_UP || in->btn == BSP_BTN_OK;
    if (s_model.screen == GD_SCR_PLAY && s_play.active) {
        if (!s_model.paused) {
            if (jump_key) {
                if (in->ev == BSP_BTN_PRESS) ring_push(IN_PRESS, in->t_us);
                else if (in->ev == BSP_BTN_RELEASE) ring_push(IN_RELEASE, in->t_us);
                return;
            }
            // 中键:普通模式按下即暂停;练习模式按下放检查点、长按暂停
            if (s_model.practice) {
                if (in->ev == BSP_BTN_PRESS) ring_push(IN_CHECKPOINT, in->t_us);
                else if (in->ev == BSP_BTN_LONG) run_model(op_pause, NULL);
            } else if (in->ev == BSP_BTN_PRESS) {
                run_model(op_pause, NULL);
            }
            return;
        }
        // 暂停中:跳跃键的松开仍要交给对局,否则继续后会一直"按住"
        if (jump_key && in->ev == BSP_BTN_RELEASE) ring_push(IN_RELEASE, in->t_us);
    }
    gd_kev_t ev;
    switch (in->ev) {
    case BSP_BTN_PRESS: ev = GD_KEV_PRESS; break;
    case BSP_BTN_LONG: ev = GD_KEV_LONG; break;
    case BSP_BTN_RELEASE: ev = GD_KEV_RELEASE; break;
    default: ev = GD_KEV_CLICK; break;   // 双击按单击处理
    }
    key_arg_t a = { .key = key_of(in->btn), .ev = ev, .now_ms = (uint32_t)(in->t_us / 1000) };
    run_model(op_key, &a);
}

// button 回调运行在共享 esp_timer 任务:只入队并立即返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_queue) return;
    const input_t in = { .btn = (uint8_t)btn, .ev = (uint8_t)ev, .t_us = esp_timer_get_time() };
    (void)xQueueSend(s_queue, &in, 0);
}

// ---------------------------------------------------------------------------
// 帧(LVGL 任务)
// ---------------------------------------------------------------------------
#if GD_AUTOPLAY
static uint16_t s_auto_next;
static uint32_t s_auto_attempt;
static void autoplay_feed(void) {
    if (s_model.practice) return;
    const gd_replay_t *r = &GD_REPLAYS[s_model.level % GD_LEVEL_COUNT];
    if (s_game.attempt != s_auto_attempt) {
        s_auto_attempt = s_game.attempt;
        s_auto_next = 0;
    }
    const int32_t now = clock_ms(esp_timer_get_time());
    if (now == GD_NO_CLOCK) return;
    while (s_auto_next < r->count) {
        const int32_t ms = (int32_t)((r->edges[s_auto_next].tick * 1000 + GD_TICK_HZ - 1) / GD_TICK_HZ) +
                           GD_INPUT_GRACE_MS - s_model.save.set.offset_ms;
        if (ms > now) break;
        gd_play_key(&s_play, r->edges[s_auto_next].down, ms);
        s_auto_next++;
    }
}
#endif

static void frame_cb(lv_timer_t *t) {
    (void)t;
    const int64_t now = esp_timer_get_time();
    if (s_last_frame_us) {
        const uint32_t gap = (uint32_t)(now - s_last_frame_us);
        if (gap > s_gap_max_us) s_gap_max_us = gap;
    }
    s_last_frame_us = now;
    s_frames++;
    play_in_t in;
    while (ring_pop(&in)) {
        if (s_model.screen != GD_SCR_PLAY) continue;
        const int32_t ms = clock_ms(in.t_us);
        if (in.kind == IN_CHECKPOINT) gd_play_checkpoint(&s_play, ms);
        else gd_play_key(&s_play, in.kind == IN_PRESS, ms);
    }
    if (s_model.screen == GD_SCR_PLAY) {
#if GD_AUTOPLAY
        autoplay_feed();
#endif
        const uint32_t fx = gd_play_frame(&s_play, now, clock_ms(now));
        if (fx) {
            apply_ui_fx(fx, now);
            apply_side(fx);
        }
    }
    gd_ui_frame(now);
}

static void refr_cb(lv_event_t *e) {
    const int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_start_us = now;
        return;
    }
    if (!s_refr_start_us) return;
    const uint32_t d = (uint32_t)(now - s_refr_start_us);
    s_refr_sum_us += d;
    s_refr_count++;
    if (d > s_refr_max_us) s_refr_max_us = d;
}

// ---------------------------------------------------------------------------
// 周期处理(应用任务)
// ---------------------------------------------------------------------------
static void perf_log(int64_t now) {
    if (now - s_perf_log_us < PERF_LOG_US) return;
    const int64_t span = now - s_perf_log_us;
    s_perf_log_us = now;
    const bool playing = s_model.screen == GD_SCR_PLAY && !s_model.paused && s_play.active;
    const uint32_t frames = s_frames, gap = s_gap_max_us, rc = s_refr_count, rmax = s_refr_max_us;
    const uint64_t rsum = s_refr_sum_us;
    s_frames = 0;
    s_gap_max_us = 0;
    s_refr_count = 0;
    s_refr_sum_us = 0;
    s_refr_max_us = 0;
    if (!playing) return;
    // PRD §9.2 的测量口径:每秒一行,帧数、最大帧间隔、刷新耗时、堆与音频欠载
    ESP_LOGI(TAG, "perf fps=%u gap_max=%ums refr=%u(avg %uus max %uus) heap=%u largest=%u underruns=%u",
             (unsigned)(frames * 1000000 / (span ? span : 1)), (unsigned)(gap / 1000), (unsigned)rc,
             (unsigned)(rc ? rsum / rc : 0), (unsigned)rmax,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)gd_audio_underruns());
}

static void tick(int64_t now) {
    if (s_model.screen == GD_SCR_SETTINGS && s_model.clear_holding) {
        uint32_t now_ms = (uint32_t)(now / 1000);
        run_model(op_tick, &now_ms);
    }
    if (s_save_pending && gd_audio_quiet()) {
        gd_save_t copy;
        bool have = false;
        if (bsp_lvgl_lock(200)) {
            copy = s_model.save;
            have = true;
            bsp_lvgl_unlock();
        }
        if (have) {
            s_save_pending = false;
            const int64_t t0 = esp_timer_get_time();
            gd_store_save(&copy);
            ESP_LOGI(TAG, "存档已保存(%u ms)", (unsigned)((esp_timer_get_time() - t0) / 1000));
        }
    }
    if (s_battery_ok && now - s_battery_poll_us > BATTERY_POLL_US) {
        s_battery_poll_us = now;
        s_battery = bsp_battery_soc();
    }
    if (!s_dimmed && s_model.screen != GD_SCR_PLAY && now - s_last_input_us > IDLE_DIM_US) {
        s_dimmed = true;
        bsp_display_backlight(BACKLIGHT_DIM);
        if (bsp_lvgl_lock(500)) {
            gd_ui_set_dimmed(true);
            bsp_lvgl_unlock();
        }
        ESP_LOGI(TAG, "闲置 2 分钟,屏幕变暗;按任意键恢复");
    }
    perf_log(now);
}

static void app_task(void *arg) {
    (void)arg;
    for (;;) {
        input_t in;
        if (xQueueReceive(s_queue, &in, pdMS_TO_TICKS(20)) == pdTRUE) handle_input(&in);
        tick(esp_timer_get_time());
    }
}

static void ui_timer_cb(lv_timer_t *t) {
    frame_cb(t);
}

bool gd_app_start(bool audio_ok, bool battery_ok) {
    s_battery_ok = battery_ok;
    gd_model_init(&s_model);
    if (!gd_store_load(&s_model.save)) ESP_LOGI(TAG, "没有存档,使用默认设置");
    gd_play_init(&s_play, &s_model, &s_game, &IO);
    if (!gd_audio_start(audio_ok)) return false;
    gd_audio_set_volume(s_model.save.set.volume);
    if (battery_ok) s_battery = bsp_battery_soc();
    s_battery_poll_us = esp_timer_get_time();

    s_env = (gd_ui_env_t){ .model = &s_model, .game = &s_game, .track_ok = track_ok,
                           .battery = battery_ok ? battery : NULL };
    if (!bsp_lvgl_lock(1000)) return false;
    gd_ui_start(&s_env);
    lv_timer_create(ui_timer_cb, FRAME_MS, NULL);
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    }
    bsp_lvgl_unlock();

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(input_t));
    if (!s_queue) return false;
    if (xTaskCreate(app_task, "gd_app", APP_STACK, NULL, APP_PRIORITY, NULL) != pdPASS) return false;
    s_last_input_us = esp_timer_get_time();
    s_perf_log_us = s_last_input_us;
    const esp_err_t be = bsp_button_init(on_key, NULL);
    if (be != ESP_OK) ESP_LOGE(TAG, "按键初始化失败:%s", esp_err_to_name(be));
    bsp_display_backlight(gd_save_backlight(s_model.save.set.brightness));
    ESP_LOGI(TAG, "几何冲刺就绪(音乐包 %s%s);空闲堆 %u 字节,最大连续块 %u 字节", gd_music_status(),
             GD_AUTOPLAY ? ",自动游玩调试模式" : "", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return true;
}
