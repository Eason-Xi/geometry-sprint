// tests/test_gd_sim.c —— 几何冲刺物理模拟的主机测试(PRD §7 的不变量与机制细则)。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gd_sim.h"

#define CEIL_Q GD_PX(GD_CEIL_PX)

static gd_level_t make_level(const gd_obj_t *objs, uint16_t n, uint16_t finish_col) {
    gd_level_t lv;
    memset(&lv, 0, sizeof lv);
    lv.name = "test";
    lv.objs = objs;
    lv.obj_count = n;
    lv.finish_col = finish_col;
    return lv;
}

// 在 press_tick 单击一下(按下 1 步后松开),一直跑到 until_tick 或死亡。
static gd_sim_t run_tap(const gd_level_t *lv, uint32_t press_tick, uint32_t until_tick) {
    gd_sim_t s;
    gd_sim_reset(&s);
    while (s.tick < until_tick && !s.dead && !s.finished) {
        if (s.tick == press_tick) gd_sim_set_held(&s, true);
        if (s.tick == press_tick + 1) gd_sim_set_held(&s, false);
        gd_sim_step(&s, lv);
    }
    return s;
}

static uint32_t tick_at_col(int col) {
    return (uint32_t)((GD_PX(col * GD_CELL_PX) - gd_tick_x(0)) / GD_SPEED);
}

static void test_jump_shape(void) {
    const gd_level_t lv = make_level(NULL, 0, 200);
    gd_sim_t s;
    gd_sim_reset(&s);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    int32_t apex = 0;
    uint32_t air = 0;
    while (!s.grounded || air == 0) {
        gd_sim_step(&s, &lv);
        if (s.y > apex) apex = s.y;
        air++;
        assert(air < 400);
    }
    const int apex_px = (apex >> GD_FP_SHIFT) - GD_HALF_PX;
    printf("jump: apex %d px, airtime %u ticks (%u ms)\n", apex_px, air, air * 1000 / GD_TICK_HZ);
    assert(apex_px >= 40 && apex_px <= 44);              // ≈ 2.1 格
    assert(air >= 96 && air <= 106);                      // ≈ 0.42 s
    assert(s.jumps == 1);
}

// 是否存在一次单击能越过 n 连刺并安全落地。
static bool spikes_clearable(int n) {
    gd_obj_t objs[1] = { { .x = 30, .y = 0, .type = GD_OBJ_SPIKE_UP, .len = (uint8_t)n } };
    const gd_level_t lv = make_level(objs, 1, 200);
    const uint32_t first = tick_at_col(30);
    for (uint32_t t = first > 150 ? first - 150 : 0; t < first + 20; t++) {
        const gd_sim_t s = run_tap(&lv, t, tick_at_col(30 + n + 6));
        if (!s.dead) return true;
    }
    return false;
}

static void test_spike_invariant(void) {
    assert(spikes_clearable(1));
    assert(spikes_clearable(2));
    assert(spikes_clearable(3));
    assert(!spikes_clearable(4));
}

// 高 h 格、宽 3 格的方块台阶:单击一次能否站上去并越过。
static bool platform_reachable(int h) {
    gd_obj_t objs[8];
    for (int r = 0; r < h; r++) objs[r] = (gd_obj_t){ .x = 30, .y = (uint8_t)r, .type = GD_OBJ_BLOCK, .len = 3 };
    const gd_level_t lv = make_level(objs, (uint16_t)h, 200);
    const uint32_t first = tick_at_col(30);
    for (uint32_t t = first > 150 ? first - 150 : 0; t < first + 10; t++) {
        const gd_sim_t s = run_tap(&lv, t, tick_at_col(32));
        if (!s.dead && (s.y >> GD_FP_SHIFT) - GD_HALF_PX == h * GD_CELL_PX && s.grounded) return true;
    }
    return false;
}

static void test_platform_invariant(void) {
    assert(platform_reachable(1));
    assert(platform_reachable(2));
    assert(!platform_reachable(3));
}

static void test_wall_kills(void) {
    gd_obj_t objs[] = { { .x = 20, .y = 0, .type = GD_OBJ_BLOCK, .len = 2 } };
    const gd_level_t lv = make_level(objs, 1, 200);
    gd_sim_t s;
    gd_sim_reset(&s);
    while (!s.dead && s.tick < 2000) gd_sim_step(&s, &lv);
    assert(s.dead);
    // 内核撞墙:死亡时外框已压入方块,但内核才刚刚接触
    const int32_t wall = GD_PX(20 * GD_CELL_PX);
    assert(s.death_x + GD_PX(GD_CORE_HALF_PX) > wall);
    assert(s.death_x + GD_PX(GD_CORE_HALF_PX) <= wall + GD_SPEED);
}

static void test_hold_to_jump(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    gd_sim_set_held(&s, true);
    for (int i = 0; i < 240 * 3; i++) gd_sim_step(&s, &lv);
    // 3 秒按住:每次落地立即再跳,约 3000 / 421 ≈ 7 次
    printf("hold 3 s: %u jumps\n", s.jumps);
    assert(s.jumps >= 7 && s.jumps <= 8);
    gd_sim_set_held(&s, false);
    for (int i = 0; i < 240; i++) gd_sim_step(&s, &lv);
    const uint32_t after = s.jumps;
    for (int i = 0; i < 240; i++) gd_sim_step(&s, &lv);
    assert(s.jumps == after && s.grounded);
}

static void test_tap_while_airborne_does_nothing(void) {
    const gd_level_t lv = make_level(NULL, 0, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    for (int i = 0; i < 30; i++) gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, true);   // 空中单击:不能二段跳
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    for (int i = 0; i < 200; i++) gd_sim_step(&s, &lv);
    assert(s.jumps == 1);
}

static void test_ship(void) {
    gd_obj_t objs[] = { { .x = 5, .y = 0, .type = GD_OBJ_PORTAL_SHIP, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    while (s.mode != GD_MODE_SHIP) gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, true);
    const int32_t y0 = s.y;
    for (int i = 0; i < 60; i++) gd_sim_step(&s, &lv);
    assert(s.y > y0);
    for (int i = 0; i < 600; i++) gd_sim_step(&s, &lv);   // 一直按住:贴着天花板,不死
    assert(!s.dead && s.y == CEIL_Q - GD_PX(GD_HALF_PX));
    gd_sim_set_held(&s, false);
    for (int i = 0; i < 600; i++) gd_sim_step(&s, &lv);   // 松开:落到地面
    assert(!s.dead && s.y == GD_PX(GD_HALF_PX));
    assert(s.vy == 0);
}

static void test_ship_speed_limit(void) {
    gd_obj_t objs[] = { { .x = 2, .y = 3, .type = GD_OBJ_PORTAL_SHIP, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    gd_sim_set_held(&s, true);
    int32_t vmax = 0;
    for (int i = 0; i < 300; i++) {
        gd_sim_step(&s, &lv);
        if (s.mode == GD_MODE_SHIP && s.vy > vmax) vmax = s.vy;
    }
    assert(vmax == GD_SHIP_VMAX);
}

static void test_ship_slides_on_block(void) {
    gd_obj_t objs[] = {
        { .x = 3, .y = 0, .type = GD_OBJ_PORTAL_SHIP, .len = 1 },
        { .x = 20, .y = 0, .type = GD_OBJ_BLOCK, .len = 10 },   // 先落到地上再滑上来会撞墙,
    };
    const gd_level_t lv = make_level(objs, 2, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    // 先按住升高,越过方块后松开:落在方块顶面滑行,不死亡
    gd_sim_set_held(&s, true);
    while (s.x < GD_PX(16 * GD_CELL_PX)) gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    while (s.x < GD_PX(29 * GD_CELL_PX) && !s.dead) gd_sim_step(&s, &lv);
    assert(!s.dead);
    assert(s.y == GD_PX(GD_CELL_PX + GD_HALF_PX));
}

static void test_yellow_pad(void) {
    gd_obj_t objs[] = { { .x = 10, .y = 0, .type = GD_OBJ_PAD_YELLOW, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    int32_t apex = 0;
    for (int i = 0; i < 400; i++) {
        gd_sim_step(&s, &lv);
        if (s.y > apex) apex = s.y;
    }
    const int apex_px = (apex >> GD_FP_SHIFT) - GD_HALF_PX;
    printf("yellow pad apex %d px\n", apex_px);
    assert(apex_px >= 72 && apex_px <= 80);   // ≈ 3.8 格
    assert(s.jumps == 0);                     // 跳板不计跳跃
}

// 在 offset_ticks(相对进入跳环的那一步)按下并一直按住,返回跳环是否触发。
static bool orb_triggered(int offset_ticks, bool hold) {
    gd_obj_t objs[] = { { .x = 20, .y = 2, .type = GD_OBJ_ORB_YELLOW, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    // 先跳起来,在空中找到第一次与跳环重叠的那一步
    gd_sim_t probe;
    gd_sim_reset(&probe);
    const uint32_t jump_tick = tick_at_col(16);
    uint32_t enter = 0;
    while (probe.tick < 2000) {
        if (probe.tick == jump_tick) gd_sim_set_held(&probe, true);
        if (probe.tick == jump_tick + 1) gd_sim_set_held(&probe, false);
        gd_sim_step(&probe, &lv);
        if (probe.orb_prev == 0) {
            enter = probe.tick;   // 重叠首次出现在这一步之后
            break;
        }
    }
    assert(enter);
    gd_sim_t s;
    gd_sim_reset(&s);
    const uint32_t press_at = (uint32_t)((int)enter - 1 + offset_ticks);
    while (s.tick < enter + 60) {
        if (s.tick == jump_tick) gd_sim_set_held(&s, true);
        if (s.tick == jump_tick + 1) gd_sim_set_held(&s, false);
        if (s.tick == press_at) gd_sim_set_held(&s, true);
        if (!hold && s.tick == press_at + 1) gd_sim_set_held(&s, false);
        gd_sim_step(&s, &lv);
    }
    return (s.used[0] & 1u) != 0;   // 只看跳环是否触发(按住落地后的再跳不算)
}

static void test_orbs(void) {
    assert(orb_triggered(0, false));         // 进入那一步按下
    assert(orb_triggered(3, false));         // 重叠期间按下
    assert(orb_triggered(-8, true));         // 提前 33 ms 按下并按住:缓冲生效
    assert(!orb_triggered(-8, false));       // 提前按下但已松开:不触发
    assert(!orb_triggered(-30, true));       // 提前 125 ms:超出缓冲
}

static void test_orb_once_and_reset(void) {
    gd_obj_t objs[] = { { .x = 10, .y = 0, .type = GD_OBJ_ORB_YELLOW, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    // 在跳环里连按两次:只触发一次
    while (s.orb_prev != 0) gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    gd_sim_step(&s, &lv);
    const uint32_t j = s.jumps;
    assert(j == 1);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    assert(s.jumps == j);
    gd_sim_clear_used(&s);
    assert(s.orb_prev == -1);
}

static void test_gravity_portal(void) {
    gd_obj_t objs[] = {
        { .x = 10, .y = 1, .type = GD_OBJ_PORTAL_FLIP, .len = 1 },
        { .x = 60, .y = 7, .type = GD_OBJ_PORTAL_NORMAL, .len = 1 },
    };
    const gd_level_t lv = make_level(objs, 2, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    while (s.grav > 0) gd_sim_step(&s, &lv);
    for (int i = 0; i < 200; i++) gd_sim_step(&s, &lv);
    assert(!s.dead && s.grounded && s.y == CEIL_Q - GD_PX(GD_HALF_PX));   // 站在天花板上
    // 倒立时按下:朝地面方向起跳
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    assert(s.vy < 0);
    while (s.grav < 0 && s.tick < 5000) gd_sim_step(&s, &lv);
    for (int i = 0; i < 300; i++) gd_sim_step(&s, &lv);
    assert(!s.dead && s.grav == 1 && s.grounded && s.y == GD_PX(GD_HALF_PX));
}

static void test_blue_orb_and_pad(void) {
    gd_obj_t objs[] = {
        { .x = 10, .y = 0, .type = GD_OBJ_PAD_BLUE, .len = 1 },
        { .x = 40, .y = 8, .type = GD_OBJ_PAD_BLUE, .len = 1, .flags = GD_OBJF_CEIL },
    };
    const gd_level_t lv = make_level(objs, 2, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    while (s.grav > 0) gd_sim_step(&s, &lv);
    assert(s.vy > 0);
    while (s.grav < 0 && s.tick < 5000) gd_sim_step(&s, &lv);
    assert(s.grav == 1 && s.vy < 0 && !s.dead);

    gd_obj_t orb[] = { { .x = 10, .y = 0, .type = GD_OBJ_ORB_BLUE, .len = 1 } };
    const gd_level_t lv2 = make_level(orb, 1, 400);
    gd_sim_reset(&s);
    while (s.orb_prev != 0) gd_sim_step(&s, &lv2);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv2);
    assert(s.grav == -1 && s.vy > 0);
}

static void test_spike_down(void) {
    // 倒立重力下,天花板上的朝下尖刺(贴天花板的第 8 行)致死
    gd_obj_t objs[] = {
        { .x = 5, .y = 1, .type = GD_OBJ_PORTAL_FLIP, .len = 1 },
        { .x = 40, .y = 8, .type = GD_OBJ_SPIKE_DOWN, .len = 1 },
    };
    const gd_level_t lv = make_level(objs, 2, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    while (!s.dead && s.tick < 5000) gd_sim_step(&s, &lv);
    assert(s.dead);
    const int col = (s.death_x >> GD_FP_SHIFT) / GD_CELL_PX;
    assert(col >= 39 && col <= 40);
}

static void test_finish_and_progress(void) {
    const gd_level_t lv = make_level(NULL, 0, 50);
    gd_sim_t s;
    gd_sim_reset(&s);
    assert(gd_sim_progress(&s, &lv) == 0);
    uint32_t n = 0;
    while (!s.finished) {
        gd_sim_step(&s, &lv);
        n++;
    }
    assert(n == gd_level_finish_tick(&lv));
    assert(gd_sim_progress(&s, &lv) == 100);
    const gd_sim_t before = s;
    gd_sim_step(&s, &lv);   // 终点后不再变化
    assert(memcmp(&before, &s, sizeof s) == 0);
}

static void test_mode_portal_halves_speed(void) {
    gd_obj_t objs[] = { { .x = 6, .y = 1, .type = GD_OBJ_PORTAL_SHIP, .len = 1 } };
    const gd_level_t lv = make_level(objs, 1, 400);
    gd_sim_t s;
    gd_sim_reset(&s);
    gd_sim_set_held(&s, true);
    gd_sim_step(&s, &lv);
    gd_sim_set_held(&s, false);
    int32_t vy_before = 0;
    while (s.mode == GD_MODE_CUBE) {
        vy_before = s.vy;
        gd_sim_step(&s, &lv);
    }
    const int32_t expect = (vy_before - GD_GRAVITY) / 2;
    assert(s.vy >= expect - GD_SHIP_UP - 2 && s.vy <= expect + GD_SHIP_UP + 2);
}

static void test_level_index(void) {
    gd_obj_t objs[] = {
        { .x = 0, .y = 0, .type = GD_OBJ_BLOCK, .len = 16 },
        { .x = 20, .y = 0, .type = GD_OBJ_SPIKE_UP, .len = 1 },
        { .x = 40, .y = 0, .type = GD_OBJ_SPIKE_UP, .len = 1 },
    };
    const gd_level_t lv = make_level(objs, 3, 100);
    assert(gd_level_first_from(&lv, 0) == 0);
    assert(gd_level_first_from(&lv, 15) == 0);
    assert(gd_level_first_from(&lv, 16) == 1);
    assert(gd_level_first_from(&lv, 35) == 1);
    assert(gd_level_first_from(&lv, 36) == 2);
    assert(gd_level_first_from(&lv, 56) == 3);
    assert(gd_ms_tick(1000) == 240 && gd_tick_ms(240) == 1000 && gd_ms_tick(-5) == 0);
}

int main(void) {
    test_jump_shape();
    test_spike_invariant();
    test_platform_invariant();
    test_wall_kills();
    test_hold_to_jump();
    test_tap_while_airborne_does_nothing();
    test_ship();
    test_ship_speed_limit();
    test_ship_slides_on_block();
    test_yellow_pad();
    test_orbs();
    test_orb_once_and_reset();
    test_gravity_portal();
    test_blue_orb_and_pad();
    test_spike_down();
    test_finish_and_progress();
    test_mode_portal_halves_speed();
    test_level_index();
    printf("Geometry Sprint physics tests: PASS\n");
    return 0;
}
