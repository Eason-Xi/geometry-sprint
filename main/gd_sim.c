// main/gd_sim.c —— 物理模拟,说明见 gd_sim.h 与 PRD §7。
#include "gd_sim.h"

#include <string.h>

#define HALF       GD_PX(GD_HALF_PX)
#define CORE_HALF  GD_PX(GD_CORE_HALF_PX)
#define LAND_TOL   GD_PX(GD_LAND_TOL_PX)
#define CEIL       GD_PX(GD_CEIL_PX)
#define ROT_STEP   14                     // 空中旋转:约 0.875°/步,一次平地跳约转 90°
#define ROT_FULL   (360 * 16)
#define ROT_QUART  (90 * 16)

typedef struct {
    int32_t x1, y1, x2, y2;   // Q12,半开区间 [x1, x2) × [y1, y2)
} box_t;

static bool overlap(const box_t *a, const box_t *b) {
    return a->x1 < b->x2 && b->x1 < a->x2 && a->y1 < b->y2 && b->y1 < a->y2;
}

static box_t cell_box(int32_t col, int32_t row, int32_t dx1, int32_t dy1, int32_t dx2, int32_t dy2) {
    const int32_t x = GD_PX(col * GD_CELL_PX), y = GD_PX(row * GD_CELL_PX);
    return (box_t){ x + GD_PX(dx1), y + GD_PX(dy1), x + GD_PX(dx2), y + GD_PX(dy2) };
}

static box_t player_box(const gd_sim_t *s, int32_t half) {
    return (box_t){ s->x - half, s->y - half, s->x + half, s->y + half };
}

// 跳环(圆)与玩家外框是否重叠。
static bool orb_touch(const gd_sim_t *s, int32_t col, int32_t row) {
    const int64_t cx = GD_PX(col * GD_CELL_PX + GD_CELL_PX / 2);
    const int64_t cy = GD_PX(row * GD_CELL_PX + GD_CELL_PX / 2);
    int64_t nx = cx < s->x - HALF ? s->x - HALF : (cx > s->x + HALF ? s->x + HALF : cx);
    int64_t ny = cy < s->y - HALF ? s->y - HALF : (cy > s->y + HALF ? s->y + HALF : cy);
    const int64_t dx = (nx - cx) >> 4, dy = (ny - cy) >> 4;   // 降到 Q8 防溢出
    const int64_t r = GD_PX(GD_ORB_R_PX) >> 4;
    return dx * dx + dy * dy <= r * r;
}

static bool used(const gd_sim_t *s, int idx) {
    return (s->used[idx >> 5] >> (idx & 31)) & 1u;
}

static void mark_used(gd_sim_t *s, int idx) {
    s->used[idx >> 5] |= 1u << (idx & 31);
}

void gd_sim_reset(gd_sim_t *s) {
    memset(s, 0, sizeof *s);
    s->x = gd_tick_x(0);
    s->y = HALF;
    s->grav = 1;
    s->mode = GD_MODE_CUBE;
    s->grounded = true;
    s->orb_prev = s->pad_prev = s->portal_prev = -1;
}

void gd_sim_clear_used(gd_sim_t *s) {
    memset(s->used, 0, sizeof s->used);
    s->orb_prev = s->pad_prev = s->portal_prev = -1;
}

void gd_sim_set_held(gd_sim_t *s, bool down) {
    if (down && !s->held) {
        s->press = true;
        s->press_tick = s->tick;
    }
    s->held = down;
}

int gd_sim_progress(const gd_sim_t *s, const gd_level_t *lv) {
    const int64_t span = (int64_t)gd_finish_x(lv) - GD_PX(GD_START_X_PX);
    if (span <= 0) return 100;
    const int64_t p = ((int64_t)s->x - GD_PX(GD_START_X_PX)) * 100 / span;
    return p < 0 ? 0 : (p > 100 ? 100 : (int)p);
}

static void die(gd_sim_t *s) {
    s->dead = true;
    s->death_x = s->x;
    s->death_y = s->y;
}

// 实心方块:内核碰到即死亡;朝"脚下"方向运动时在容差内吸附到表面。
// 返回 false 表示已死亡。
static bool collide_solid(gd_sim_t *s, const box_t *blk, bool *grounded) {
    const box_t outer = player_box(s, HALF);
    if (!overlap(&outer, blk)) return true;
    const box_t core = player_box(s, CORE_HALF);
    if (overlap(&core, blk)) {
        die(s);
        return false;
    }
    const int g = s->grav;
    const bool ship = s->mode == GD_MODE_SHIP;
    const int32_t bottom = s->y - HALF, top = s->y + HALF;
    // 向下运动(或静止)并落在方块顶面附近。正常重力 = 着陆;飞船倒立时只是贴着滑行。
    if (s->vy <= 0 && bottom >= blk->y2 - LAND_TOL && (g > 0 || ship)) {
        s->y = blk->y2 + HALF;
        s->vy = 0;
        if (g > 0) *grounded = true;
        return true;
    }
    // 向上运动(或静止)并顶到方块底面附近。倒立重力 = 着陆;飞船正常重力时贴着滑行。
    if (s->vy >= 0 && top <= blk->y1 + LAND_TOL && (g < 0 || ship)) {
        s->y = blk->y1 - HALF;
        s->vy = 0;
        if (g < 0) *grounded = true;
        return true;
    }
    return true;   // 侧面擦碰:外框重叠但内核未碰,继续前进(下一步可能撞上)
}

static void flip_gravity(gd_sim_t *s, int new_grav) {
    if (s->grav == new_grav) return;
    const int old = s->grav;
    s->grav = (int8_t)new_grav;
    s->vy = old * GD_BLUE_V;   // 朝新的"下方"(即旧的上方)运动
    s->grounded = false;
}

void gd_sim_step(gd_sim_t *s, const gd_level_t *lv) {
    if (s->dead || s->finished) return;
    bool press = s->press;
    s->press = false;
    const int g = s->grav;

    // 1. 竖直速度
    if (s->mode == GD_MODE_SHIP) {
        s->vy += g * (s->held ? GD_SHIP_UP : -GD_SHIP_DOWN);
        if (s->vy > GD_SHIP_VMAX) s->vy = GD_SHIP_VMAX;
        if (s->vy < -GD_SHIP_VMAX) s->vy = -GD_SHIP_VMAX;
    } else {
        s->vy -= g * GD_GRAVITY;
        if (s->vy < -GD_MAX_FALL) s->vy = -GD_MAX_FALL;
        if (s->vy > GD_MAX_FALL) s->vy = GD_MAX_FALL;
    }

    // 2. 位移
    s->x += GD_SPEED;
    s->y += s->vy;

    // 3. 地面与天花板:始终是实心表面(撞上不死,只停止该方向的速度)
    bool grounded = false;
    if (s->y - HALF < 0) {
        s->y = HALF;
        if (s->vy < 0) s->vy = 0;
        if (g > 0) grounded = true;
    }
    if (s->y + HALF > CEIL) {
        s->y = CEIL - HALF;
        if (s->vy > 0) s->vy = 0;
        if (g < 0) grounded = true;
    }

    // 4. 附近对象
    const int32_t left_col = ((s->x - HALF) >> GD_FP_SHIFT) / GD_CELL_PX - 1;
    const int32_t right_px = (s->x + HALF) >> GD_FP_SHIFT;
    int16_t orb_now = -1, pad_now = -1, portal_now = -1;
    for (uint16_t i = gd_level_first_from(lv, left_col); i < lv->obj_count; i++) {
        const gd_obj_t *o = &lv->objs[i];
        if ((int32_t)o->x * GD_CELL_PX > right_px + GD_CELL_PX) break;
        switch (o->type) {
        case GD_OBJ_BLOCK: {
            const box_t blk = cell_box(o->x, o->y, 0, 0, o->len * GD_CELL_PX, GD_CELL_PX);
            if (!collide_solid(s, &blk, &grounded)) return;
            break;
        }
        case GD_OBJ_SPIKE_UP:
        case GD_OBJ_SPIKE_DOWN: {
            const box_t outer = player_box(s, HALF);
            const int dy1 = o->type == GD_OBJ_SPIKE_UP ? 2 : 10;
            for (int k = 0; k < o->len; k++) {
                const box_t hit = cell_box(o->x + k, o->y, 8, dy1, 12, dy1 + 8);
                if (overlap(&outer, &hit)) {
                    die(s);
                    return;
                }
            }
            break;
        }
        case GD_OBJ_PAD_YELLOW:
        case GD_OBJ_PAD_BLUE: {
            const bool ceil = o->flags & GD_OBJF_CEIL;
            const box_t hit = ceil ? cell_box(o->x, o->y, 2, 16, 18, 20) : cell_box(o->x, o->y, 2, 0, 18, 4);
            const box_t outer = player_box(s, HALF);
            if (!overlap(&outer, &hit)) break;
            pad_now = (int16_t)i;
            if (s->pad_prev == (int16_t)i) break;
            if (o->type == GD_OBJ_PAD_YELLOW) {
                s->vy = (ceil ? -1 : 1) * GD_PAD_YELLOW_V;
            } else {
                flip_gravity(s, ceil ? 1 : -1);
            }
            grounded = false;
            break;
        }
        case GD_OBJ_ORB_YELLOW:
        case GD_OBJ_ORB_BLUE: {
            if (!orb_touch(s, o->x, o->y)) break;
            const bool entering = s->orb_prev != (int16_t)i;
            orb_now = (int16_t)i;
            if (used(s, i)) break;
            const bool buffered = s->held && entering && s->tick - s->press_tick <= GD_ORB_BUFFER;
            if (!press && !buffered) break;
            mark_used(s, i);
            press = false;   // 这次按下已被跳环消费,不再触发地面起跳
            if (o->type == GD_OBJ_ORB_YELLOW) {
                s->vy = s->grav * GD_ORB_YELLOW_V;
            } else {
                flip_gravity(s, -s->grav);
            }
            grounded = false;
            s->jumps++;
            break;
        }
        case GD_OBJ_PORTAL_FLIP:
        case GD_OBJ_PORTAL_NORMAL:
        case GD_OBJ_PORTAL_SHIP:
        case GD_OBJ_PORTAL_CUBE: {
            const box_t hit = cell_box(o->x, o->y, 5, -20, 15, 40);
            const box_t outer = player_box(s, HALF);
            if (!overlap(&outer, &hit)) break;
            portal_now = (int16_t)i;
            if (s->portal_prev == (int16_t)i) break;
            if (o->type == GD_OBJ_PORTAL_FLIP) {
                flip_gravity(s, -1);
                grounded = false;
            } else if (o->type == GD_OBJ_PORTAL_NORMAL) {
                if (s->grav != 1) grounded = false;
                flip_gravity(s, 1);
            } else {
                const uint8_t mode = o->type == GD_OBJ_PORTAL_SHIP ? GD_MODE_SHIP : GD_MODE_CUBE;
                if (s->mode != mode) {
                    s->mode = mode;
                    s->vy /= 2;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    s->orb_prev = orb_now;
    s->pad_prev = pad_now;
    s->portal_prev = portal_now;

    // 5. 方块起跳:着地时按下,或按住时每次着地立即再跳
    if (s->mode == GD_MODE_CUBE && grounded && (press || s->held)) {
        s->vy = s->grav * GD_JUMP_V;
        grounded = false;
        s->jumps++;
    }
    s->grounded = grounded;
    s->grounded_ticks = grounded ? (uint16_t)(s->grounded_ticks < 60000 ? s->grounded_ticks + 1 : 60000) : 0;

    // 6. 视觉旋转:空中匀速转,着地吸附到最近的 90°
    if (s->mode == GD_MODE_CUBE) {
        if (grounded) {
            s->rot = (int16_t)(((s->rot + ROT_QUART / 2) / ROT_QUART) * ROT_QUART % ROT_FULL);
        } else {
            s->rot = (int16_t)((s->rot + (s->grav > 0 ? ROT_STEP : ROT_FULL - ROT_STEP)) % ROT_FULL);
        }
    } else {
        s->rot = 0;
    }

    s->tick++;
    if (s->x >= gd_finish_x(lv)) s->finished = true;
}
