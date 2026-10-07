// tests/test_gd_model.c —— 页面导航、暂停/设置菜单、结算与存档格式的主机测试(PRD §4、§5、§10)。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gd_model.h"

#define K(m, k, e) gd_model_key((m), (k), (e), 0)

static void test_navigation(void) {
    gd_model_t m;
    gd_model_init(&m);
    assert(m.screen == GD_SCR_TITLE);
    // 确定键:PRESS 不动作,CLICK 进入选关(一次按下只触发一次)
    assert(K(&m, GD_KEY_OK, GD_KEV_PRESS) == 0);
    assert(K(&m, GD_KEY_OK, GD_KEV_CLICK) & GD_FX_SCREEN);
    assert(m.screen == GD_SCR_SELECT && m.level == 0);
    // 上/下键:PRESS 动作,随后的 CLICK 不再动作
    assert(K(&m, GD_KEY_UP, GD_KEV_PRESS) == 0);                 // 已在第一关
    assert(K(&m, GD_KEY_DOWN, GD_KEV_PRESS) & GD_FX_REFRESH);
    assert(K(&m, GD_KEY_DOWN, GD_KEV_CLICK) == 0);
    assert(m.level == 1);
    for (int i = 0; i < 10; i++) K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.level == GD_LEVEL_COUNT - 1);
    // 长按确定返回标题
    assert(K(&m, GD_KEY_OK, GD_KEV_LONG) & GD_FX_SND_BACK);
    assert(m.screen == GD_SCR_TITLE);
    // 标题:左键设置、中键统计
    K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert(m.screen == GD_SCR_SETTINGS);
    K(&m, GD_KEY_OK, GD_KEV_LONG);
    assert(m.screen == GD_SCR_TITLE);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.screen == GD_SCR_STATS);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(m.screen == GD_SCR_TITLE);
}

static void test_play_and_pause(void) {
    gd_model_t m;
    gd_model_init(&m);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    uint32_t fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(fx & GD_FX_START);
    assert(m.screen == GD_SCR_PLAY && m.level == 1 && !m.practice && !m.paused);
    // 游戏中按键不经过状态机
    assert(K(&m, GD_KEY_OK, GD_KEV_PRESS) == 0 && K(&m, GD_KEY_DOWN, GD_KEV_PRESS) == 0);
    fx = gd_model_pause(&m);
    assert((fx & GD_FX_PAUSE) && (fx & GD_FX_SAVE) && m.paused && m.pause_cursor == GD_PAUSE_RESUME);
    assert(gd_model_pause(&m) == 0);
    // 菜单循环移动
    K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert(m.pause_cursor == GD_PAUSE_EXIT);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.pause_cursor == GD_PAUSE_RESUME);
    // 长按确定 = 继续
    fx = K(&m, GD_KEY_OK, GD_KEV_LONG);
    assert((fx & GD_FX_RESUME) && !m.paused);
    // 练习模式开关
    gd_model_pause(&m);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert((fx & GD_FX_PRACTICE) && m.practice && !m.paused);
    // 重新开始
    gd_model_pause(&m);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert((fx & GD_FX_RESTART) && !m.paused && m.practice);
    // 返回选关
    gd_model_pause(&m);
    K(&m, GD_KEY_UP, GD_KEV_PRESS);
    fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert((fx & GD_FX_STOP) && (fx & GD_FX_SCREEN) && m.screen == GD_SCR_SELECT && !m.paused);
}

static void test_progress_and_result(void) {
    gd_model_t m;
    gd_model_init(&m);
    m.screen = GD_SCR_PLAY;
    m.level = 2;
    gd_model_attempt(&m);
    gd_model_attempt(&m);
    assert(m.save.lv[2].attempts == 2);
    assert(gd_model_died(&m, 37) & GD_FX_SAVE);
    assert(m.save.lv[2].best == 37 && m.new_best == 37);
    assert(gd_model_died(&m, 20) == 0 && m.new_best == -1);
    assert(gd_model_died(&m, 250) & GD_FX_SAVE);
    assert(m.save.lv[2].best == 99);
    // 练习模式的进度单独记录
    m.practice = true;
    gd_model_died(&m, 80);
    assert(m.save.lv[2].best_practice == 80 && m.save.lv[2].best == 99);
    gd_model_finished(&m, 5, 40, 90000);
    assert(m.save.lv[2].practice_done && !m.save.lv[2].completed && !m.result.first_clear);
    // 正式通关
    m.practice = false;
    gd_model_finished(&m, 12, 300, 90000);
    assert(m.save.lv[2].completed && m.save.lv[2].best == 100 && m.result.first_clear);
    assert(m.result.attempts == 12 && m.result.jumps == 300);
    uint32_t fx = gd_model_show_result(&m);
    assert((fx & GD_FX_STOP) && m.screen == GD_SCR_RESULT);
    gd_model_finished(&m, 1, 1, 1);
    assert(!m.result.first_clear);
    // 结算:左键再玩一次,右键返回选关
    fx = K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert((fx & GD_FX_START) && m.screen == GD_SCR_PLAY && !m.practice);
    m.screen = GD_SCR_RESULT;
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(m.screen == GD_SCR_SELECT);
    gd_model_add_stats(&m, 7, 30);
    assert(m.save.lv[2].jumps == 7 && m.save.play_seconds == 30);
}

static void test_settings(void) {
    gd_model_t m;
    gd_model_init(&m);
    m.screen = GD_SCR_SETTINGS;
    // 音量:进入编辑,上键减、下键增,夹在 0..10
    uint32_t fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(m.set_editing);
    for (int i = 0; i < 20; i++) K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.save.set.volume == GD_VOLUME_MAX);
    fx = K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert((fx & GD_FX_VOLUME) && m.save.set.volume == GD_VOLUME_MAX - 1);
    fx = K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(!m.set_editing && (fx & GD_FX_SAVE));
    // 开关项
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(!m.save.set.sfx);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(!m.save.set.progress_bar);
    // 亮度
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    fx = K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert((fx & GD_FX_BRIGHT) && m.save.set.brightness == GD_BRIGHT_LEVELS - 2);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    // 音画偏移 ±100,步进 10
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.set_cursor == GD_SET_OFFSET);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    for (int i = 0; i < 15; i++) K(&m, GD_KEY_UP, GD_KEV_PRESS);
    assert(m.save.set.offset_ms == -GD_OFFSET_LIMIT);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.save.set.offset_ms == -GD_OFFSET_LIMIT + GD_OFFSET_STEP);
    K(&m, GD_KEY_OK, GD_KEV_LONG);   // 编辑中长按:结束编辑,不离开页面
    assert(!m.set_editing && m.screen == GD_SCR_SETTINGS);
    // 关于:打开后任意键关闭
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.set_cursor == GD_SET_ABOUT);
    K(&m, GD_KEY_OK, GD_KEV_CLICK);
    assert(m.about_open);
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(!m.about_open && m.set_cursor == GD_SET_ABOUT);
    // 循环移动
    K(&m, GD_KEY_DOWN, GD_KEV_PRESS);
    assert(m.set_cursor == GD_SET_VOLUME);
    fx = K(&m, GD_KEY_OK, GD_KEV_LONG);
    assert(m.screen == GD_SCR_TITLE && (fx & GD_FX_SAVE));
}

static void test_clear_hold(void) {
    gd_model_t m;
    gd_model_init(&m);
    m.save.lv[0].best = 55;
    m.save.lv[0].attempts = 9;
    m.save.set.volume = 3;
    m.screen = GD_SCR_SETTINGS;
    m.set_cursor = GD_SET_CLEAR;
    // 按住不足 1.5 秒松开:不清除
    gd_model_key(&m, GD_KEY_OK, GD_KEV_PRESS, 1000);
    assert(gd_model_tick(&m, 2000) == 0);
    assert(gd_model_key(&m, GD_KEY_OK, GD_KEV_LONG, 1500) == 0);   // 期间的 LONG 不返回
    assert(m.screen == GD_SCR_SETTINGS);
    gd_model_key(&m, GD_KEY_OK, GD_KEV_RELEASE, 2200);
    assert(gd_model_tick(&m, 5000) == 0 && m.save.lv[0].best == 55);
    // 按住 1.5 秒:清除进度,保留设置
    gd_model_key(&m, GD_KEY_OK, GD_KEV_PRESS, 6000);
    assert(gd_model_tick(&m, 7499) == 0);
    const uint32_t fx = gd_model_tick(&m, 7500);
    assert(fx & GD_FX_SAVE);
    assert(m.save.lv[0].best == 0 && m.save.lv[0].attempts == 0 && m.save.set.volume == 3);
    assert(m.cleared_at == 7500 && !m.clear_holding);
}

static void test_save_format(void) {
    gd_save_t s, d;
    gd_save_defaults(&s);
    assert(s.set.volume == GD_VOLUME_DEFAULT && s.set.sfx && s.set.progress_bar);
    s.lv[0].best = 42;
    s.lv[1].completed = true;
    s.lv[1].best = 100;
    s.lv[5].best_practice = 77;
    s.lv[5].attempts = 123456;
    s.lv[5].jumps = 0xABCDEF;
    s.play_seconds = 3600;
    s.set.volume = 9;
    s.set.offset_ms = -40;
    s.hint_shown = true;
    uint8_t buf[GD_SAVE_BYTES];
    assert(gd_save_encode(&s, buf) == GD_SAVE_BYTES);
    assert(gd_save_decode(buf, sizeof buf, &d));
    assert(memcmp(&s, &d, sizeof s) == 0);
    // CRC 错、长度错、版本错:返回默认值
    buf[5] ^= 1;
    assert(!gd_save_decode(buf, sizeof buf, &d) && d.lv[0].best == 0 && d.set.volume == GD_VOLUME_DEFAULT);
    buf[5] ^= 1;
    assert(!gd_save_decode(buf, sizeof buf - 1, &d));
    assert(!gd_save_decode(NULL, 0, &d));
    // 越界字段被钳位(重算 CRC 后仍可读)
    gd_save_t bad = s;
    bad.lv[0].best = 250;
    bad.set.volume = 99;
    bad.set.brightness = 9;
    bad.set.offset_ms = 777;
    gd_save_encode(&bad, buf);
    assert(gd_save_decode(buf, sizeof buf, &d));
    assert(d.lv[0].best == 100 && d.set.volume == GD_VOLUME_MAX && d.set.brightness == GD_BRIGHT_LEVELS - 1);
    assert(d.set.offset_ms == GD_OFFSET_LIMIT);
    buf[0] = 9;
    assert(!gd_save_decode(buf, sizeof buf, &d));
    // 清除进度保留设置
    gd_save_clear_progress(&s);
    assert(s.lv[1].completed == false && s.play_seconds == 0 && s.set.volume == 9 && !s.hint_shown);
    assert(gd_save_backlight(0) < gd_save_backlight(1) && gd_save_backlight(2) == 100 && gd_save_backlight(7) == 100);
}

int main(void) {
    test_navigation();
    test_play_and_pause();
    test_progress_and_result();
    test_settings();
    test_clear_hold();
    test_save_format();
    printf("Geometry Sprint model tests: PASS\n");
    return 0;
}
