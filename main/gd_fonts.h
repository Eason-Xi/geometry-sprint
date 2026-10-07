// main/gd_fonts.h —— 应用字体(思源黑体子集,由 tools/gen_gd_fonts.py 生成)。
//
// gd_zh14 / gd_zh18(Bold)、gd_zh24(Heavy):gd_strings.h 全部字符 + 可打印 ASCII + 界面符号。
// gd_zh40(Heavy):标题、关卡完成与暂停大字,外加数字与 "%"。
// 所有文字必须显式使用这些字体;默认 Montserrat 不含中文。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(gd_zh14)
LV_FONT_DECLARE(gd_zh18)
LV_FONT_DECLARE(gd_zh24)
LV_FONT_DECLARE(gd_zh40)

#define GD_FONT_SMALL   (&gd_zh14)
#define GD_FONT_BODY    (&gd_zh18)
#define GD_FONT_HEAD    (&gd_zh24)
#define GD_FONT_DISPLAY (&gd_zh40)

// 启动自检:逐个码点检查各字体是否真的含有需要的字形(占位框视为缺失)。
// 须在 LVGL 初始化后调用;返回缺失字形数并写日志。
int gd_fonts_selfcheck(void);
