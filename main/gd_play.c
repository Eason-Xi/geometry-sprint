// main/gd_play.c —— 对局控制器,说明见 gd_play.h。
#include "gd_play.h"

#include "gd_sfx.h"
#include "gd_strings.h"
#include "gd_ui.h"

#define HINT_MS 3000

static const gd_level_t *level(const gd_play_t *p) {
    return &GD_LEVELS[p->m->level % GD_LEVEL_COUNT];
}

void gd_play_init(gd_play_t *p, gd_model_t *m, gd_game_t *g, const gd_play_io_t *io) {
    *p = (gd_play_t){ .m = m, .g = g, .io = io };
    gd_game_begin(g, &GD_LEVELS[0], false);
}

uint32_t gd_play_tick_at(const gd_play_t *p, int32_t song_ms) {
    return gd_ms_tick(song_ms + p->m->save.set.offset_ms - GD_INPUT_GRACE_MS);
}

// 把尚未计入的跳跃数与游戏时长写进模型。
static void flush_stats(gd_play_t *p) {
    const uint32_t jumps = gd_game_jumps(p->g);
    const uint32_t new_jumps = jumps > p->jumps_flushed ? jumps - p->jumps_flushed : 0;
    gd_model_add_stats(p->m, new_jumps, p->play_ms / 1000);
    p->jumps_flushed = jumps;
    p->play_ms %= 1000;
}

// 新的一次尝试:音乐从 start_ms 开始,界面回到起点。
static void begin_attempt(gd_play_t *p, int32_t start_ms, int64_t now_us) {
    p->death_us = 0;
    p->finish_us = 0;
    p->clock_ready = false;
    gd_model_attempt(p->m);
    p->io->music_play(level(p)->track, start_ms);
    gd_ui_play_begin(now_us);
}

void gd_play_start(gd_play_t *p, int64_t now_us) {
    gd_game_begin(p->g, level(p), p->m->practice);
    p->active = true;
    p->jumps_flushed = 0;
    p->play_ms = 0;
    p->last_frame_us = now_us;
    begin_attempt(p, 0, now_us);
    if (p->m->level == 0 && !p->m->save.hint_shown) {
        p->m->save.hint_shown = true;
        gd_ui_play_hint(S_HINT_FIRST, now_us, HINT_MS);
    }
}

void gd_play_key(gd_play_t *p, bool down, int32_t song_ms) {
    if (!p->active) return;
    const uint32_t tick = (song_ms == GD_NO_CLOCK || !p->clock_ready) ? 0 : gd_play_tick_at(p, song_ms);
    gd_game_input(p->g, down ? GD_IN_PRESS : GD_IN_RELEASE, tick);
}

void gd_play_checkpoint(gd_play_t *p, int32_t song_ms) {
    if (!p->active || !p->g->practice || song_ms == GD_NO_CLOCK || !p->clock_ready) return;
    gd_game_input(p->g, GD_IN_CHECKPOINT, gd_play_tick_at(p, song_ms));
}

static void on_died(gd_play_t *p, int64_t now_us) {
    p->death_us = now_us ? now_us : 1;
    p->io->music_stop();
    p->io->sfx(GD_SFX_DEATH);
    const uint32_t fx = gd_model_died(p->m, gd_sim_progress(&p->g->sim, p->g->lv));
    if (fx & GD_FX_SAVE) {
        flush_stats(p);
        p->io->save();   // 音乐已停:死亡动画期间写存档
    }
    gd_ui_play_died(now_us);
}

static void on_finished(gd_play_t *p, int64_t now_us) {
    p->finish_us = now_us ? now_us : 1;
    const uint32_t ticks = p->g->sim.tick - p->g->attempt_start_tick;
    gd_model_finished(p->m, p->g->attempt, gd_game_jumps(p->g), (uint32_t)gd_tick_ms(ticks));
    p->io->sfx(GD_SFX_COMPLETE);
    gd_ui_play_finished(now_us);
}

uint32_t gd_play_frame(gd_play_t *p, int64_t now_us, int32_t song_ms) {
    if (!p->active) return 0;
    const int64_t dt = now_us - p->last_frame_us;
    p->last_frame_us = now_us;
    if (p->m->paused) return 0;
    if (dt > 0 && dt < 1000000 && !p->death_us) p->play_ms += (uint32_t)(dt / 1000);

    if (p->death_us) {
        gd_game_advance(p->g, 0);   // 只消化死亡期间的按键状态
        if (now_us - p->death_us >= (int64_t)GD_DEATH_MS * 1000) {
            const int32_t start_ms = gd_game_restart(p->g);
            begin_attempt(p, start_ms, now_us);
        }
        return 0;
    }
    if (p->finish_us) {
        if (now_us - p->finish_us >= (int64_t)GD_COMPLETE_MS * 1000) {
            gd_play_stop(p);   // 停音乐、计入统计;随后结算页写存档
            return gd_model_show_result(p->m);
        }
        return 0;
    }
    if (song_ms == GD_NO_CLOCK) return 0;
    // 时钟刚对准本次尝试(练习模式从检查点开始时,歌曲时间也从检查点开始)
    if (!p->clock_ready) {
        const uint32_t at = gd_play_tick_at(p, song_ms);
        if (at + 2 < p->g->sim.tick && p->g->sim.tick > 0) return 0;   // 旧时钟或尚在预备
        p->clock_ready = true;
    }
    const uint32_t ev = gd_game_advance(p->g, gd_play_tick_at(p, song_ms));
    if (ev & GD_EV_CHECKPOINT) {
        p->io->sfx(GD_SFX_CHECKPOINT);
        gd_ui_play_checkpoint(now_us);
    }
    if (ev & GD_EV_DIED) on_died(p, now_us);
    if (ev & GD_EV_FINISHED) on_finished(p, now_us);
    return 0;
}

void gd_play_pause(gd_play_t *p, bool paused, int64_t now_us) {
    if (!p->active) return;
    p->last_frame_us = now_us;
    if (p->death_us || p->finish_us) return;   // 死亡 / 完成动画期间音乐已停或即将结束
    p->io->music_pause(paused);
    if (paused) flush_stats(p);
}

void gd_play_restart(gd_play_t *p, int64_t now_us) {
    if (!p->active) return;
    flush_stats(p);
    p->io->music_stop();
    p->g->practice = p->m->practice;
    gd_game_set_practice(p->g, p->m->practice);   // 回到开头,清空检查点
    p->jumps_flushed = gd_game_jumps(p->g);
    begin_attempt(p, 0, now_us);
}

void gd_play_set_practice(gd_play_t *p, int64_t now_us) {
    gd_play_restart(p, now_us);
    if (p->m->practice) gd_ui_play_hint(S_HINT_PRACTICE, now_us, HINT_MS);
}

void gd_play_stop(gd_play_t *p) {
    if (!p->active) return;
    flush_stats(p);
    p->active = false;
    p->io->music_stop();
}
