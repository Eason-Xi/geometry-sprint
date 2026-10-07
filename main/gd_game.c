// main/gd_game.c —— 关卡会话,说明见 gd_game.h。
#include "gd_game.h"

#include <limits.h>
#include <string.h>

static void start_attempt_at(gd_game_t *g, const gd_ckpt_t *c) {
    const bool down = g->key_down;
    gd_sim_reset(&g->sim);
    if (c) {
        g->sim.x = c->x;
        g->sim.y = c->y;
        g->sim.vy = c->vy;
        g->sim.grav = c->grav;
        g->sim.mode = c->mode;
        g->sim.grounded = c->grounded;
        g->sim.rot = c->rot;
        g->sim.tick = c->tick;
    }
    // 按键状态延续到新尝试;不制造按下沿(重开那一刻不会自动起跳一次以外的动作)。
    g->sim.held = down;
    g->sim.press_tick = g->sim.tick;
    g->run = GD_RUN_PLAYING;
    g->attempt_start_tick = g->sim.tick;
    g->attempt_start_x = g->sim.x;
    g->last_ckpt_tick = g->sim.tick;
    g->in_count = 0;
}

void gd_game_begin(gd_game_t *g, const gd_level_t *lv, bool practice) {
    memset(g, 0, sizeof *g);
    g->lv = lv;
    g->practice = practice;
    g->attempt = 1;
    start_attempt_at(g, NULL);
}

void gd_game_input(gd_game_t *g, gd_in_kind_t kind, uint32_t tick) {
    if (g->in_count == GD_INPUT_RING) {
        g->in_head = (uint8_t)((g->in_head + 1) % GD_INPUT_RING);
        g->in_count--;
    }
    g->in[(g->in_head + g->in_count) % GD_INPUT_RING] = (gd_input_t){ .tick = tick, .kind = (uint8_t)kind };
    g->in_count++;
}

static void push_ckpt(gd_game_t *g) {
    const gd_sim_t *s = &g->sim;
    g->ckpt[g->ckpt_head] = (gd_ckpt_t){
        .x = s->x, .y = s->y, .vy = s->vy, .grav = s->grav, .mode = s->mode,
        .grounded = s->grounded, .rot = s->rot, .tick = s->tick,
    };
    g->ckpt_head = (uint8_t)((g->ckpt_head + 1) % GD_MAX_CKPT);
    if (g->ckpt_n < GD_MAX_CKPT) g->ckpt_n++;
    g->last_ckpt_tick = s->tick;
}

// 手动检查点只放在"安全"的位置:着地,或者在飞船形态中。
static bool safe_for_ckpt(const gd_sim_t *s) {
    return !s->dead && !s->finished && (s->grounded || s->mode == GD_MODE_SHIP);
}

// 处理所有 tick ≤ 当前步的输入;返回是否放置了检查点。
static bool apply_inputs(gd_game_t *g) {
    bool placed = false;
    while (g->in_count) {
        const gd_input_t *in = &g->in[g->in_head];
        if (in->tick > g->sim.tick) break;
        switch (in->kind) {
        case GD_IN_PRESS:
            g->key_down = true;
            gd_sim_set_held(&g->sim, true);
            break;
        case GD_IN_RELEASE:
            g->key_down = false;
            gd_sim_set_held(&g->sim, false);
            break;
        case GD_IN_CHECKPOINT:
            if (g->practice && safe_for_ckpt(&g->sim)) {
                push_ckpt(g);
                placed = true;
            }
            break;
        default:
            break;
        }
        g->in_head = (uint8_t)((g->in_head + 1) % GD_INPUT_RING);
        g->in_count--;
    }
    return placed;
}

uint32_t gd_game_advance(gd_game_t *g, uint32_t target_tick) {
    uint32_t ev = 0;
    int steps = 0;
    while (g->run == GD_RUN_PLAYING && g->sim.tick < target_tick && steps < GD_CATCHUP_MAX) {
        if (apply_inputs(g)) ev |= GD_EV_CHECKPOINT;
        gd_sim_step(&g->sim, g->lv);
        steps++;
        if (g->sim.dead) {
            g->run = GD_RUN_DEAD;
            ev |= GD_EV_DIED;
            break;
        }
        if (g->sim.finished) {
            g->run = GD_RUN_DONE;
            ev |= GD_EV_FINISHED;
            break;
        }
        if (g->practice && g->sim.mode == GD_MODE_CUBE && g->sim.grounded_ticks >= GD_AUTO_CKPT_GROUND &&
            g->sim.tick - g->last_ckpt_tick >= GD_AUTO_CKPT_GAP) {
            push_ckpt(g);
            ev |= GD_EV_CHECKPOINT;
        }
    }
    // 追步结束后到达的、已经过期的松开/按下也要更新按键状态,避免死亡期间状态错乱。
    if (g->run != GD_RUN_PLAYING) {
        while (g->in_count) {
            const gd_input_t *in = &g->in[g->in_head];
            if (in->kind == GD_IN_PRESS) g->key_down = true;
            if (in->kind == GD_IN_RELEASE) g->key_down = false;
            g->in_head = (uint8_t)((g->in_head + 1) % GD_INPUT_RING);
            g->in_count--;
        }
    }
    return ev;
}

int32_t gd_game_restart(gd_game_t *g) {
    g->jumps_done += g->sim.jumps;
    g->attempt++;
    if (g->practice && g->ckpt_n) {
        const uint8_t last = (uint8_t)((g->ckpt_head + GD_MAX_CKPT - 1) % GD_MAX_CKPT);
        start_attempt_at(g, &g->ckpt[last]);
    } else {
        start_attempt_at(g, NULL);
    }
    return gd_tick_ms(g->sim.tick);
}

int32_t gd_game_set_practice(gd_game_t *g, bool practice) {
    g->jumps_done += g->sim.jumps;
    g->practice = practice;
    g->ckpt_n = 0;
    g->ckpt_head = 0;
    g->attempt++;
    start_attempt_at(g, NULL);
    return 0;
}

uint32_t gd_game_jumps(const gd_game_t *g) {
    return g->jumps_done + g->sim.jumps;
}

int32_t gd_game_ckpt_x(const gd_game_t *g, int idx_from_latest) {
    if (idx_from_latest < 0 || idx_from_latest >= g->ckpt_n) return INT32_MIN;
    const int i = (g->ckpt_head + GD_MAX_CKPT - 1 - idx_from_latest) % GD_MAX_CKPT;
    return g->ckpt[i].x;
}
