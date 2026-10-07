// main/gd_mpack.c —— 音乐包解析与 IMA-ADPCM 解码,说明见 gd_mpack.h。
#include "gd_mpack.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t gd_crc32(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool all_bytes(const uint8_t *p, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] != v) return false;
    }
    return true;
}

gd_mpack_err_t gd_mpack_header_size(const uint8_t head[GD_MPACK_HEADER], uint32_t *header_size) {
    if (all_bytes(head, GD_MPACK_HEADER, 0xFF) || all_bytes(head, GD_MPACK_HEADER, 0x00)) return GD_MPACK_EMPTY;
    if (rd32(head) != GD_MPACK_MAGIC) return GD_MPACK_BAD_MAGIC;
    if (rd16(head + 4) != GD_MPACK_VERSION) return GD_MPACK_BAD_VERSION;
    const uint16_t count = rd16(head + 6);
    const uint32_t size = rd32(head + 8);
    if (count == 0 || count > GD_MPACK_MAX_TRACKS || size != GD_MPACK_HEADER + (uint32_t)count * GD_MPACK_ENTRY) {
        return GD_MPACK_BAD_SIZE;
    }
    *header_size = size;
    return GD_MPACK_OK;
}

gd_mpack_err_t gd_mpack_parse(const uint8_t *head, size_t len, uint32_t capacity, gd_mpack_t *out) {
    memset(out, 0, sizeof *out);
    if (len < GD_MPACK_HEADER) return GD_MPACK_BAD_SIZE;
    uint32_t header_size;
    const gd_mpack_err_t e = gd_mpack_header_size(head, &header_size);
    if (e != GD_MPACK_OK) return e;
    if (len < header_size) return GD_MPACK_BAD_SIZE;
    const uint32_t total = rd32(head + 12);
    if (total < header_size || total > capacity) return GD_MPACK_BAD_SIZE;
    // 头部 CRC:CRC 字段按 0 计算
    uint8_t zero[4] = { 0 };
    uint32_t crc = gd_crc32(0, head, 16);
    crc = gd_crc32(crc, zero, 4);
    crc = gd_crc32(crc, head + 20, header_size - 20);
    if (crc != rd32(head + 16)) return GD_MPACK_BAD_CRC;

    out->count = rd16(head + 6);
    out->header_size = header_size;
    out->total_size = total;
    for (uint16_t i = 0; i < out->count; i++) {
        const uint8_t *p = head + GD_MPACK_HEADER + (size_t)i * GD_MPACK_ENTRY;
        gd_mtrack_t *t = &out->tracks[i];
        t->track = p[0];
        t->block_align = rd16(p + 2);
        t->sample_rate = rd32(p + 4);
        t->offset = rd32(p + 8);
        t->size = rd32(p + 12);
        t->samples = rd32(p + 16);
        t->crc = rd32(p + 20);
        const int spb = gd_adpcm_samples_per_block(t->block_align);
        if (t->track == 0 || spb <= 0 || t->sample_rate < 8000 || t->sample_rate > 48000) return GD_MPACK_BAD_TRACK;
        if (t->size == 0 || t->size % t->block_align) return GD_MPACK_BAD_TRACK;
        if (t->offset < header_size || t->offset > total || t->size > total - t->offset) return GD_MPACK_BAD_TRACK;
        const uint64_t max_samples = (uint64_t)(t->size / t->block_align) * (uint64_t)spb;
        if (t->samples == 0 || t->samples > max_samples) return GD_MPACK_BAD_TRACK;
        for (uint16_t k = 0; k < i; k++) {
            if (out->tracks[k].track == t->track) return GD_MPACK_BAD_TRACK;
        }
    }
    return GD_MPACK_OK;
}

const gd_mtrack_t *gd_mpack_find(const gd_mpack_t *p, uint8_t track) {
    for (uint16_t i = 0; i < p->count; i++) {
        if (p->tracks[i].track == track) return &p->tracks[i];
    }
    return NULL;
}

const char *gd_mpack_err_name(gd_mpack_err_t e) {
    switch (e) {
    case GD_MPACK_OK: return "ok";
    case GD_MPACK_EMPTY: return "empty";
    case GD_MPACK_BAD_MAGIC: return "bad magic";
    case GD_MPACK_BAD_VERSION: return "bad version";
    case GD_MPACK_BAD_SIZE: return "bad size";
    case GD_MPACK_BAD_CRC: return "bad crc";
    case GD_MPACK_BAD_TRACK: return "bad track";
    }
    return "?";
}

// —— IMA-ADPCM ——
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t INDEX_ADJ[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

int gd_adpcm_samples_per_block(uint16_t block_align) {
    if (block_align < 8 || block_align > GD_MPACK_MAX_BLOCK || (block_align & 3)) return 0;
    return (block_align - 4) * 2 + 1;
}

int gd_adpcm_decode_block(const uint8_t *block, uint16_t block_align, int16_t *out) {
    const int spb = gd_adpcm_samples_per_block(block_align);
    if (spb <= 0) return 0;
    int pred = (int16_t)rd16(block);
    int idx = block[2];
    if (idx > 88) idx = 88;
    int n = 0;
    out[n++] = (int16_t)pred;
    for (uint16_t i = 4; i < block_align; i++) {
        for (int half = 0; half < 2; half++) {
            const int nib = half ? block[i] >> 4 : block[i] & 0x0F;
            // 精确重建 ((2Δ+1)·step)/8:与 ffmpeg adpcm_ima_wav 编码器跟踪预测值的公式一致,
            // 用按位累加的经典公式解码会逐步漂移(实测最大偏差 70 LSB)。
            const int diff = ((2 * (nib & 7) + 1) * STEP[idx]) >> 3;
            pred += (nib & 8) ? -diff : diff;
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            idx += INDEX_ADJ[nib];
            if (idx < 0) idx = 0;
            if (idx > 88) idx = 88;
            out[n++] = (int16_t)pred;
        }
    }
    return n;
}

void gd_mtrack_seek(const gd_mtrack_t *t, int32_t ms, uint32_t *block, uint32_t *skip) {
    const uint32_t spb = (uint32_t)gd_adpcm_samples_per_block(t->block_align);
    uint64_t sample = ms <= 0 ? 0 : (uint64_t)ms * t->sample_rate / 1000;
    if (sample >= t->samples) sample = t->samples;
    *block = spb ? (uint32_t)(sample / spb) : 0;
    *skip = spb ? (uint32_t)(sample % spb) : 0;
}
