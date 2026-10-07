// main/gd_sfx.c —— 音效合成器,说明见 gd_sfx.h。
#include "gd_sfx.h"

#include <stddef.h>
#include <string.h>

enum { W_SQUARE = 0, W_TRIANGLE, W_NOISE };

typedef struct {
    uint16_t hz_from, hz_to;   // 频率线性滑变
    uint16_t ms;
    uint8_t wave;
    uint8_t vol;               // 0..100
} seg_t;

typedef struct {
    const seg_t *seg;
    uint8_t n;
    uint8_t layer;             // 同时触发的叠加层(0 = 无)
} sfx_def_t;

static const seg_t S_MOVE[] = { { 1200, 1200, 22, W_SQUARE, 30 } };
static const seg_t S_OK[] = { { 880, 880, 40, W_SQUARE, 34 }, { 1320, 1320, 60, W_SQUARE, 34 } };
static const seg_t S_BACK[] = { { 1320, 1320, 35, W_SQUARE, 30 }, { 660, 660, 60, W_SQUARE, 30 } };
// 死亡:短促的"咔嚓"碎裂噪声,叠一层快速下坠的方波"啾——"(见 GD_SFX_DEATH_TONE)。
// 每层只有一段:段内包络连续,滑音不会出现断口。
static const seg_t S_DEATH[] = { { 2000, 250, 110, W_NOISE, 90 } };
static const seg_t S_DEATH_TONE[] = { { 640, 70, 260, W_SQUARE, 40 } };
static const seg_t S_CKPT[] = { { 1046, 1046, 45, W_TRIANGLE, 70 }, { 1568, 1568, 80, W_TRIANGLE, 70 } };
static const seg_t S_COMPLETE[] = {
    { 523, 523, 100, W_SQUARE, 40 }, { 659, 659, 100, W_SQUARE, 40 }, { 784, 784, 100, W_SQUARE, 40 },
    { 1046, 1046, 320, W_SQUARE, 40 },
};
static const seg_t S_NEWBEST[] = {
    { 784, 784, 70, W_TRIANGLE, 75 }, { 988, 988, 70, W_TRIANGLE, 75 }, { 1175, 1175, 70, W_TRIANGLE, 75 },
    { 1568, 1568, 220, W_TRIANGLE, 75 },
};

#define DEF(a) { a, (uint8_t)(sizeof(a) / sizeof((a)[0])), 0 }
#define DEF_LAYER(a, l) { a, (uint8_t)(sizeof(a) / sizeof((a)[0])), l }
static const sfx_def_t DEFS[GD_SFX_COUNT] = {
    [GD_SFX_NONE] = { NULL, 0, 0 },
    [GD_SFX_MOVE] = DEF(S_MOVE),
    [GD_SFX_OK] = DEF(S_OK),
    [GD_SFX_BACK] = DEF(S_BACK),
    [GD_SFX_DEATH] = DEF_LAYER(S_DEATH, GD_SFX_DEATH_TONE),
    [GD_SFX_CHECKPOINT] = DEF(S_CKPT),
    [GD_SFX_COMPLETE] = DEF(S_COMPLETE),
    [GD_SFX_NEWBEST] = DEF(S_NEWBEST),
    [GD_SFX_DEATH_TONE] = DEF(S_DEATH_TONE),
};

void gd_sfx_init(gd_sfx_t *s, uint32_t sample_rate) {
    memset(s, 0, sizeof *s);
    s->rate = sample_rate ? sample_rate : 16000;
    s->inc_per_hz = (uint32_t)(((uint64_t)1 << 32) / s->rate);
}

static void start_voice(gd_sfx_t *s, uint8_t id) {
    int slot = -1;
    for (int i = 0; i < GD_SFX_VOICES; i++) {
        if (!s->v[i].id) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {   // 满了:替换最旧的
        slot = 0;
        for (int i = 1; i < GD_SFX_VOICES; i++) {
            if (s->v[i].age < s->v[slot].age) slot = i;
        }
    }
    s->v[slot] = (gd_sfx_voice_t){ .id = id, .lfsr = 0xACE1u + (uint32_t)slot * 77u, .age = ++s->clock };
}

void gd_sfx_play(gd_sfx_t *s, uint8_t id) {
    if (id == GD_SFX_NONE || id >= GD_SFX_COUNT) return;
    start_voice(s, id);
    if (DEFS[id].layer) start_voice(s, DEFS[id].layer);
}

bool gd_sfx_busy(const gd_sfx_t *s) {
    for (int i = 0; i < GD_SFX_VOICES; i++) {
        if (s->v[i].id) return true;
    }
    return false;
}

void gd_sfx_stop_all(gd_sfx_t *s) {
    for (int i = 0; i < GD_SFX_VOICES; i++) s->v[i].id = 0;
}

static uint32_t own_ms(uint8_t id) {
    uint32_t ms = 0;
    for (uint8_t i = 0; i < DEFS[id].n; i++) ms += DEFS[id].seg[i].ms;
    return ms;
}

uint32_t gd_sfx_duration_ms(uint8_t id) {
    if (id >= GD_SFX_COUNT) return 0;
    const uint32_t ms = own_ms(id), layer = DEFS[id].layer ? own_ms(DEFS[id].layer) : 0;
    return ms > layer ? ms : layer;
}

// 一个采样(−32767..32767 × 音量)。
static int32_t voice_sample(gd_sfx_t *s, gd_sfx_voice_t *v) {
    const sfx_def_t *d = &DEFS[v->id];
    const seg_t *g = &d->seg[v->seg];
    const uint32_t len = (uint32_t)g->ms * s->rate / 1000;
    const int32_t hz = g->hz_from + ((int32_t)g->hz_to - g->hz_from) * (int32_t)v->pos / (int32_t)(len ? len : 1);
    const uint32_t prev = v->phase;
    v->phase += (uint32_t)hz * s->inc_per_hz;
    int32_t raw;
    switch (g->wave) {
    case W_TRIANGLE: {
        const uint32_t p = v->phase >> 16;                         // 0..65535
        raw = p < 32768 ? (int32_t)p * 2 - 32768 : 32767 - ((int32_t)p - 32768) * 2;
        break;
    }
    case W_NOISE:
        if (v->phase < prev) {   // 每个周期换一个随机值
            v->lfsr ^= v->lfsr << 13;
            v->lfsr ^= v->lfsr >> 17;
            v->lfsr ^= v->lfsr << 5;
            v->noise = (int16_t)(v->lfsr & 0xFFFF);
        }
        raw = v->noise;
        break;
    default:
        raw = v->phase < 0x80000000u ? 24000 : -24000;
        break;
    }
    // 包络:2 ms 起音,最后 40% 线性释音
    const uint32_t attack = s->rate / 500;
    int32_t env = 256;
    if (v->pos < attack) env = (int32_t)(v->pos * 256 / (attack ? attack : 1));
    const uint32_t rel_start = len * 6 / 10;
    if (v->pos > rel_start && len > rel_start) env = env * (int32_t)(len - v->pos) / (int32_t)(len - rel_start);
    const int32_t out = raw * g->vol / 100 * env / 256;
    if (++v->pos >= len) {
        v->pos = 0;
        if (++v->seg >= d->n) v->id = 0;
    }
    return out;
}

void gd_sfx_mix(gd_sfx_t *s, int16_t *pcm, int n, int gain) {
    for (int i = 0; i < GD_SFX_VOICES; i++) {
        gd_sfx_voice_t *v = &s->v[i];
        if (!v->id) continue;
        for (int k = 0; k < n && v->id; k++) {
            int32_t x = pcm[k] + voice_sample(s, v) * gain / 256;
            if (x > 32767) x = 32767;
            if (x < -32768) x = -32768;
            pcm[k] = (int16_t)x;
        }
    }
}
