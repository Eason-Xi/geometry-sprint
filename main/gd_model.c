// main/gd_model.c —— 页面导航与菜单状态机,说明见 gd_model.h。
#include "gd_model.h"

#include <string.h>

void gd_model_init(gd_model_t *m) {
    memset(m, 0, sizeof *m);
    m->screen = GD_SCR_TITLE;
    m->new_best = -1;
    gd_save_defaults(&m->save);
}

static uint32_t go(gd_model_t *m, gd_screen_t s, uint32_t snd) {
    m->screen = s;
    m->paused = false;
    m->set_editing = false;
    m->about_open = false;
    m->clear_holding = false;
    return GD_FX_SCREEN | snd;
}

static uint32_t start_level(gd_model_t *m) {
    m->practice = false;
    m->paused = false;
    m->pause_cursor = 0;
    m->new_best = -1;
    return go(m, GD_SCR_PLAY, GD_FX_SND_OK) | GD_FX_START;
}

static uint32_t key_title(gd_model_t *m, gd_key_t key, gd_kev_t ev) {
    if (key == GD_KEY_UP && ev == GD_KEV_PRESS) return go(m, GD_SCR_SETTINGS, GD_FX_SND_OK);
    if (key == GD_KEY_DOWN && ev == GD_KEV_PRESS) return go(m, GD_SCR_STATS, GD_FX_SND_OK);
    if (key == GD_KEY_OK && ev == GD_KEV_CLICK) return go(m, GD_SCR_SELECT, GD_FX_SND_OK);
    return 0;
}

static uint32_t key_select(gd_model_t *m, gd_key_t key, gd_kev_t ev) {
    if (key == GD_KEY_UP && ev == GD_KEV_PRESS) {
        if (m->level == 0) return 0;
        m->level--;
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key == GD_KEY_DOWN && ev == GD_KEV_PRESS) {
        if (m->level + 1 >= GD_LEVEL_COUNT) return 0;
        m->level++;
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key == GD_KEY_OK && ev == GD_KEV_CLICK) return start_level(m);
    if (key == GD_KEY_OK && ev == GD_KEV_LONG) return go(m, GD_SCR_TITLE, GD_FX_SND_BACK);
    return 0;
}

static uint32_t key_pause(gd_model_t *m, gd_key_t key, gd_kev_t ev) {
    if (key == GD_KEY_UP && ev == GD_KEV_PRESS) {
        m->pause_cursor = (uint8_t)((m->pause_cursor + GD_PAUSE_COUNT - 1) % GD_PAUSE_COUNT);
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key == GD_KEY_DOWN && ev == GD_KEV_PRESS) {
        m->pause_cursor = (uint8_t)((m->pause_cursor + 1) % GD_PAUSE_COUNT);
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key == GD_KEY_OK && ev == GD_KEV_LONG) {
        m->paused = false;
        return GD_FX_REFRESH | GD_FX_RESUME | GD_FX_SND_OK;
    }
    if (key != GD_KEY_OK || ev != GD_KEV_CLICK) return 0;
    switch (m->pause_cursor) {
    case GD_PAUSE_RESUME:
        m->paused = false;
        return GD_FX_REFRESH | GD_FX_RESUME | GD_FX_SND_OK;
    case GD_PAUSE_PRACTICE:
        m->practice = !m->practice;
        m->paused = false;
        return GD_FX_REFRESH | GD_FX_PRACTICE | GD_FX_SND_OK;
    case GD_PAUSE_RESTART:
        m->paused = false;
        return GD_FX_REFRESH | GD_FX_RESTART | GD_FX_SND_OK;
    default:
        return go(m, GD_SCR_SELECT, GD_FX_SND_BACK) | GD_FX_STOP | GD_FX_SAVE;
    }
}

static uint32_t key_result(gd_model_t *m, gd_key_t key, gd_kev_t ev) {
    if (key == GD_KEY_UP && ev == GD_KEV_PRESS) return start_level(m);
    if (key == GD_KEY_OK && (ev == GD_KEV_CLICK || ev == GD_KEV_LONG)) return go(m, GD_SCR_SELECT, GD_FX_SND_BACK);
    return 0;
}

static uint32_t adjust(gd_model_t *m, int dir) {
    gd_settings_t *s = &m->save.set;
    switch (m->set_cursor) {
    case GD_SET_VOLUME: {
        const int v = s->volume + dir;
        if (v < 0 || v > GD_VOLUME_MAX) return 0;
        s->volume = (uint8_t)v;
        return GD_FX_REFRESH | GD_FX_VOLUME;
    }
    case GD_SET_BRIGHT: {
        const int v = s->brightness + dir;
        if (v < 0 || v >= GD_BRIGHT_LEVELS) return 0;
        s->brightness = (uint8_t)v;
        return GD_FX_REFRESH | GD_FX_BRIGHT | GD_FX_SND_MOVE;
    }
    case GD_SET_OFFSET: {
        const int v = s->offset_ms + dir * GD_OFFSET_STEP;
        if (v < -GD_OFFSET_LIMIT || v > GD_OFFSET_LIMIT) return 0;
        s->offset_ms = (int16_t)v;
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    default:
        return 0;
    }
}

static uint32_t key_settings(gd_model_t *m, gd_key_t key, gd_kev_t ev, uint32_t now_ms) {
    if (m->about_open) {
        if ((key != GD_KEY_OK && ev == GD_KEV_PRESS) || (key == GD_KEY_OK && (ev == GD_KEV_CLICK || ev == GD_KEV_LONG))) {
            m->about_open = false;
            return GD_FX_REFRESH | GD_FX_SND_BACK;
        }
        return 0;
    }
    if (m->set_editing) {
        if (key == GD_KEY_UP && ev == GD_KEV_PRESS) return adjust(m, -1);
        if (key == GD_KEY_DOWN && ev == GD_KEV_PRESS) return adjust(m, 1);
        if (key == GD_KEY_OK && (ev == GD_KEV_CLICK || ev == GD_KEV_LONG)) {
            m->set_editing = false;
            return GD_FX_REFRESH | GD_FX_SAVE | GD_FX_SND_OK;
        }
        return 0;
    }
    if (key == GD_KEY_UP && ev == GD_KEV_PRESS) {
        m->set_cursor = (uint8_t)((m->set_cursor + GD_SET_COUNT - 1) % GD_SET_COUNT);
        m->clear_holding = false;
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key == GD_KEY_DOWN && ev == GD_KEV_PRESS) {
        m->set_cursor = (uint8_t)((m->set_cursor + 1) % GD_SET_COUNT);
        m->clear_holding = false;
        return GD_FX_REFRESH | GD_FX_SND_MOVE;
    }
    if (key != GD_KEY_OK) return 0;
    if (m->set_cursor == GD_SET_CLEAR) {
        // 清除存档:按住 1.5 秒(期间的 LONG 不当作返回)
        if (ev == GD_KEV_PRESS) {
            m->clear_holding = true;
            m->clear_hold_start = now_ms;
            return GD_FX_REFRESH;
        }
        if (ev == GD_KEV_RELEASE && m->clear_holding) {
            m->clear_holding = false;
            return GD_FX_REFRESH;
        }
        return 0;
    }
    if (ev == GD_KEV_LONG) return go(m, GD_SCR_TITLE, GD_FX_SND_BACK) | GD_FX_SAVE;
    if (ev != GD_KEV_CLICK) return 0;
    switch (m->set_cursor) {
    case GD_SET_SFX:
        m->save.set.sfx = !m->save.set.sfx;
        return GD_FX_REFRESH | GD_FX_SAVE | GD_FX_SND_OK;
    case GD_SET_BAR:
        m->save.set.progress_bar = !m->save.set.progress_bar;
        return GD_FX_REFRESH | GD_FX_SAVE | GD_FX_SND_OK;
    case GD_SET_ABOUT:
        m->about_open = true;
        return GD_FX_REFRESH | GD_FX_SND_OK;
    default:
        m->set_editing = true;
        return GD_FX_REFRESH | GD_FX_SND_OK;
    }
}

static uint32_t key_stats(gd_model_t *m, gd_key_t key, gd_kev_t ev) {
    if (key == GD_KEY_OK && (ev == GD_KEV_CLICK || ev == GD_KEV_LONG)) return go(m, GD_SCR_TITLE, GD_FX_SND_BACK);
    return 0;
}

uint32_t gd_model_key(gd_model_t *m, gd_key_t key, gd_kev_t ev, uint32_t now_ms) {
    switch (m->screen) {
    case GD_SCR_TITLE: return key_title(m, key, ev);
    case GD_SCR_SELECT: return key_select(m, key, ev);
    case GD_SCR_PLAY: return m->paused ? key_pause(m, key, ev) : 0;
    case GD_SCR_RESULT: return key_result(m, key, ev);
    case GD_SCR_SETTINGS: return key_settings(m, key, ev, now_ms);
    case GD_SCR_STATS: return key_stats(m, key, ev);
    }
    return 0;
}

uint32_t gd_model_pause(gd_model_t *m) {
    if (m->screen != GD_SCR_PLAY || m->paused) return 0;
    m->paused = true;
    m->pause_cursor = GD_PAUSE_RESUME;
    return GD_FX_REFRESH | GD_FX_PAUSE | GD_FX_SAVE;
}

void gd_model_attempt(gd_model_t *m) {
    m->save.lv[m->level].attempts++;
}

uint32_t gd_model_died(gd_model_t *m, int progress) {
    gd_level_rec_t *r = &m->save.lv[m->level];
    if (progress < 0) progress = 0;
    if (progress > 99) progress = 99;   // 死亡时进度不会是 100
    uint8_t *best = m->practice ? &r->best_practice : &r->best;
    if (progress > *best) {
        *best = (uint8_t)progress;
        m->new_best = (int8_t)progress;
        return GD_FX_SAVE | GD_FX_REFRESH;
    }
    m->new_best = -1;
    return 0;
}

uint32_t gd_model_finished(gd_model_t *m, uint32_t attempts, uint32_t jumps, uint32_t time_ms) {
    gd_level_rec_t *r = &m->save.lv[m->level];
    m->result = (gd_result_t){
        .attempts = attempts, .jumps = jumps, .time_ms = time_ms, .practice = m->practice,
        .first_clear = !m->practice && !r->completed,
    };
    if (m->practice) {
        r->practice_done = true;
        r->best_practice = 100;
    } else {
        r->completed = true;
        r->best = 100;
    }
    return GD_FX_SAVE;
}

uint32_t gd_model_show_result(gd_model_t *m) {
    return go(m, GD_SCR_RESULT, 0) | GD_FX_STOP | GD_FX_SAVE;
}

void gd_model_add_stats(gd_model_t *m, uint32_t jumps, uint32_t seconds) {
    m->save.lv[m->level].jumps += jumps;
    m->save.play_seconds += seconds;
}

uint32_t gd_model_tick(gd_model_t *m, uint32_t now_ms) {
    if (m->screen == GD_SCR_SETTINGS && m->clear_holding && now_ms - m->clear_hold_start >= GD_CLEAR_HOLD_MS) {
        m->clear_holding = false;
        gd_save_clear_progress(&m->save);
        m->cleared_at = now_ms ? now_ms : 1;
        return GD_FX_REFRESH | GD_FX_SAVE | GD_FX_SND_OK;
    }
    return 0;
}
