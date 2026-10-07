// tests/test_gd_levels.c —— 6 个正式关卡的回归测试(PRD §6.2、§12):
// 数据完整、录像能通关、求解器重新证明可通关、容错窗口达到难度下限。
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gd_solver.h"

// 难度对应的最小容错窗口(步):简单 50 ms、普通 33 ms、困难 20 ms。
static int min_window_ticks(uint8_t difficulty) {
    const int ms = difficulty == GD_DIFF_EASY ? 50 : (difficulty == GD_DIFF_NORMAL ? 33 : 20);
    return (ms * GD_TICK_HZ + 999) / 1000;
}

static void test_level_data(void) {
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const gd_level_t *lv = &GD_LEVELS[i];
        assert(lv->name && lv->name[0] && lv->artist && lv->artist[0]);
        assert(lv->track == i + 1);
        assert(lv->difficulty <= GD_DIFF_HARD);
        assert(lv->obj_count > 0 && lv->obj_count <= GD_MAX_OBJS);
        for (uint16_t k = 0; k < lv->obj_count; k++) {
            const gd_obj_t *o = &lv->objs[k];
            assert(o->type < GD_OBJ_COUNT && o->y < GD_ROWS && o->len >= 1 && o->len <= GD_MAX_RUN);
            assert(o->x < lv->finish_col);
            if (k) assert(o->x >= lv->objs[k - 1].x);
        }
        // 关卡时长 85–92 s:音乐包每首截取 95 s(末尾 3 s 淡出),通关后还要留出结算动画
        const int ms = gd_tick_ms(gd_level_finish_tick(lv));
        assert(ms >= 85000 && ms <= 92000);
        if (i) assert(lv->difficulty >= GD_LEVELS[i - 1].difficulty);   // 难度不倒退
    }
}

static void test_replays_finish(void) {
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const gd_replay_t *r = &GD_REPLAYS[i];
        gd_sim_t s;
        const bool ok = gd_replay(&GD_LEVELS[i], r->edges, r->count, gd_level_finish_tick(&GD_LEVELS[i]) + 4, &s);
        if (!ok) fprintf(stderr, "level %d replay died at tick %u\n", i + 1, s.tick);
        assert(ok);
        assert(s.tick == r->finish_tick);
        assert(s.tick == gd_level_finish_tick(&GD_LEVELS[i]));
        for (uint16_t k = 1; k < r->count; k++) assert(r->edges[k].tick > r->edges[k - 1].tick);
        // 关卡设计要求每个跳环都在必经路线上:通关录像必须触发过全部跳环(防止出现绕开的捷径)
        for (uint16_t k = 0; k < GD_LEVELS[i].obj_count; k++) {
            const uint8_t ty = GD_LEVELS[i].objs[k].type;
            if (ty != GD_OBJ_ORB_YELLOW && ty != GD_OBJ_ORB_BLUE) continue;
            if (!((s.used[k >> 5] >> (k & 31)) & 1u)) {
                fprintf(stderr, "level %d: orb at column %u is never used by the replay\n", i + 1,
                        GD_LEVELS[i].objs[k].x);
                assert(0);
            }
        }
    }
}

static void test_solver_and_windows(void) {
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const gd_level_t *lv = &GD_LEVELS[i];
        gd_solution_t sol;
        assert(gd_solve(lv, &sol, 50000000L));
        // 录像(已居中)的容错窗口
        const gd_replay_t *r = &GD_REPLAYS[i];
        gd_edge_t *edges = malloc(sizeof(gd_edge_t) * r->count);
        memcpy(edges, r->edges, sizeof(gd_edge_t) * r->count);
        const gd_window_t w = gd_measure_windows(lv, edges, r->count, NULL);
        printf("level %d %-16s solver %6ld nodes; replay %3u edges, window min %3d ms, median %3d ms (%d presses)\n",
               i + 1, lv->name, sol.nodes, r->count, w.min_ticks * 1000 / GD_TICK_HZ,
               w.median_ticks * 1000 / GD_TICK_HZ, w.measured);
        assert(w.measured > 20);
        assert(w.min_ticks >= min_window_ticks(lv->difficulty));
        free(edges);
        free(sol.edges);
    }
}

// 每关都至少用到 PRD §6.1 为它规定的新机制。
static bool has_type(const gd_level_t *lv, gd_obj_type_t t) {
    for (uint16_t k = 0; k < lv->obj_count; k++) {
        if (lv->objs[k].type == t) return true;
    }
    return false;
}

static void test_mechanics_per_level(void) {
    assert(has_type(&GD_LEVELS[0], GD_OBJ_PORTAL_SHIP) && has_type(&GD_LEVELS[0], GD_OBJ_BLOCK));
    assert(has_type(&GD_LEVELS[1], GD_OBJ_PAD_YELLOW));
    assert(has_type(&GD_LEVELS[2], GD_OBJ_ORB_YELLOW));
    assert(has_type(&GD_LEVELS[3], GD_OBJ_PORTAL_FLIP) && has_type(&GD_LEVELS[3], GD_OBJ_ORB_BLUE));
    assert(has_type(&GD_LEVELS[4], GD_OBJ_PAD_BLUE) && has_type(&GD_LEVELS[4], GD_OBJ_PORTAL_SHIP));
    assert(has_type(&GD_LEVELS[5], GD_OBJ_ORB_YELLOW) && has_type(&GD_LEVELS[5], GD_OBJ_PORTAL_FLIP));
}

int main(void) {
    test_level_data();
    test_replays_finish();
    test_mechanics_per_level();
    test_solver_and_windows();
    printf("Geometry Sprint level tests: PASS\n");
    return 0;
}
