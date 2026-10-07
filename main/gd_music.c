// main/gd_music.c —— 音乐包挂载与校验,说明见 gd_music.h。
#include "gd_music.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gd_music";

#define MUSIC_SUBTYPE  0x40
#define VERIFY_CHUNK   4096

static const esp_partition_t *s_part;
static gd_mpack_t s_pack;
static bool s_ok;
static gd_mpack_err_t s_err = GD_MPACK_EMPTY;
static volatile uint8_t s_bad[GD_MPACK_MAX_TRACKS];   // 数据 CRC 校验失败

bool gd_music_init(void) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)MUSIC_SUBTYPE, "music");
    if (!s_part) {
        ESP_LOGW(TAG, "没有 music 分区,静音运行");
        return false;
    }
    uint8_t head[GD_MPACK_HEADER];
    if (esp_partition_read(s_part, 0, head, sizeof head) != ESP_OK) return false;
    uint32_t header_size = 0;
    s_err = gd_mpack_header_size(head, &header_size);
    if (s_err != GD_MPACK_OK) {
        ESP_LOGW(TAG, "音乐包不可用(%s),静音运行。用 tools/gen_gd_music.py 生成后重新构建并烧录",
                 gd_mpack_err_name(s_err));
        return false;
    }
    uint8_t *buf = malloc(header_size);
    if (!buf) return false;
    esp_err_t e = esp_partition_read(s_part, 0, buf, header_size);
    s_err = e == ESP_OK ? gd_mpack_parse(buf, header_size, (uint32_t)s_part->size, &s_pack) : GD_MPACK_BAD_SIZE;
    free(buf);
    s_ok = s_err == GD_MPACK_OK;
    if (!s_ok) {
        ESP_LOGW(TAG, "音乐包头部无效(%s),静音运行", gd_mpack_err_name(s_err));
        return false;
    }
    ESP_LOGI(TAG, "音乐包:%u 首,%u 字节(分区 %u 字节)", s_pack.count, (unsigned)s_pack.total_size,
             (unsigned)s_part->size);
    return true;
}

const gd_mtrack_t *gd_music_track(uint8_t track) {
    if (!s_ok) return NULL;
    const gd_mtrack_t *t = gd_mpack_find(&s_pack, track);
    if (!t) return NULL;
    const int idx = (int)(t - s_pack.tracks);
    return s_bad[idx] ? NULL : t;
}

bool gd_music_track_ok(uint8_t track) {
    return gd_music_track(track) != NULL;
}

esp_err_t gd_music_read(uint32_t offset, void *buf, size_t len) {
    if (!s_part) return ESP_ERR_INVALID_STATE;
    return esp_partition_read(s_part, offset, buf, len);
}

const char *gd_music_status(void) {
    return gd_mpack_err_name(s_err);
}

static void verify_task(void *arg) {
    (void)arg;
    uint8_t *buf = malloc(VERIFY_CHUNK);
    for (uint16_t i = 0; buf && i < s_pack.count; i++) {
        const gd_mtrack_t *t = &s_pack.tracks[i];
        uint32_t crc = 0;
        bool io_ok = true;
        for (uint32_t off = 0; off < t->size; off += VERIFY_CHUNK) {
            const size_t n = t->size - off < VERIFY_CHUNK ? t->size - off : VERIFY_CHUNK;
            if (esp_partition_read(s_part, t->offset + off, buf, n) != ESP_OK) {
                io_ok = false;
                break;
            }
            crc = gd_crc32(crc, buf, n);
            vTaskDelay(1);   // 让出 CPU:校验在后台慢慢进行
        }
        if (!io_ok || crc != t->crc) {
            s_bad[i] = 1;
            ESP_LOGE(TAG, "曲目 %u 数据校验失败,该关将静音运行", t->track);
        } else {
            ESP_LOGI(TAG, "曲目 %u 数据校验通过", t->track);
        }
    }
    free(buf);
    vTaskDelete(NULL);
}

void gd_music_verify_async(void) {
    if (!s_ok) return;
    if (xTaskCreate(verify_task, "gd_mverify", 3072, NULL, 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "无法创建校验任务,跳过数据校验");
    }
}
