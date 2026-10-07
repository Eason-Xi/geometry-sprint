// tests/gd_solver.c —— 关卡求解与容错窗口,说明见 gd_solver.h。
#include "gd_solver.h"

#include <stdlib.h>
#include <string.h>

#define DECIDE      4               // 每 4 步决策一次(60 Hz)
#define HASH_BITS   22
#define SHIFT_MAX   60              // 窗口测量:按下沿最多平移 ±250 ms
#define HORIZON     120             // 平移后最多再观察 0.5 s
#define COUPLE      120             // 0.5 s 内、仍在空中的下一次按键视为联动(跳环)

// —— 已访问状态集合(开放寻址) ——
typedef struct {
    uint64_t *slot;
    size_t mask, used;
} vset_t;

static bool vset_init(vset_t *v) {
    v->mask = ((size_t)1 << HASH_BITS) - 1;
    v->used = 0;
    v->slot = calloc(v->mask + 1, sizeof(uint64_t));
    return v->slot != NULL;
}

// 插入成功返回 true;已存在或表满返回 false。
static bool vset_insert(vset_t *v, uint64_t key) {
    if (key == 0) key = 1;
    if (v->used * 10 > (v->mask + 1) * 7) return false;
    size_t i = (size_t)(key ^ (key >> 29)) & v->mask;
    while (v->slot[i]) {
        if (v->slot[i] == key) return false;
        i = (i + 1) & v->mask;
    }
    v->slot[i] = key;
    v->used++;
    return true;
}

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xBF58476D1CE4E5B9ull;
}

// 只取影响后续物理的字段(x 由 tick 决定;旋转、跳跃计数只影响显示)。
static uint64_t state_key(const gd_sim_t *s) {
    uint64_t h = 0xCBF29CE484222325ull;
    h = mix(h, s->tick);
    h = mix(h, (uint32_t)s->y);
    h = mix(h, (uint32_t)s->vy);
    h = mix(h, (uint64_t)(uint8_t)s->grav | (uint64_t)s->mode << 8 | (uint64_t)s->grounded << 16 |
                   (uint64_t)s->held << 17);
    h = mix(h, (uint64_t)(uint16_t)s->orb_prev | (uint64_t)(uint16_t)s->pad_prev << 16 |
                   (uint64_t)(uint16_t)s->portal_prev << 32);
    const uint32_t since = s->tick - s->press_tick;
    h = mix(h, since > GD_ORB_BUFFER + 1 ? GD_ORB_BUFFER + 1 : since);
    for (size_t i = 0; i < sizeof s->used / sizeof s->used[0]; i++) {
        if (s->used[i]) h = mix(h, (uint64_t)i << 32 | s->used[i]);
    }
    return h;
}

typedef struct {
    gd_sim_t sim;
    uint8_t next;     // 下一个要尝试的动作序号(0 = 松开,1 = 按住,2 = 已试完)
    uint8_t action;   // 到达本帧所用的动作
} frame_t;

bool gd_solve(const gd_level_t *lv, gd_solution_t *out, long max_nodes) {
    memset(out, 0, sizeof *out);
    const uint32_t finish = gd_level_finish_tick(lv);
    const size_t depth = finish / DECIDE + 4;
    frame_t *frames = malloc(sizeof(frame_t) * depth);
    vset_t seen;
    if (!frames || !vset_init(&seen)) {
        free(frames);
        return false;
    }
    gd_sim_reset(&frames[0].sim);
    frames[0].next = 0;
    frames[0].action = 0;
    long sp = 0;
    bool ok = false;
    uint8_t last_action = 0;
    while (sp >= 0 && out->nodes < max_nodes) {
        frame_t *f = &frames[sp];
        if (f->next >= 2) {
            sp--;
            continue;
        }
        const uint8_t a = f->next++;
        gd_sim_t s = f->sim;
        gd_sim_set_held(&s, a);
        for (int k = 0; k < DECIDE && !s.dead && !s.finished; k++) gd_sim_step(&s, lv);
        out->nodes++;
        if (s.dead) continue;
        if (s.finished) {
            ok = true;
            last_action = a;
            out->finish_tick = s.tick;
            break;
        }
        if (!vset_insert(&seen, state_key(&s))) continue;
        if ((size_t)sp + 1 >= depth) continue;
        sp++;
        frames[sp].sim = s;
        frames[sp].next = 0;
        frames[sp].action = a;
    }
    if (ok) {
        // 第 i 次决策(i = 0..sp)在第 i*DECIDE 步;frames[i+1].action 是第 i 次的动作。
        out->edges = malloc(sizeof(gd_edge_t) * (size_t)(sp + 2));
        uint8_t prev = 0;
        for (long i = 0; i <= sp; i++) {
            const uint8_t a = i < sp ? frames[i + 1].action : last_action;
            if (a != prev) {
                out->edges[out->count++] = (gd_edge_t){ .tick = (uint32_t)i * DECIDE, .down = a };
                prev = a;
            }
        }
    }
    free(seen.slot);
    free(frames);
    return ok;
}

bool gd_replay(const gd_level_t *lv, const gd_edge_t *edges, int count, uint32_t max_tick, gd_sim_t *final) {
    gd_sim_t s;
    gd_sim_reset(&s);
    int i = 0;
    while (!s.dead && !s.finished && s.tick < max_tick) {
        while (i < count && edges[i].tick <= s.tick) gd_sim_set_held(&s, edges[i++].down);
        gd_sim_step(&s, lv);
    }
    if (final) *final = s;
    return s.finished;
}

static bool same_physics(const gd_sim_t *a, const gd_sim_t *b) {
    return a->y == b->y && a->vy == b->vy && a->grav == b->grav && a->mode == b->mode &&
           a->grounded == b->grounded && a->orb_prev == b->orb_prev && a->pad_prev == b->pad_prev &&
           a->portal_prev == b->portal_prev && memcmp(a->used, b->used, sizeof a->used) == 0;
}

// 回放并记录每一步之前的状态(快照在应用该步输入之前),返回最后一个有效步。
static uint32_t build_ref(const gd_level_t *lv, const gd_edge_t *edges, int count, gd_sim_t *ref, uint32_t finish) {
    gd_sim_t s;
    gd_sim_reset(&s);
    int ei = 0;
    while (!s.dead && !s.finished && s.tick < finish) {
        ref[s.tick] = s;
        while (ei < count && edges[ei].tick <= s.tick) gd_sim_set_held(&s, edges[ei++].down);
        gd_sim_step(&s, lv);
    }
    ref[s.tick] = s;
    return s.tick;
}

// 联动组:按下与其松开,加上同一次腾空中紧接着的跳环按键(及其松开)。返回组内最后一个沿的下标。
static int group_end(const gd_sim_t *ref, const gd_edge_t *edges, int count, int e, uint32_t last) {
    int ge = e;
    if (ge + 1 < count && !edges[ge + 1].down) ge++;
    uint32_t prev_press = edges[e].tick;
    while (ge + 1 < count && edges[ge + 1].down && edges[ge + 1].tick <= last &&
           edges[ge + 1].tick - prev_press <= COUPLE && !ref[edges[ge + 1].tick].grounded) {
        ge++;
        prev_press = edges[ge].tick;
        if (ge + 1 < count && !edges[ge + 1].down) ge++;
    }
    return ge;
}

// 把组 [e, ge] 平移 d 步(可选再把组 [e2, ge2] 平移 d2 步),从参考轨迹出发模拟,
// 在最后一个被平移的沿之后与参考轨迹完全会合(或通关)即视为成功。
static bool shift_ok(const gd_level_t *lv, const gd_sim_t *ref, const gd_edge_t *edges, int count, gd_edge_t *mod,
                     int e, int ge, int d, int e2, int ge2, int d2, uint32_t last) {
    memcpy(mod, edges, sizeof(gd_edge_t) * (size_t)count);
    for (int k = e; k <= ge; k++) mod[k].tick = (uint32_t)((int64_t)mod[k].tick + d);
    for (int k = e2; e2 >= 0 && k <= ge2; k++) mod[k].tick = (uint32_t)((int64_t)mod[k].tick + d2);
    // 平移后仍须保持时间顺序、不早于 0
    if ((int64_t)edges[e].tick + d < 0) return false;
    for (int k = 1; k < count; k++) {
        if (mod[k].tick <= mod[k - 1].tick) return false;
    }
    const int lastk = e2 >= 0 ? ge2 : ge;
    uint32_t from = mod[e].tick < edges[e].tick ? mod[e].tick : edges[e].tick;
    uint32_t conv_after = 0;
    for (int k = e; k <= lastk; k++) {
        if (mod[k].tick > conv_after) conv_after = mod[k].tick;
        if (edges[k].tick > conv_after) conv_after = edges[k].tick;
    }
    if (from > last) return false;
    const uint32_t horizon_end = (lastk + 1 < count ? edges[lastk + 1].tick : conv_after) + HORIZON;
    gd_sim_t t = ref[from];
    int k = e;
    while (!t.dead && !t.finished && t.tick < horizon_end && t.tick <= last) {
        while (k < count && mod[k].tick <= t.tick) gd_sim_set_held(&t, mod[k++].down);
        gd_sim_step(&t, lv);
        if (t.tick > conv_after && t.tick <= last && same_physics(&t, &ref[t.tick])) return true;
    }
    return t.finished && !t.dead;
}

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

gd_window_t gd_measure_windows(const gd_level_t *lv, const gd_edge_t *edges, int count, int *per_edge) {
    gd_window_t w = { .edge = -1, .min_ticks = 1 << 30 };
    // 参考轨迹:每一步之前的状态
    const uint32_t finish = gd_level_finish_tick(lv) + 2;
    gd_sim_t *ref = malloc(sizeof(gd_sim_t) * (finish + 1));
    int *wins = malloc(sizeof(int) * (size_t)(count + 1));
    if (!ref || !wins) {
        free(ref);
        free(wins);
        return w;
    }
    const uint32_t last = build_ref(lv, edges, count, ref, finish);

    gd_edge_t *mod = malloc(sizeof(gd_edge_t) * (size_t)(count + 1));
    for (int e = 0; mod && e < count; e++) {
        if (per_edge) per_edge[e] = -1;
        if (!edges[e].down || edges[e].tick > last) continue;
        if (ref[edges[e].tick].mode != GD_MODE_CUBE) continue;
        const int ge = group_end(ref, edges, count, e, last);
        const int e2 = ge + 1 < count && edges[ge + 1].down ? ge + 1 : (ge + 2 < count ? ge + 2 : -1);
        const int ge2 = e2 >= 0 && edges[e2].down ? group_end(ref, edges, count, e2, last) : -1;
        int ok_lo = 0, ok_hi = 0;
        for (int dir = -1; dir <= 1; dir += 2) {
            for (int sh = 1; sh <= SHIFT_MAX; sh++) {
                const int d = dir * sh;
                if (!shift_ok(lv, ref, edges, count, mod, e, ge, d, -1, -1, 0, last)) {
                    // 玩家会跟着调整下一次按键:在范围内为下一组找一个能会合的时机
                    bool rescued = false;
                    for (int s2 = 1; ge2 >= 0 && !rescued && s2 <= 2 * SHIFT_MAX; s2++) {
                        const int d2 = (s2 & 1) ? (s2 + 1) / 2 : -(s2 / 2);
                        rescued = shift_ok(lv, ref, edges, count, mod, e, ge, d, e2, ge2, d2, last);
                    }
                    if (!rescued) break;
                }
                if (dir < 0) ok_lo = sh;
                else ok_hi = sh;
            }
        }
        const int win = ok_lo + ok_hi + 1;
        if (per_edge) per_edge[e] = win;
        wins[w.measured++] = win;
        if (win < w.min_ticks) {
            w.min_ticks = win;
            w.edge = e;
            w.tick = edges[e].tick;
        }
    }
    free(mod);
    if (w.measured) {
        qsort(wins, (size_t)w.measured, sizeof(int), cmp_int);
        w.median_ticks = wins[w.measured / 2];
    } else {
        w.min_ticks = 0;
    }
    free(ref);
    free(wins);
    return w;
}

// 测量组 [e, ge] 的可行平移范围(允许下一组重新找时机);*rescue_d2 返回中点平移所需的下一组平移。
static void shift_range(const gd_level_t *lv, const gd_sim_t *ref, const gd_edge_t *edges, int count, gd_edge_t *mod,
                        int e, int ge, int e2, int ge2, uint32_t last, int *lo, int *hi) {
    *lo = 0;
    *hi = 0;
    for (int dir = -1; dir <= 1; dir += 2) {
        for (int sh = 1; sh <= SHIFT_MAX; sh++) {
            const int d = dir * sh;
            bool ok = shift_ok(lv, ref, edges, count, mod, e, ge, d, -1, -1, 0, last);
            for (int s2 = 1; !ok && ge2 >= 0 && s2 <= 2 * SHIFT_MAX; s2++) {
                const int d2 = (s2 & 1) ? (s2 + 1) / 2 : -(s2 / 2);
                ok = shift_ok(lv, ref, edges, count, mod, e, ge, d, e2, ge2, d2, last);
            }
            if (!ok) break;
            if (dir < 0) *lo = sh;
            else *hi = sh;
        }
    }
}

int gd_center_edges(const gd_level_t *lv, gd_edge_t *edges, int count) {
    const uint32_t finish = gd_level_finish_tick(lv) + 2;
    gd_sim_t *ref = malloc(sizeof(gd_sim_t) * (finish + 1));
    gd_edge_t *mod = malloc(sizeof(gd_edge_t) * (size_t)(count + 1));
    gd_edge_t *backup = malloc(sizeof(gd_edge_t) * (size_t)(count + 1));
    int moved = 0;
    if (!ref || !mod || !backup) goto out;
    for (int e = 0; e < count; e++) {
        if (!edges[e].down) continue;
        const uint32_t last = build_ref(lv, edges, count, ref, finish);
        if (edges[e].tick > last || ref[edges[e].tick].mode != GD_MODE_CUBE) continue;
        const int ge = group_end(ref, edges, count, e, last);
        int lo, hi;
        shift_range(lv, ref, edges, count, mod, e, ge, -1, -1, last, &lo, &hi);
        const int d = (hi - lo) / 2;
        if (d == 0) continue;
        memcpy(backup, edges, sizeof(gd_edge_t) * (size_t)count);
        for (int k = e; k <= ge; k++) edges[k].tick = (uint32_t)((int64_t)edges[k].tick + d);
        if (gd_replay(lv, edges, count, finish, NULL)) {
            moved++;
        } else {
            memcpy(edges, backup, sizeof(gd_edge_t) * (size_t)count);
        }
    }
out:
    free(ref);
    free(mod);
    free(backup);
    return moved;
}
