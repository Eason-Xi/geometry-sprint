// main/gd_audio.h —— 音频任务:音乐包流式解码 + 音效混音 + 歌曲时钟(ESP-IDF)。
//
// 16 kHz / 16 bit / 单声道。I2S 输出队列 6 × 240 帧 = 90 ms:持续写入时队列总是满的,
// 因此"正在播出的歌曲采样 = 已写入 − 队列容量",再用 esp_timer 在两次写入之间插值(最多外推 20 ms),
// 得到与扬声器同步的歌曲时钟。物理与画面都以它为准,音频是唯一的时间基准(PRD §7.5)。
// 没有该曲(或没有音频硬件)时照样推进时钟:前者输出静音,后者按定时器空跑。
//
// 线程:命令函数可在任意任务调用(短临界区,不阻塞);只有音频任务调用 BSP 音频接口与读音乐分区。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GD_AUDIO_RATE 16000

bool gd_audio_start(bool hw_ok);
bool gd_audio_hw_ok(void);
// 从 start_ms 播放第 track 首;track 为 0 或不可用时以静音推进时钟。
void gd_audio_play(uint8_t track, int32_t start_ms);
void gd_audio_stop(void);
void gd_audio_pause(bool paused);
void gd_audio_sfx(uint8_t id);
// 音量档位 0..10(0 = 静音)。
void gd_audio_set_volume(uint8_t level);
uint8_t gd_audio_volume_percent(uint8_t level);
// t_us 时刻扬声器正在播放的歌曲毫秒;没有歌曲时返回 INT32_MIN。
int32_t gd_audio_song_ms(int64_t t_us);
// 每次 gd_audio_play() 递增;用于确认时钟属于哪一次播放。
uint32_t gd_audio_song_id(void);
// 音乐没有在推进(停止或暂停):此时写 Flash 不会打断音乐流。
bool gd_audio_quiet(void);
// 写入间隔超过队列时长(可能听到断音)的次数。
uint32_t gd_audio_underruns(void);
