// main/gd_mpack.h —— 音乐包格式解析与 IMA-ADPCM 解码(纯 C,主机可测)。
//
// 音乐包由 tools/gen_gd_music.py 从本地 BGM 生成,烧录到 "music" 数据分区(PRD §8)。
// 布局(小端):
//   头部 32 字节:magic "GDMU" | u16 版本 | u16 曲目数 | u32 头部总长 | u32 包总长 |
//                u32 头部 CRC32(计算时该字段按 0)| 12 字节保留
//   曲目表,每项 32 字节:u8 曲目号 | u8 保留 | u16 块长 | u32 采样率 | u32 数据偏移 |
//                u32 数据字节数 | u32 采样总数 | u32 数据 CRC32 | 8 字节保留
//   之后是各曲的 IMA-ADPCM 数据块(WAV IMA 单声道格式:4 字节块头 + 4 bit 采样,低半字节在前)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GD_MPACK_MAGIC       0x554D4447u   // "GDMU"
#define GD_MPACK_VERSION     1
#define GD_MPACK_HEADER      32
#define GD_MPACK_ENTRY       32
#define GD_MPACK_MAX_TRACKS  8
#define GD_MPACK_MAX_BLOCK   2048

typedef struct {
    uint8_t track;          // 曲目号(与关卡的 track 对应)
    uint16_t block_align;   // 每块字节数
    uint32_t sample_rate;
    uint32_t offset;        // 数据相对包起点的偏移
    uint32_t size;          // 数据字节数(块长的整数倍)
    uint32_t samples;       // 采样总数
    uint32_t crc;           // 数据 CRC32
} gd_mtrack_t;

typedef struct {
    uint16_t count;
    uint32_t header_size;
    uint32_t total_size;
    gd_mtrack_t tracks[GD_MPACK_MAX_TRACKS];
} gd_mpack_t;

typedef enum {
    GD_MPACK_OK = 0,
    GD_MPACK_EMPTY,         // 分区是擦除态(全 0xFF)或全 0:没有烧录音乐包
    GD_MPACK_BAD_MAGIC,
    GD_MPACK_BAD_VERSION,
    GD_MPACK_BAD_SIZE,
    GD_MPACK_BAD_CRC,
    GD_MPACK_BAD_TRACK,
} gd_mpack_err_t;

uint32_t gd_crc32(uint32_t crc, const uint8_t *data, size_t len);

// 第一步:只看固定的 32 字节头部,得到头部总长(用于再读取曲目表)。
gd_mpack_err_t gd_mpack_header_size(const uint8_t head[GD_MPACK_HEADER], uint32_t *header_size);
// 第二步:解析完整头部(head 长度 = header_size),校验 CRC 与每首曲目的边界。
// capacity 是分区大小:包总长与每首数据都不得越界。
gd_mpack_err_t gd_mpack_parse(const uint8_t *head, size_t len, uint32_t capacity, gd_mpack_t *out);
const gd_mtrack_t *gd_mpack_find(const gd_mpack_t *p, uint8_t track);
const char *gd_mpack_err_name(gd_mpack_err_t e);

// IMA-ADPCM(WAV 格式,单声道)。
int gd_adpcm_samples_per_block(uint16_t block_align);
// 解码一整块,返回写出的采样数(块长非法时返回 0)。
int gd_adpcm_decode_block(const uint8_t *block, uint16_t block_align, int16_t *out);
// 毫秒 → (块号, 块内跳过的采样数)。超出曲长时定位到末尾。
void gd_mtrack_seek(const gd_mtrack_t *t, int32_t ms, uint32_t *block, uint32_t *skip);
