// tools/gd_preview/lv_conf.h —— 主机预览用的 LVGL 最小配置。
//
// 与固件(sdkconfig.defaults)保持一致:16 位色、内置内存池、20 ms 刷新周期、关闭默认主题、
// 保留缺字占位框。主机是 64 位,对象和绘制任务里的指针更大,测得的池占用比板上偏高(偏保守)。
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#ifndef GD_PREVIEW_MEM_KB
#define GD_PREVIEW_MEM_KB 40
#endif
#define LV_MEM_SIZE (GD_PREVIEW_MEM_KB * 1024U)
#define LV_DEF_REFR_PERIOD 20
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1

#endif /* LV_CONF_H */
