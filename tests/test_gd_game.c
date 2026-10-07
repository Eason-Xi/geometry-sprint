// tests/test_gd_game.c —— 关卡会话的主机测试:确定性、追步、输入时序、重开与练习检查点。
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "gd_game.h"

static gd_level_t make_level(const gd_obj_t *objs, uint16_t n, uint16_t finish_col) {
    gd_level_t lv;
    memset(&lv, 0, sizeof lv);
    lv.name = "test";
    lv.objs = objs;
    lv.obj_count = n;
    lv.finish_col = finish_col;
    return lv;
}

static const gd_obj_t OBJS[] = {
    { .x = 25, .y = 0, .type = GD_OBJ_SPIKE_UP, .len = 1 },
    { .x = 45, .y = 0, .type = GD_OBJ_BLOCK, .len = 4 },
    { .x = 60, .y = 2, .type = GD_OBJ_ORB_YELLOW, .len = 1 },
    { .x = 80, .y = 0, .type = GD_OBJ_SPIKE_UP, .len = 2 },
};

static const uint32_t PRESSES[][2] = { { 120, 140 }, { 205, 220 }, { 330, 333 }, { 400, 470 } };

static void feed(gd_game_t *g) {
    for (size_t i = 0; i < sizeof PRESSES / sizeof PRESSES[0]; i++) {
        gd_game_input(g, GD_IN_PRESS, PRESSES[i][0]);
        gd_game_input(g, GD_IN_RELEASE, PRESSES[i][1]);
    }
}

// 用不同的帧长推进同一输入序列,最终状态必须逐位相同。
static void test_frame_rate_independence(void) {
    const gd_level_t lv = make_level(OBJS, 4, 200);
    const int frames[] = { 1, 5, 17, 40 };
    gd_sim_t ref;
    for (size_t f = 0; f < sizeof frames / sizeof frames[0]; f++) {
        gd_game_t g;
        gd_game_begin(&g, &lv, false);
        feed(&g);
        uint32_t target = 0;
        while (g.run == GD_RUN_PLAYING && g.sim.tick < 900) {
            target += (uint32_t)frames[f];
            gd_game_advance(&g, target);
        }
        if (f == 0) ref = g.sim;
        else assert(memcmp(&ref, &g.sim, sizeof ref) == 0);
    }
    printf("deterministic run: tick %u dead=%d x=%d\n", ref.tick, ref.dead, ref.x >> GD_FP_SHIFT);
}

static void test_catchup_cap(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, false);
    gd_game_advance(&g, 1000);
    assert(g.sim.tick == GD_CATCHUP_MAX);
    // 落后太多时分几帧追上,不跳步
    for (int i = 0; i < 30; i++) gd_game_advance(&g, 1000);
    assert(g.sim.tick == 1000);
}

static void test_late_input_applies_now(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, false);
    gd_game_advance(&g, 30);
    gd_game_advance(&g, 50);
    assert(g.sim.tick == 50);
    gd_game_input(&g, GD_IN_PRESS, 45);   // 早于当前步到达
    gd_game_advance(&g, 51);
    assert(g.sim.jumps == 1 && !g.sim.grounded);
    assert(g.sim.press_tick == 50);
}

static void test_death_and_restart(void) {
    const gd_level_t lv = make_level(OBJS, 4, 200);
    gd_game_t g;
    gd_game_begin(&g, &lv, false);
    uint32_t ev = 0;
    for (uint32_t t = 1; t < 2000 && !(ev & GD_EV_DIED); t++) ev |= gd_game_advance(&g, t);
    assert(ev & GD_EV_DIED);
    assert(g.run == GD_RUN_DEAD);
    const uint32_t tick = g.sim.tick;
    assert(gd_game_advance(&g, tick + 100) == 0 && g.sim.tick == tick);   // 死亡后不再前进
    // 死亡期间按下的键会被记住
    gd_game_input(&g, GD_IN_PRESS, 0);
    gd_game_advance(&g, tick + 101);
    assert(g.key_down);
    const int32_t ms = gd_game_restart(&g);
    assert(ms == 0 && g.attempt == 2 && g.run == GD_RUN_PLAYING);
    assert(g.sim.tick == 0 && g.sim.x == gd_tick_x(0) && g.sim.held);
}

static void test_jumps_accumulate(void) {
    const gd_level_t lv = make_level(OBJS, 4, 200);
    gd_game_t g;
    gd_game_begin(&g, &lv, false);
    gd_game_input(&g, GD_IN_PRESS, 3);
    gd_game_input(&g, GD_IN_RELEASE, 5);
    gd_game_advance(&g, 40);
    assert(gd_game_jumps(&g) == 1);
    for (uint32_t t = 41; t < 3000 && g.run == GD_RUN_PLAYING; t++) gd_game_advance(&g, t);
    gd_game_restart(&g);
    assert(gd_game_jumps(&g) == 1);
}

static void test_practice_checkpoints(void) {
    // 平地 + 远处一个必死的高墙
    static const gd_obj_t wall[] = { { .x = 150, .y = 0, .type = GD_OBJ_BLOCK, .len = 2 },
                                      { .x = 150, .y = 1, .type = GD_OBJ_BLOCK, .len = 2 },
                                      { .x = 150, .y = 2, .type = GD_OBJ_BLOCK, .len = 2 },
                                      { .x = 150, .y = 3, .type = GD_OBJ_BLOCK, .len = 2 } };
    const gd_level_t lv = make_level(wall, 4, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, true);
    uint32_t ev = 0, ckpts = 0;
    for (uint32_t t = 1; t < 4000 && !(ev & GD_EV_DIED); t++) {
        const uint32_t e = gd_game_advance(&g, t);
        if (e & GD_EV_CHECKPOINT) ckpts++;
        ev |= e;
    }
    assert(ev & GD_EV_DIED);
    // 走到墙前约 2900 px / 208 px/s ≈ 13.9 s,每 2 s 一个自动检查点
    printf("practice: %u auto checkpoints before death at tick %u\n", ckpts, g.sim.tick);
    assert(ckpts >= 6 && ckpts <= 7);
    assert(g.ckpt_n == ckpts);
    const int32_t latest = gd_game_ckpt_x(&g, 0);
    assert(latest != INT32_MIN && latest < g.sim.death_x);
    assert(gd_game_ckpt_x(&g, (int)ckpts) == INT32_MIN);
    const int32_t ms = gd_game_restart(&g);
    assert(g.sim.x == latest && g.attempt == 2);
    assert(ms == gd_tick_ms(g.sim.tick) && ms > 0);
    assert(g.attempt_start_x == latest);
}

static void test_manual_checkpoint_only_when_safe(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, true);
    gd_game_input(&g, GD_IN_PRESS, 10);
    gd_game_input(&g, GD_IN_RELEASE, 12);
    gd_game_input(&g, GD_IN_CHECKPOINT, 40);    // 空中:忽略
    gd_game_input(&g, GD_IN_CHECKPOINT, 200);   // 着地:放置
    uint32_t ev = 0;
    for (uint32_t t = 1; t <= 210; t++) ev |= gd_game_advance(&g, t);
    assert(ev & GD_EV_CHECKPOINT);
    assert(g.ckpt_n == 1);
    // 普通模式不放检查点
    gd_game_begin(&g, &lv, false);
    gd_game_input(&g, GD_IN_CHECKPOINT, 5);
    gd_game_advance(&g, 10);
    assert(g.ckpt_n == 0);
}

static void test_checkpoint_ring(void) {
    const gd_level_t lv = make_level(NULL, 0, 4000);
    gd_game_t g;
    gd_game_begin(&g, &lv, true);
    for (uint32_t t = 1; t <= 480u * 40; t++) gd_game_advance(&g, t);
    assert(g.ckpt_n == GD_MAX_CKPT);
    assert(gd_game_ckpt_x(&g, 0) > gd_game_ckpt_x(&g, GD_MAX_CKPT - 1));
}

static void test_set_practice_resets(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, true);
    for (uint32_t t = 1; t <= 1200; t++) gd_game_advance(&g, t);
    assert(g.ckpt_n > 0);
    assert(gd_game_set_practice(&g, false) == 0);
    assert(!g.practice && g.ckpt_n == 0 && g.sim.tick == 0 && g.attempt == 2);
}

static void test_input_ring_overflow(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_game_t g;
    gd_game_begin(&g, &lv, false);
    for (int i = 0; i < GD_INPUT_RING + 5; i++) gd_game_input(&g, (i % 2) ? GD_IN_RELEASE : GD_IN_PRESS, 100u + (uint32_t)i);
    assert(g.in_count == GD_INPUT_RING);
    assert(g.in[g.in_head].tick == 105);
}

int main(void) {
    test_frame_rate_independence();
    test_catchup_cap();
    test_late_input_applies_now();
    test_death_and_restart();
    test_jumps_accumulate();
    test_practice_checkpoints();
    test_manual_checkpoint_only_when_safe();
    test_checkpoint_ring();
    test_set_practice_resets();
    test_input_ring_overflow();
    printf("Geometry Sprint session tests: PASS\n");
    return 0;
}
