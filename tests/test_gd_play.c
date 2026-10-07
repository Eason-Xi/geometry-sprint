// tests/test_gd_play.c —— 对局控制器的主机测试:音乐与时钟对接、死亡重开、练习续播、通关结算、暂停与统计。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gd_play.h"
#include "gd_replay.h"
#include "gd_sfx.h"
#include "gd_ui.h"

// —— 界面桩:记录事件 ——
static int s_begin, s_died, s_finished, s_ckpt, s_hint;
static const char *s_hint_text;
void gd_ui_play_begin(int64_t now_us) { (void)now_us; s_begin++; }
void gd_ui_play_died(int64_t now_us) { (void)now_us; s_died++; }
void gd_ui_play_finished(int64_t now_us) { (void)now_us; s_finished++; }
void gd_ui_play_checkpoint(int64_t now_us) { (void)now_us; s_ckpt++; }
void gd_ui_play_hint(const char *text, int64_t now_us, int32_t ms) {
    (void)now_us;
    (void)ms;
    s_hint++;
    s_hint_text = text;
}

// —— 音频桩:歌曲时钟 = 开播位置 + 经过时间(暂停时冻结) ——
static bool s_on, s_paused;
static int32_t s_base;
static int64_t s_t0, s_now;
static uint8_t s_track;
static int s_plays, s_stops, s_pauses, s_saves, s_sfx[GD_SFX_COUNT];

static void io_play(uint8_t track, int32_t start_ms) {
    s_on = true;
    s_paused = false;
    s_track = track;
    s_base = start_ms;
    s_t0 = s_now;
    s_plays++;
}
static void io_stop(void) {
    s_on = false;
    s_stops++;
}
static void io_pause(bool p) {
    if (p && !s_paused) s_base += (int32_t)((s_now - s_t0) / 1000);
    if (!p && s_paused) s_t0 = s_now;
    s_paused = p;
    s_pauses++;
}
static void io_sfx(uint8_t id) {
    s_sfx[id]++;
}
static void io_save(void) {
    s_saves++;
}
static const gd_play_io_t IO = { io_play, io_stop, io_pause, io_sfx, io_save };

static int32_t song(void) {
    if (!s_on) return GD_NO_CLOCK;
    return s_paused ? s_base : s_base + (int32_t)((s_now - s_t0) / 1000);
}

static gd_model_t m;
static gd_game_t g;
static gd_play_t p;

static uint32_t step(int ms) {
    uint32_t fx = 0;
    for (int t = 0; t < ms; t += 20) {
        s_now += 20000;
        fx |= gd_play_frame(&p, s_now, song());
    }
    return fx;
}

static void reset(int level, bool practice) {
    memset(s_sfx, 0, sizeof s_sfx);
    s_begin = s_died = s_finished = s_ckpt = s_hint = s_plays = s_stops = s_pauses = s_saves = 0;
    s_on = false;
    s_now = 1000000;
    gd_model_init(&m);
    m.screen = GD_SCR_PLAY;
    m.level = (uint8_t)level;
    m.practice = practice;
    gd_play_init(&p, &m, &g, &IO);
}

static void test_start_and_first_hint(void) {
    reset(0, false);
    gd_play_start(&p, s_now);
    assert(s_plays == 1 && s_track == GD_LEVELS[0].track && s_base == 0 && s_begin == 1);
    assert(s_hint == 1 && m.save.hint_shown && m.save.lv[0].attempts == 1);
    step(1000);
    assert(g.sim.tick > 200 && !g.sim.dead);
    // 物理步跟随歌曲时间(含 6 ms 输入宽限)
    assert(g.sim.tick == gd_play_tick_at(&p, song()));
    // 第二次进入第 1 关不再提示
    reset(0, false);
    m.save.hint_shown = true;
    gd_play_start(&p, s_now);
    assert(s_hint == 0);
}

static void test_death_restart(void) {
    reset(1, false);
    gd_play_start(&p, s_now);
    for (int i = 0; i < 1000 && !g.sim.dead; i++) step(20);
    assert(g.sim.dead && s_died == 1 && s_stops == 1 && s_sfx[GD_SFX_DEATH] == 1);
    assert(m.save.lv[1].best > 0 && s_saves == 1);   // 新纪录:死亡期间请求存档
    const int deaths_progress = m.save.lv[1].best;
    step(GD_DEATH_MS - 40);
    assert(s_plays == 1 && g.sim.dead);
    step(60);
    assert(s_plays == 2 && s_base == 0 && !g.sim.dead && g.attempt == 2 && m.save.lv[1].attempts == 2);
    // 第二次死在同一处:不是新纪录,不再请求存档
    for (int i = 0; i < 1000 && !g.sim.dead; i++) step(20);
    assert(m.save.lv[1].best == deaths_progress && s_saves == 1 && m.new_best == -1);
}

static void test_inputs_follow_clock(void) {
    reset(0, false);
    m.save.hint_shown = true;
    gd_play_start(&p, s_now);
    step(400);
    // 在歌曲 450 ms 处按下:物理在对应步起跳
    s_now += 30000;
    const int32_t at = song();
    gd_play_key(&p, true, at);
    gd_play_key(&p, false, at + 30);
    step(100);
    assert(g.sim.jumps == 1 && g.sim.press_tick == gd_play_tick_at(&p, at));
}

static void test_practice_resume_from_checkpoint(void) {
    reset(1, true);
    gd_play_start(&p, s_now);
    // 用通关录像玩一段,累积检查点,然后松手撞死
    const gd_replay_t *r = &GD_REPLAYS[1];
    uint16_t next = 0;
    for (int i = 0; i < 600; i++) {   // 12 s
        s_now += 20000;
        const int32_t now = song();
        while (next < r->count && (int32_t)((r->edges[next].tick * 1000 + 239) / 240) + GD_INPUT_GRACE_MS <= now) {
            gd_play_key(&p, r->edges[next].down, (int32_t)((r->edges[next].tick * 1000 + 239) / 240) + GD_INPUT_GRACE_MS);
            next++;
        }
        gd_play_frame(&p, s_now, now);
    }
    assert(s_ckpt >= 3 && s_sfx[GD_SFX_CHECKPOINT] == s_ckpt && g.ckpt_n == s_ckpt);
    gd_play_key(&p, false, song());
    for (int i = 0; i < 2000 && !g.sim.dead; i++) step(20);
    assert(g.sim.dead);
    const int32_t ckpt_x = gd_game_ckpt_x(&g, 0);
    for (int i = 0; i < 100 && s_plays < 2; i++) step(20);
    // 音乐从检查点对应的歌曲时间续播,物理在时钟赶上之前不前进
    assert(s_plays == 2 && s_base == gd_tick_ms(g.sim.tick) && g.sim.x == ckpt_x);
    const uint32_t t0 = g.sim.tick;
    step(100);
    assert(g.sim.tick > t0 && g.sim.tick <= t0 + 30);
}

static void test_finish_and_result(void) {
    reset(0, false);
    m.save.hint_shown = true;
    gd_play_start(&p, s_now);
    const gd_replay_t *r = &GD_REPLAYS[0];
    uint16_t next = 0;
    uint32_t fx = 0;
    for (int i = 0; i < 6000 && !(fx & GD_FX_SCREEN); i++) {
        s_now += 20000;
        const int32_t now = song();
        while (now != GD_NO_CLOCK && next < r->count &&
               (int32_t)((r->edges[next].tick * 1000 + 239) / 240) + GD_INPUT_GRACE_MS <= now) {
            gd_play_key(&p, r->edges[next].down, (int32_t)((r->edges[next].tick * 1000 + 239) / 240) + GD_INPUT_GRACE_MS);
            next++;
        }
        fx = gd_play_frame(&p, s_now, now);
        if (s_finished && !(fx & GD_FX_SCREEN)) assert(m.screen == GD_SCR_PLAY);
    }
    assert(s_finished == 1 && s_sfx[GD_SFX_COMPLETE] == 1);
    assert((fx & GD_FX_SCREEN) && (fx & GD_FX_SAVE) && m.screen == GD_SCR_RESULT && !p.active);
    assert(m.result.attempts == 1 && m.result.first_clear && m.save.lv[0].completed);
    assert(m.result.time_ms >= 89000 && m.result.time_ms <= 91000);
    assert(m.save.lv[0].jumps == gd_game_jumps(&g) && m.save.play_seconds >= 90);
    assert(!s_on);   // 进入结算时音乐已停
}

static void test_pause_and_restart(void) {
    reset(0, false);
    m.save.hint_shown = true;
    gd_play_start(&p, s_now);
    step(2000);
    m.paused = true;
    gd_play_pause(&p, true, s_now);
    const uint32_t tick = g.sim.tick;
    step(1000);
    assert(g.sim.tick == tick && s_paused);
    m.paused = false;
    gd_play_pause(&p, false, s_now);
    step(200);
    assert(g.sim.tick > tick && !s_paused);
    gd_play_restart(&p, s_now);
    assert(s_plays == 2 && g.attempt == 2 && g.sim.tick == 0 && m.save.lv[0].attempts == 2);
    // 切到练习模式:从头开始并给出提示
    m.practice = true;
    gd_play_set_practice(&p, s_now);
    assert(g.practice && s_hint >= 1 && g.attempt == 3);
    gd_play_stop(&p);
    assert(!p.active && !s_on);
}

int main(void) {
    test_start_and_first_hint();
    test_death_restart();
    test_inputs_follow_clock();
    test_practice_resume_from_checkpoint();
    test_finish_and_result();
    test_pause_and_restart();
    printf("Geometry Sprint play controller tests: PASS\n");
    return 0;
}
