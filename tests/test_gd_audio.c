// tests/test_gd_audio.c —— 音乐包解析、IMA-ADPCM 解码与音效合成的主机测试(PRD §8)。
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gd_mpack.h"
#include "gd_sfx.h"

// —— 参考 IMA-ADPCM 编码器(块格式与重建公式都与 ffmpeg adpcm_ima_wav 相同) ——
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t ADJ[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int encode_nibble(int sample, int *pred, int *idx) {
    const int step = STEP[*idx];
    int diff = sample - *pred, nib = 0;
    if (diff < 0) {
        nib = 8;
        diff = -diff;
    }
    if (diff >= step) { nib |= 4; diff -= step; }
    if (diff >= step >> 1) { nib |= 2; diff -= step >> 1; }
    if (diff >= step >> 2) { nib |= 1; }
    const int vpdiff = ((2 * (nib & 7) + 1) * step) >> 3;   // 与解码器相同的精确重建
    *pred += (nib & 8) ? -vpdiff : vpdiff;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *idx += ADJ[nib];
    if (*idx < 0) *idx = 0;
    if (*idx > 88) *idx = 88;
    return nib;
}

// 编码一块:pcm 至少 samples_per_block 个采样。idx 在块间延续。
static void encode_block(const int16_t *pcm, uint16_t align, uint8_t *blk, int *idx) {
    int pred = pcm[0];
    blk[0] = (uint8_t)(pred & 0xFF);
    blk[1] = (uint8_t)((pred >> 8) & 0xFF);
    blk[2] = (uint8_t)*idx;
    blk[3] = 0;
    int s = 1;
    for (uint16_t i = 4; i < align; i++) {
        const int lo = encode_nibble(pcm[s++], &pred, idx);
        const int hi = encode_nibble(pcm[s++], &pred, idx);
        blk[i] = (uint8_t)(lo | hi << 4);
    }
}

static void test_adpcm_round_trip(void) {
    const uint16_t align = 1024;
    const int spb = gd_adpcm_samples_per_block(align);
    assert(spb == 2041);
    assert(gd_adpcm_samples_per_block(7) == 0 && gd_adpcm_samples_per_block(1026) == 0);
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)spb * 4);
    int16_t *dec = malloc(sizeof(int16_t) * (size_t)spb);
    // 和弦 + 包络,接近真实音乐的动态
    for (int i = 0; i < spb * 4; i++) {
        const double t = i / 16000.0;
        const double env = 0.3 + 0.7 * fabs(sin(t * 3.0));
        pcm[i] = (int16_t)(env * (9000 * sin(2 * M_PI * 220 * t) + 5000 * sin(2 * M_PI * 554 * t) +
                                  3000 * sin(2 * M_PI * 1320 * t)));
    }
    uint8_t blk[1024];
    int idx = 0;
    double sig = 0, err = 0;
    for (int b = 0; b < 4; b++) {
        encode_block(pcm + b * spb, align, blk, &idx);
        assert(gd_adpcm_decode_block(blk, align, dec) == spb);
        assert(dec[0] == pcm[b * spb]);
        for (int i = 0; i < spb; i++) {
            const double d = (double)dec[i] - pcm[b * spb + i];
            sig += (double)pcm[b * spb + i] * pcm[b * spb + i];
            err += d * d;
        }
    }
    const double snr = 10 * log10(sig / (err + 1));
    printf("adpcm round trip SNR %.1f dB\n", snr);
    assert(snr > 20.0);
    // 已知向量:全 0 半字节 → 预测值每步只加 step>>3,索引递减
    memset(blk, 0, sizeof blk);
    blk[0] = 0xE8;   // 1000
    blk[1] = 0x03;
    blk[2] = 10;
    assert(gd_adpcm_decode_block(blk, 8, dec) == 9);
    const int16_t expect[9] = { 1000, 1002, 1004, 1006, 1007, 1008, 1009, 1010, 1011 };
    assert(memcmp(dec, expect, sizeof expect) == 0);
    free(pcm);
    free(dec);
}

// —— 构造一个内存中的音乐包 ——
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static size_t build_pack(uint8_t *buf, size_t cap, int tracks, uint16_t align, uint32_t blocks_per_track) {
    memset(buf, 0, cap);
    const uint32_t header = GD_MPACK_HEADER + (uint32_t)tracks * GD_MPACK_ENTRY;
    uint32_t off = header;
    for (int i = 0; i < tracks; i++) {
        uint8_t *e = buf + GD_MPACK_HEADER + i * GD_MPACK_ENTRY;
        const uint32_t size = align * blocks_per_track;
        for (uint32_t k = 0; k < size; k++) buf[off + k] = (uint8_t)(k * 7 + i);
        e[0] = (uint8_t)(i + 1);
        wr16(e + 2, align);
        wr32(e + 4, 16000);
        wr32(e + 8, off);
        wr32(e + 12, size);
        wr32(e + 16, (uint32_t)gd_adpcm_samples_per_block(align) * blocks_per_track - 100);
        wr32(e + 20, gd_crc32(0, buf + off, size));
        off += size;
    }
    wr32(buf, GD_MPACK_MAGIC);
    wr16(buf + 4, GD_MPACK_VERSION);
    wr16(buf + 6, (uint16_t)tracks);
    wr32(buf + 8, header);
    wr32(buf + 12, off);
    wr32(buf + 16, 0);
    uint8_t zero[4] = { 0 };
    uint32_t crc = gd_crc32(0, buf, 16);
    crc = gd_crc32(crc, zero, 4);
    crc = gd_crc32(crc, buf + 20, header - 20);
    wr32(buf + 16, crc);
    return off;
}

static void test_mpack_parse(void) {
    static uint8_t buf[64 * 1024];
    const size_t total = build_pack(buf, sizeof buf, 3, 512, 10);
    uint32_t hs = 0;
    assert(gd_mpack_header_size(buf, &hs) == GD_MPACK_OK && hs == GD_MPACK_HEADER + 3 * GD_MPACK_ENTRY);
    gd_mpack_t p;
    assert(gd_mpack_parse(buf, hs, sizeof buf, &p) == GD_MPACK_OK);
    assert(p.count == 3 && p.total_size == total);
    const gd_mtrack_t *t2 = gd_mpack_find(&p, 2);
    assert(t2 && t2->block_align == 512 && t2->sample_rate == 16000);
    assert(t2->crc == gd_crc32(0, buf + t2->offset, t2->size));
    assert(gd_mpack_find(&p, 9) == NULL);

    // 分区容量不足以容纳整个包
    assert(gd_mpack_parse(buf, hs, (uint32_t)total - 1, &p) == GD_MPACK_BAD_SIZE);
    // 头部被改动:CRC 失败
    buf[GD_MPACK_HEADER + 4] ^= 1;
    assert(gd_mpack_parse(buf, hs, sizeof buf, &p) == GD_MPACK_BAD_CRC);
    buf[GD_MPACK_HEADER + 4] ^= 1;
    // 擦除态 / 全零:没有音乐包
    uint8_t blank[GD_MPACK_HEADER];
    memset(blank, 0xFF, sizeof blank);
    assert(gd_mpack_header_size(blank, &hs) == GD_MPACK_EMPTY);
    memset(blank, 0, sizeof blank);
    assert(gd_mpack_header_size(blank, &hs) == GD_MPACK_EMPTY);
    // 魔数与版本
    memcpy(blank, "XXXX", 4);
    assert(gd_mpack_header_size(blank, &hs) == GD_MPACK_BAD_MAGIC);
    buf[4] = 9;
    assert(gd_mpack_header_size(buf, &hs) == GD_MPACK_BAD_VERSION);
    buf[4] = GD_MPACK_VERSION;
}

static void test_mpack_bad_track(void) {
    static uint8_t buf[32 * 1024];
    build_pack(buf, sizeof buf, 1, 512, 4);
    uint8_t *e = buf + GD_MPACK_HEADER;
    wr32(e + 12, 512 * 4 + 1);   // 不是块长的整数倍(同时越界)
    uint8_t zero[4] = { 0 };
    const uint32_t header = GD_MPACK_HEADER + GD_MPACK_ENTRY;
    uint32_t crc = gd_crc32(0, buf, 16);
    crc = gd_crc32(crc, zero, 4);
    crc = gd_crc32(crc, buf + 20, header - 20);
    wr32(buf + 16, crc);
    gd_mpack_t p;
    assert(gd_mpack_parse(buf, header, sizeof buf, &p) == GD_MPACK_BAD_TRACK);
    assert(strcmp(gd_mpack_err_name(GD_MPACK_BAD_TRACK), "bad track") == 0);
}

static void test_seek(void) {
    const gd_mtrack_t t = { .track = 1, .block_align = 1024, .sample_rate = 16000, .samples = 2041 * 10 };
    uint32_t b, s;
    gd_mtrack_seek(&t, 0, &b, &s);
    assert(b == 0 && s == 0);
    gd_mtrack_seek(&t, 1000, &b, &s);   // 16000 采样 = 第 7 块 + 1713
    assert(b == 7 && s == 16000 - 7 * 2041);
    gd_mtrack_seek(&t, 999999, &b, &s); // 越过末尾:停在末尾
    assert((uint64_t)b * 2041 + s == t.samples);
    gd_mtrack_seek(&t, -50, &b, &s);
    assert(b == 0 && s == 0);
}

static void test_sfx(void) {
    gd_sfx_t s;
    gd_sfx_init(&s, 16000);
    assert(!gd_sfx_busy(&s));
    gd_sfx_play(&s, GD_SFX_DEATH);
    gd_sfx_play(&s, GD_SFX_NONE);   // 忽略
    gd_sfx_play(&s, 200);           // 越界忽略
    assert(gd_sfx_busy(&s));
    int16_t buf[240];
    int64_t energy = 0;
    const int blocks = (int)(gd_sfx_duration_ms(GD_SFX_DEATH) * 16000 / 1000 / 240) + 2;
    for (int b = 0; b < blocks; b++) {
        memset(buf, 0, sizeof buf);
        gd_sfx_mix(&s, buf, 240, 256);
        for (int i = 0; i < 240; i++) energy += (int64_t)buf[i] * buf[i];
    }
    assert(energy > 0);
    assert(!gd_sfx_busy(&s));
    // 饱和截断
    for (int i = 0; i < 240; i++) buf[i] = 32000;
    gd_sfx_play(&s, GD_SFX_COMPLETE);
    gd_sfx_mix(&s, buf, 240, 256);
    for (int i = 0; i < 240; i++) assert(buf[i] <= 32767);
    // 增益 0:不改变 PCM
    gd_sfx_stop_all(&s);
    gd_sfx_play(&s, GD_SFX_OK);
    for (int i = 0; i < 240; i++) buf[i] = 123;
    gd_sfx_mix(&s, buf, 240, 0);
    for (int i = 0; i < 240; i++) assert(buf[i] == 123);
    // 超过声部数:替换最旧的
    gd_sfx_stop_all(&s);
    for (int i = 0; i < GD_SFX_VOICES + 2; i++) gd_sfx_play(&s, GD_SFX_MOVE);
    int active = 0;
    for (int i = 0; i < GD_SFX_VOICES; i++) active += s.v[i].id != 0;
    assert(active == GD_SFX_VOICES);
    assert(gd_sfx_duration_ms(GD_SFX_COMPLETE) == 620);
}

int main(void) {
    test_adpcm_round_trip();
    test_mpack_parse();
    test_mpack_bad_track();
    test_seek();
    test_sfx();
    printf("Geometry Sprint audio tests: PASS\n");
    return 0;
}
