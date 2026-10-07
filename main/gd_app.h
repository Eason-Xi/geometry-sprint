// main/gd_app.h —— 应用任务:按键分发、对局帧、存档与电源管理(ESP-IDF)。
#pragma once

#include <stdbool.h>

// 须在显示、LVGL(已切到横屏)、NVS 与音乐包初始化之后调用。
bool gd_app_start(bool audio_ok, bool battery_ok);
