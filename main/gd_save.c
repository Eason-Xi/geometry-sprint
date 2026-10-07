// main/gd_save.c —— 存档格式,说明见 gd_save.h。
#include "gd_save.h"

#include <string.h>

#include "gd_mpack.h"   // gd_crc32

void gd_save_defaults(gd_save_t *s) {
    memset(s, 0, sizeof *s);
    s->set.volume = GD_VOLUME_DEFAULT;
    s->set.sfx = true;
    s->set.progress_bar = true;
    s->set.brightness = GD_BRIGHT_LEVELS - 1;
    s->set.offset_ms = 0;
}

void gd_save_clear_progress(gd_save_t *s) {
    const gd_settings_t keep = s->set;
    gd_save_defaults(s);
    s->set = keep;
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

size_t gd_save_encode(const gd_save_t *s, uint8_t *buf) {
    uint8_t *p = buf;
    *p++ = GD_SAVE_VERSION;
    *p++ = GD_LEVEL_COUNT;
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        const gd_level_rec_t *r = &s->lv[i];
        *p++ = r->best;
        *p++ = r->best_practice;
        *p++ = (uint8_t)((r->completed ? 1 : 0) | (r->practice_done ? 2 : 0));
        *p++ = 0;
        put32(p, r->attempts);
        p += 4;
        put32(p, r->jumps);
        p += 4;
    }
    put32(p, s->play_seconds);
    p += 4;
    *p++ = s->set.volume;
    *p++ = s->set.sfx ? 1 : 0;
    *p++ = s->set.progress_bar ? 1 : 0;
    *p++ = s->set.brightness;
    *p++ = (uint8_t)(s->set.offset_ms & 0xFF);
    *p++ = (uint8_t)((uint16_t)s->set.offset_ms >> 8);
    *p++ = s->hint_shown ? 1 : 0;
    put32(p, gd_crc32(0, buf, (size_t)(p - buf)));
    p += 4;
    return (size_t)(p - buf);
}

static uint8_t clamp_u8(uint8_t v, uint8_t hi) {
    return v > hi ? hi : v;
}

bool gd_save_decode(const uint8_t *buf, size_t len, gd_save_t *out) {
    gd_save_defaults(out);
    if (!buf || len != GD_SAVE_BYTES || buf[0] != GD_SAVE_VERSION || buf[1] != GD_LEVEL_COUNT) return false;
    if (gd_crc32(0, buf, len - 4) != get32(buf + len - 4)) return false;
    const uint8_t *p = buf + 2;
    for (int i = 0; i < GD_LEVEL_COUNT; i++) {
        gd_level_rec_t *r = &out->lv[i];
        r->best = clamp_u8(p[0], 100);
        r->best_practice = clamp_u8(p[1], 100);
        r->completed = p[2] & 1;
        r->practice_done = (p[2] & 2) != 0;
        r->attempts = get32(p + 4);
        r->jumps = get32(p + 8);
        if (r->completed) r->best = 100;
        if (r->practice_done) r->best_practice = 100;
        p += 12;
    }
    out->play_seconds = get32(p);
    p += 4;
    out->set.volume = clamp_u8(p[0], GD_VOLUME_MAX);
    out->set.sfx = p[1] != 0;
    out->set.progress_bar = p[2] != 0;
    out->set.brightness = clamp_u8(p[3], GD_BRIGHT_LEVELS - 1);
    int off = (int16_t)(uint16_t)(p[4] | p[5] << 8);
    if (off > GD_OFFSET_LIMIT) off = GD_OFFSET_LIMIT;
    if (off < -GD_OFFSET_LIMIT) off = -GD_OFFSET_LIMIT;
    out->set.offset_ms = (int16_t)(off / GD_OFFSET_STEP * GD_OFFSET_STEP);
    out->hint_shown = p[6] != 0;
    return true;
}

uint8_t gd_save_backlight(uint8_t brightness) {
    static const uint8_t PCT[GD_BRIGHT_LEVELS] = { 35, 65, 100 };
    return PCT[brightness < GD_BRIGHT_LEVELS ? brightness : GD_BRIGHT_LEVELS - 1];
}
