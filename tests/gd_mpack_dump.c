// tests/gd_mpack_dump.c —— 用固件同一份解析/解码代码检查音乐包(tests/test_gd_music.py 调用)。
//
//   gd_mpack_dump <pack.bin>                 解析头部,逐曲校验数据 CRC
//   gd_mpack_dump <pack.bin> <track> <out>   另把该曲完整解码成 s16le 原始 PCM
#include <stdio.h>
#include <stdlib.h>

#include "gd_mpack.h"

int main(int argc, char **argv) {
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s pack.bin [track out.raw]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);

    gd_mpack_t p;
    const gd_mpack_err_t e = gd_mpack_parse(buf, (size_t)size, (uint32_t)size, &p);
    if (e != GD_MPACK_OK) {
        printf("ERROR %s\n", gd_mpack_err_name(e));
        return 1;
    }
    printf("OK %u tracks, %u bytes\n", p.count, p.total_size);
    int bad = 0;
    for (uint16_t i = 0; i < p.count; i++) {
        const gd_mtrack_t *t = &p.tracks[i];
        const int ok = gd_crc32(0, buf + t->offset, t->size) == t->crc;
        printf("track %u: %u Hz, block %u, %u samples, crc %s\n", t->track, t->sample_rate, t->block_align, t->samples,
               ok ? "ok" : "BAD");
        bad |= !ok;
    }
    if (argc == 4) {
        const gd_mtrack_t *t = gd_mpack_find(&p, (uint8_t)atoi(argv[2]));
        if (!t) return 1;
        FILE *out = fopen(argv[3], "wb");
        int16_t pcm[GD_MPACK_MAX_BLOCK * 2];
        uint32_t left = t->samples;
        for (uint32_t off = 0; off < t->size && left; off += t->block_align) {
            const int n = gd_adpcm_decode_block(buf + t->offset + off, t->block_align, pcm);
            const uint32_t take = (uint32_t)n < left ? (uint32_t)n : left;
            fwrite(pcm, sizeof(int16_t), take, out);
            left -= take;
        }
        fclose(out);
    }
    free(buf);
    return bad;
}
