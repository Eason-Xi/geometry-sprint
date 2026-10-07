// main/gd_audio.c —— 音频任务与歌曲时钟,说明见 gd_audio.h。
#include "gd_audio.h"

#include <limits.h>
#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gd_mpack.h"
#include "gd_music.h"
#include "gd_sfx.h"

static const char *TAG = "gd_audio";

#define BLOCK          BSP_AUDIO_DMA_FRAME_NUM                              // 240 帧 = 15 ms
#define QUEUE_FRAMES   (BSP_AUDIO_DMA_DESC_NUM * BSP_AUDIO_DMA_FRAME_NUM)   // 1440 帧 = 90 ms
#define BLOCK_US       ((int64_t)BLOCK * 1000000 / GD_AUDIO_RATE)
#define QUEUE_US       ((int64_t)QUEUE_FRAMES * 1000000 / GD_AUDIO_RATE)
#define TASK_STACK     4096
#define TASK_PRIORITY  6       // 高于应用任务(5)与 LVGL 任务(4),保证 PCM 供给不断流
#define SFX_RING       16
#define SFX_GAIN       200     // 音效相对音乐的音量(/256)
#define IDLE_TAIL_US   250000  // 声音结束后再写一会儿静音,再停止写入
#define CLOCK_AHEAD_US 20000   // 两次写入之间最多外推 20 ms,音频卡住时时钟不跑飞

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// —— 命令(任意任务写,音频任务读) ——
static uint32_t s_play_seq;
static uint8_t s_req_track;
static int32_t s_req_start_ms;
static bool s_req_stop;
static bool s_req_paused;
static uint8_t s_req_volume = 6;
static uint8_t s_sfx[SFX_RING];
static uint8_t s_sfx_head, s_sfx_count;

// —— 歌曲时钟(音频任务写,任意任务读) ——
static int64_t s_anchor_pos;    // 扬声器上正在播放的歌曲采样
static int64_t s_anchor_us;
static bool s_clock_valid;
static bool s_clock_running;
static uint32_t s_song_id;
static volatile bool s_quiet = true;
static volatile uint32_t s_underruns;

static TaskHandle_t s_task;
static bool s_hw_ok;
static gd_sfx_t s_syn;
static int16_t s_block[BLOCK];

// —— 解码状态(只在音频任务里) ——
static const gd_mtrack_t *s_track;
static int64_t s_pos;            // 已送出的歌曲采样(含开始偏移)
static uint32_t s_blk;           // 下一块的块号
static int16_t s_pcm[GD_MPACK_MAX_BLOCK * 2];
static uint8_t s_raw[GD_MPACK_MAX_BLOCK];
static int s_pcm_len, s_pcm_at;
static uint32_t s_skip;          // 下一块解码后先丢弃的采样数(跳转用)

uint8_t gd_audio_volume_percent(uint8_t level) {
    if (level == 0) return 0;
    if (level > 10) level = 10;
    return (uint8_t)(40u + 6u * level);    // codec 0..100 对应 −50..0 dB:1 档 46%,10 档 100%
}

bool gd_audio_hw_ok(void) {
    return s_hw_ok;
}

static void notify(void) {
    if (s_task) xTaskNotifyGive(s_task);
}

void gd_audio_play(uint8_t track, int32_t start_ms) {
    taskENTER_CRITICAL(&s_mux);
    s_req_track = track;
    s_req_start_ms = start_ms < 0 ? 0 : start_ms;
    s_req_stop = false;
    s_req_paused = false;
    s_play_seq++;
    s_quiet = false;
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

void gd_audio_stop(void) {
    taskENTER_CRITICAL(&s_mux);
    s_req_stop = true;
    s_req_paused = false;
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

void gd_audio_pause(bool paused) {
    taskENTER_CRITICAL(&s_mux);
    s_req_paused = paused;
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

void gd_audio_sfx(uint8_t id) {
    taskENTER_CRITICAL(&s_mux);
    if (s_sfx_count < SFX_RING) {
        s_sfx[(s_sfx_head + s_sfx_count) % SFX_RING] = id;
        s_sfx_count++;
    }
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

void gd_audio_set_volume(uint8_t level) {
    taskENTER_CRITICAL(&s_mux);
    s_req_volume = level > 10 ? 10 : level;
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

int32_t gd_audio_song_ms(int64_t t_us) {
    taskENTER_CRITICAL(&s_mux);
    const bool valid = s_clock_valid, running = s_clock_running;
    const int64_t pos = s_anchor_pos, at = s_anchor_us;
    taskEXIT_CRITICAL(&s_mux);
    if (!valid) return INT32_MIN;
    int64_t extra = running ? t_us - at : 0;
    if (extra > CLOCK_AHEAD_US) extra = CLOCK_AHEAD_US;
    if (extra < -1000000) extra = -1000000;
    const int64_t song_us = pos * 1000000 / GD_AUDIO_RATE + extra;
    return (int32_t)(song_us >= 0 ? song_us / 1000 : -((-song_us + 999) / 1000));
}

uint32_t gd_audio_song_id(void) {
    taskENTER_CRITICAL(&s_mux);
    const uint32_t id = s_song_id;
    taskEXIT_CRITICAL(&s_mux);
    return id;
}

bool gd_audio_quiet(void) {
    return s_quiet;
}

uint32_t gd_audio_underruns(void) {
    return s_underruns;
}

static void set_clock(int64_t pos, int64_t at, bool valid, bool running) {
    taskENTER_CRITICAL(&s_mux);
    s_anchor_pos = pos;
    s_anchor_us = at;
    s_clock_valid = valid;
    s_clock_running = running;
    taskEXIT_CRITICAL(&s_mux);
}

// 定位到歌曲第 start 个采样:读出所在块并跳过块内多余的采样。
static void seek(int64_t start) {
    s_pcm_len = s_pcm_at = 0;
    s_skip = 0;
    if (!s_track) return;
    uint32_t blk, skip;
    gd_mtrack_seek(s_track, (int32_t)(start * 1000 / GD_AUDIO_RATE), &blk, &skip);
    s_blk = blk;
    // 毫秒取整带来的偏差折算进块内跳过的采样
    const int64_t at = (int64_t)blk * gd_adpcm_samples_per_block(s_track->block_align) + skip;
    const int64_t diff = start - at;
    if (diff > 0) skip += (uint32_t)diff;
    s_skip = skip;
}

// 取 n 个音乐采样;曲目结束或没有曲目时补零。
static void music_fill(int16_t *out, int n) {
    int i = 0;
    while (i < n) {
        if (s_track && s_pcm_at >= s_pcm_len) {
            const uint32_t blocks = s_track->size / s_track->block_align;
            if (s_blk < blocks) {
                if (gd_music_read(s_track->offset + s_blk * s_track->block_align, s_raw, s_track->block_align) ==
                    ESP_OK) {
                    s_pcm_len = gd_adpcm_decode_block(s_raw, s_track->block_align, s_pcm);
                } else {
                    s_pcm_len = gd_adpcm_samples_per_block(s_track->block_align);
                    memset(s_pcm, 0, sizeof(int16_t) * (size_t)s_pcm_len);
                }
                s_blk++;
                s_pcm_at = (int)s_skip < s_pcm_len ? (int)s_skip : s_pcm_len;
                s_skip = 0;
                continue;
            }
            s_track = NULL;   // 曲终
        }
        if (s_track && s_pcm_at < s_pcm_len) {
            const int take = (s_pcm_len - s_pcm_at) < (n - i) ? (s_pcm_len - s_pcm_at) : (n - i);
            memcpy(out + i, s_pcm + s_pcm_at, sizeof(int16_t) * (size_t)take);
            s_pcm_at += take;
            i += take;
        } else {
            memset(out + i, 0, sizeof(int16_t) * (size_t)(n - i));
            i = n;
        }
    }
}

// 输出一块 PCM。没有音频硬件或写入失败时按块时长延时,保持时钟节奏。返回是否真正写入。
static bool output_block(int64_t *virtual_us) {
    if (s_hw_ok) {
        if (bsp_audio_write(s_block, sizeof s_block) == ESP_OK) return true;
        ESP_LOGW(TAG, "PCM 写入失败,本块改为空跑");
    }
    *virtual_us += BLOCK_US;
    const int64_t wait = *virtual_us - esp_timer_get_time();
    if (wait > 0) vTaskDelay(pdMS_TO_TICKS((wait + 999) / 1000));
    else if (wait < -100000) *virtual_us = esp_timer_get_time();
    return false;
}

static void audio_task(void *arg) {
    (void)arg;
    uint32_t applied_play = 0;
    int applied_volume = -1;
    bool song_active = false, paused = false, writing = false;
    int64_t virtual_us = esp_timer_get_time();
    int64_t last_sound_us = 0, last_write_us = 0;

    for (;;) {
        // —— 取命令快照 ——
        taskENTER_CRITICAL(&s_mux);
        const uint32_t play_seq = s_play_seq;
        const uint8_t req_track = s_req_track;
        const int32_t req_start = s_req_start_ms;
        const bool stop = s_req_stop;
        s_req_stop = false;
        const bool want_pause = s_req_paused;
        const uint8_t volume = s_req_volume;
        uint8_t sfx[SFX_RING];
        const uint8_t sfx_n = s_sfx_count;
        for (uint8_t i = 0; i < sfx_n; i++) sfx[i] = s_sfx[(s_sfx_head + i) % SFX_RING];
        s_sfx_head = (uint8_t)((s_sfx_head + sfx_n) % SFX_RING);
        s_sfx_count = 0;
        taskEXIT_CRITICAL(&s_mux);

        if (s_hw_ok && volume != applied_volume) {
            bsp_audio_set_volume(gd_audio_volume_percent(volume));   // 走 I2C,与 PCM 写入串行
            applied_volume = volume;
        }

        if (play_seq != applied_play) {
            applied_play = play_seq;
            s_track = req_track ? gd_music_track(req_track) : NULL;
            s_pos = (int64_t)req_start * GD_AUDIO_RATE / 1000;
            seek(s_pos);
            song_active = true;
            paused = false;
            taskENTER_CRITICAL(&s_mux);
            s_song_id++;
            taskEXIT_CRITICAL(&s_mux);
            set_clock(s_pos - (s_hw_ok ? QUEUE_FRAMES : 0), esp_timer_get_time(), true, true);
        } else if (stop && song_active) {
            song_active = false;
            s_track = NULL;
            set_clock(0, 0, false, false);
        }
        if (song_active && want_pause != paused) {
            paused = want_pause;
            taskENTER_CRITICAL(&s_mux);
            s_clock_running = !paused;
            s_anchor_us = esp_timer_get_time();
            taskEXIT_CRITICAL(&s_mux);
        }

        for (uint8_t i = 0; i < sfx_n; i++) gd_sfx_play(&s_syn, sfx[i]);

        const bool advancing = song_active && !paused;
        // 音乐不在推进时允许写 Flash:音效只用内存中的合成数据,I2S 中断在 IRAM,队列里的 90 ms 不受影响。
        s_quiet = !advancing;
        const int64_t now = esp_timer_get_time();
        if (advancing || gd_sfx_busy(&s_syn)) last_sound_us = now;
        if (!advancing && !gd_sfx_busy(&s_syn) && now - last_sound_us > IDLE_TAIL_US) {
            // 无声且尾巴已播完:停止写入,等待新命令。DMA 自动补零输出静音。
            writing = false;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
            virtual_us = esp_timer_get_time();
            continue;
        }
        if (advancing && !writing) {
            // 从空闲恢复(开播或暂停后继续):先写满一整队静音,
            // 此后"正在播放 = 已写入 − 队列容量"才成立。
            memset(s_block, 0, sizeof s_block);
            for (int i = 0; i < BSP_AUDIO_DMA_DESC_NUM; i++) output_block(&virtual_us);
            last_write_us = 0;
        }
        if (advancing) {
            music_fill(s_block, BLOCK);
            s_pos += BLOCK;
        } else {
            memset(s_block, 0, sizeof s_block);
        }
        gd_sfx_mix(&s_syn, s_block, BLOCK, SFX_GAIN);
        const int64_t t_before = esp_timer_get_time();
        if (writing && last_write_us && t_before - last_write_us > QUEUE_US - BLOCK_US) s_underruns++;
        const bool real = output_block(&virtual_us);
        writing = true;
        const int64_t t_after = esp_timer_get_time();
        last_write_us = t_after;
        if (advancing) {
            // 写入返回时队列是满的:扬声器正在播放的是 (已写入 − 队列容量)。
            set_clock(s_pos - (real ? QUEUE_FRAMES : 0), t_after, true, true);
        }
    }
}

bool gd_audio_start(bool hw_ok) {
    if (s_task) return true;
    s_hw_ok = hw_ok;
    gd_sfx_init(&s_syn, GD_AUDIO_RATE);
    if (hw_ok) {
        const esp_err_t e = bsp_audio_set_format(GD_AUDIO_RATE, 16, 1);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "设置音频格式失败(%s),改为无声运行", esp_err_to_name(e));
            s_hw_ok = false;
        }
    }
    if (xTaskCreate(audio_task, "gd_audio", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "创建音频任务失败");
        s_task = NULL;
        return false;
    }
    ESP_LOGI(TAG, "音频任务已启动(%s,%d Hz,块 %d 帧,输出队列 %d 帧 = %d ms)", s_hw_ok ? "扬声器" : "无声空跑",
             GD_AUDIO_RATE, BLOCK, QUEUE_FRAMES, (int)(QUEUE_FRAMES * 1000 / GD_AUDIO_RATE));
    return true;
}
