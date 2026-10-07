// main/main.c —— 几何冲刺固件入口:初始化 BSP、切到横屏(侧键朝上),交给 gd_app。
//
// 显示与 LVGL 是硬依赖(失败就无法使用);音频、电量计、存档与音乐包是软依赖:
// 没有音频时游戏仍可无声游玩(时钟按定时器推进),没有音乐包时静音游玩,没有电量计时不显示电量。
// 启动直接进入几何冲刺,不经过基线的硬件测试菜单(那些 demo_*.c 只留给主机测试,不进固件)。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

#include "gd_app.h"
#include "gd_fonts.h"
#include "gd_music.h"
#include "gd_store.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "几何冲刺启动");
    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    // 先旋转再建界面:页面布局按 320 × 240 计算。
    const esp_err_t oe = bsp_lvgl_set_orientation(BSP_LVGL_LANDSCAPE_KEYS_TOP);
    if (oe != ESP_OK) ESP_LOGE(TAG, "切换横屏失败:%s", esp_err_to_name(oe));
    if (bsp_lvgl_lock(1000)) {
        const int missing = gd_fonts_selfcheck();
        if (missing) ESP_LOGE(TAG, "字库缺 %d 个字形,界面可能出现方框", missing);
        bsp_lvgl_unlock();
    }

    const esp_err_t audio = bsp_audio_init();
    if (audio != ESP_OK) ESP_LOGW(TAG, "音频初始化失败:%s,将无声运行", esp_err_to_name(audio));
    const esp_err_t battery = bsp_battery_init();
    if (battery != ESP_OK) ESP_LOGW(TAG, "电量计不可用:%s", esp_err_to_name(battery));
    gd_store_init();
    gd_music_init();

    if (!gd_app_start(audio == ESP_OK, battery == ESP_OK)) {
        ESP_LOGE(TAG, "应用启动失败");
        return;
    }
    gd_music_verify_async();
}
