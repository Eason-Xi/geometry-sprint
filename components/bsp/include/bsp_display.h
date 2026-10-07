// components/bsp/include/bsp_display.h
// ST7789P3 240x320 显示:SPI 面板初始化 + 厂商专属寄存器 + LEDC 背光调光。
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

// The final LVGL frame is masked to this radius; pixels outside are pure black.
#define BSP_LVGL_SCREEN_RADIUS 30

// 初始化 SPI 总线、面板、厂商寄存器、背光 LEDC。成功调用可重复；失败会回滚本次
// 已创建的显示资源，修正故障后可重试。成功后屏幕已上电但内容未定。
esp_err_t bsp_display_init(void);

// 取底层面板句柄。想直接 esp_lcd_panel_draw_bitmap 画,或接 LVGL 以外的 GUI 时用。
// 未初始化返回 NULL。
esp_lcd_panel_handle_t bsp_display_panel(void);

// 取底层 panel io 句柄(LVGL 接入需要)。未初始化返回 NULL。
esp_lcd_panel_io_handle_t bsp_display_io(void);

// 背光亮度 0..100(%)。LEDC PWM,0=全灭。
void bsp_display_backlight(uint8_t percent);

// deep sleep 专用：关闭显示、让 ST7789 进入 Sleep In，停止背光 PWM，
// 将 CS/SCLK/MOSI/DC/背光设为安全电平并在 deep sleep 中保持。调用时必须
// 已阻止 LVGL 刷屏，调用后必须立即进入 deep sleep 或重启。
esp_err_t bsp_display_prepare_deep_sleep(void);

// ---------------------------------------------------------------------------
// LVGL 接入(可选层)。必须先 bsp_display_init() 成功后再调。
// 不想用 LVGL 的开发者可忽略本段,直接用 bsp_display_panel() 自己画。
// ---------------------------------------------------------------------------
// 前向声明:LVGL 里 lv_display_t 是 `typedef struct _lv_display_t lv_display_t;`,
// 故此处用 struct 形式即可,避免本头文件强行 include lvgl.h。
struct _lv_display_t;

// 启动 LVGL 与其渲染任务,返回 lv_display_t*。失败返回 NULL；display/回调注册失败
// 回滚 display 并保留已初始化的 port，后续可重试。port 自身部分初始化失败需重启，
// 不覆盖依赖中可能尚未退出的任务。首次初始化由单一任务串行调用。
struct _lv_display_t *bsp_lvgl_init(void);

// 屏幕逻辑方向。横屏两种握法按三个侧键所在的边区分,旋转值见 bsp_pins.h。
typedef enum {
    BSP_LVGL_PORTRAIT = 0,           // 240 × 320,挂绳孔在上(上电默认)
    BSP_LVGL_LANDSCAPE_KEYS_TOP,     // 320 × 240,侧键在顶边、挂绳孔在左
    BSP_LVGL_LANDSCAPE_KEYS_BOTTOM,  // 320 × 240,侧键在底边、挂绳孔在右
} bsp_lvgl_orientation_t;

// 切换 LVGL 逻辑方向:由面板 MADCTL 完成旋转,不需要额外缓冲。圆角遮罩自动
// 跟随逻辑分辨率。须在 bsp_lvgl_init() 成功后、构建页面之前调用(先建页面再旋转,
// 布局会按旧尺寸计算);内部短暂持有 LVGL 锁,调用方不得在 LVGL 任务中调用。
// LVGL 未就绪返回 ESP_ERR_INVALID_STATE,参数非法返回 ESP_ERR_INVALID_ARG,
// 取锁超时返回 ESP_ERR_TIMEOUT。
esp_err_t bsp_lvgl_set_orientation(bsp_lvgl_orientation_t orientation);

// LVGL 非线程安全:在【非 LVGL 任务】里操作任何 lv_* 对象前后必须加解锁。
// LVGL 尚未就绪或超时时 lock 返回 false；只有 lock 成功后才调用 unlock。
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
