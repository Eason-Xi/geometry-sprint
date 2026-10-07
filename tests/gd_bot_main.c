// tests/gd_bot_main.c —— 求解全部关卡,把通关录像输出为 C 源码(由 tools/gen_gd_levels.py 编译运行)。
//
// 标准输出:main/gd_replays_data.c 的全文;标准错误:每关的搜索节点、按键数与容错窗口。
// 任何一关无解时返回非零。
#include <stdio.h>
#include <stdlib.h>

#include "gd_solver.h"

int main(void) {
    gd_solution_t sol[GD_LEVEL_COUNT];
    int failed = 0;
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const gd_level_t *lv = &GD_LEVELS[i];
        if (!gd_solve(lv, &sol[i], 200000000L)) {
            fprintf(stderr, "level %d (%s): NO SOLUTION after %ld nodes\n", i + 1, lv->name, sol[i].nodes);
            failed = 1;
            continue;
        }
        const int moved = gd_center_edges(lv, sol[i].edges, sol[i].count);
        const gd_window_t w = gd_measure_windows(lv, sol[i].edges, sol[i].count, NULL);
        fprintf(stderr,
                "level %d (%s): solved, %ld nodes, %d edges (%d centered), finish tick %u; window min %d ticks (%d ms) "
                "at tick %u, median %d ticks over %d presses\n",
                i + 1, lv->name, sol[i].nodes, sol[i].count, moved, sol[i].finish_tick, w.min_ticks,
                w.min_ticks * 1000 / GD_TICK_HZ, w.tick, w.median_ticks, w.measured);
    }
    if (failed) return 1;
    printf("// main/gd_replays_data.c —— 由 tools/gen_gd_levels.py 生成,请勿手改。\n");
    printf("// 每关一条通关录像(主机求解器 tests/gd_solver.c 的结果)。\n");
    printf("#include \"gd_replay.h\"\n\n");
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        printf("static const gd_edge_t R%d[] = {", i + 1);
        if (sol[i].count == 0) printf("\n    { 0, 0 },");   // C 不允许空数组初始化
        for (int k = 0; k < sol[i].count; k++) {
            if (k % 8 == 0) printf("\n   ");
            printf(" { %u, %u },", sol[i].edges[k].tick, sol[i].edges[k].down);
        }
        printf("\n};\n\n");
    }
    printf("const gd_replay_t GD_REPLAYS[GD_LEVEL_COUNT] = {\n");
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        printf("    { R%d, %d, %u },\n", i + 1, sol[i].count, sol[i].finish_tick);
        free(sol[i].edges);
    }
    printf("};\n");
    return 0;
}
