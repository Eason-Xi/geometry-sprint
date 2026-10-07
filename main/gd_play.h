// main/gd_play.h —— 对局控制器:把歌曲时钟、按键、物理、界面事件、音乐与存档串起来(纯 C)。
//
// 固件(gd_app.c)与主机预览共用这一份流程逻辑:
//   * 每帧 gd_play_frame():按歌曲时间(+ 音画偏移)推进物理,处理死亡 / 通关 / 检查点事件;
//   * 死亡 GD_DEATH_MS 后自动重开(练习模式回到检查点,音乐跳到对应位置);
//   * 通关 GD_COMPLETE_MS 后进入结算页。
// 所有函数都在 LVGL 任务里(或持有 LVGL 锁时)调用;音乐与存档通过 io 回调发出,回调不得阻塞。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gd_game.h"
#include "gd_model.h"

#define GD_NO_CLOCK INT32_MIN
#define GD_INPUT_GRACE_MS 6     // 物理比歌曲时间晚 6 ms,让按键事件先到达

typedef struct {
    void (*music_play)(uint8_t track, int32_t start_ms);   // 从 start_ms 播放该曲(无音乐时用静音虚拟时钟)
    void (*music_stop)(void);
    void (*music_pause)(bool paused);
    void (*sfx)(uint8_t id);
    void (*save)(void);                                     // 请求写存档(应用在音乐停下时执行)
} gd_play_io_t;

typedef struct {
    gd_model_t *m;
    gd_game_t *g;
    const gd_play_io_t *io;
    bool active;              // 正在一局中(PLAY 页)
    bool clock_ready;         // 音乐时钟已对准本次尝试
    int64_t death_us;         // 0 = 未死亡
    int64_t finish_us;        // 0 = 未通关
    int64_t last_frame_us;
    uint32_t play_ms;         // 尚未计入存档的游戏时长
    uint32_t jumps_flushed;   // 已计入存档的本局跳跃数
} gd_play_t;

void gd_play_init(gd_play_t *p, gd_model_t *m, gd_game_t *g, const gd_play_io_t *io);
// 开始一局(模型已切到 PLAY 页,model->level / model->practice 决定关卡与模式)。
void gd_play_start(gd_play_t *p, int64_t now_us);
// 跳跃键按下 / 松开,song_ms 是按键时刻的歌曲时间(GD_NO_CLOCK = 时钟未就绪,只更新按键状态)。
void gd_play_key(gd_play_t *p, bool down, int32_t song_ms);
// 练习模式放置检查点。
void gd_play_checkpoint(gd_play_t *p, int32_t song_ms);
// 每帧调用,song_ms 为当前歌曲时间。返回需要应用继续处理的 GD_FX_*(进入结算页时)。
uint32_t gd_play_frame(gd_play_t *p, int64_t now_us, int32_t song_ms);
// 暂停菜单的操作。
void gd_play_pause(gd_play_t *p, bool paused, int64_t now_us);
void gd_play_restart(gd_play_t *p, int64_t now_us);
void gd_play_set_practice(gd_play_t *p, int64_t now_us);
// 离开这一局(返回选关或进入结算后),把未计入的统计写进模型。
void gd_play_stop(gd_play_t *p);
// 歌曲时间 → 物理步(含音画偏移与输入宽限)。
uint32_t gd_play_tick_at(const gd_play_t *p, int32_t song_ms);
