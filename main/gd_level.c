// main/gd_level.c —— 关卡查询辅助,说明见 gd_level.h。
#include "gd_level.h"

uint16_t gd_level_first_from(const gd_level_t *lv, int32_t col) {
    // 对象最长 GD_MAX_RUN 格:起始列 ≥ col − (GD_MAX_RUN − 1) 的对象才可能覆盖 col。
    const int32_t from = col - (GD_MAX_RUN - 1);
    uint16_t lo = 0, hi = lv->obj_count;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi) / 2);
        if ((int32_t)lv->objs[mid].x < from) lo = (uint16_t)(mid + 1);
        else hi = mid;
    }
    return lo;
}

uint32_t gd_level_finish_tick(const gd_level_t *lv) {
    const int32_t dist = gd_finish_x(lv) - gd_tick_x(0);
    if (dist <= 0) return 0;
    return (uint32_t)((dist + GD_SPEED - 1) / GD_SPEED);
}
