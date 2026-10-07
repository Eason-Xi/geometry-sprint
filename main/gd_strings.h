// main/gd_strings.h —— 几何冲刺的全部界面文案。
//
// 这是界面代码中唯一允许出现非 ASCII 字符串字面量的文件:tools/gen_gd_fonts.py 从这里收集字符集
// 生成中文字库子集,check 子命令会拒绝其他界面文件里的中文字面量(PRD §11)。
// 改动文案后运行 tools/gen_gd_fonts.py generate 重新生成字库。
#pragma once

#define S_APP_NAME          "几何冲刺"
#define S_APP_SUB           "GEOMETRY SPRINT"

// —— 界面符号 ——
#define S_SYM_LEFT          "◀"
#define S_SYM_RIGHT         "▶"
#define S_SYM_STAR          "★"
#define S_SYM_DIAMOND       "◆"

// —— 顶边按键标签 ——
#define S_KEY_SETTINGS      "设置"
#define S_KEY_STATS         "统计"
#define S_KEY_START         "开始"
#define S_KEY_PREV          "上一关"
#define S_KEY_NEXT          "下一关"
#define S_KEY_UP            "上移"
#define S_KEY_DOWN          "下移"
#define S_KEY_OK            "确定"
#define S_KEY_AGAIN         "再玩"
#define S_KEY_BACK          "返回"
#define S_KEY_MINUS         "减少"
#define S_KEY_PLUS          "增加"
#define S_KEY_DONE          "完成"
#define S_KEY_CLOSE         "关闭"

// —— 标题 ——
#define S_TITLE_HINT        "按 开始 选择关卡"

// —— 选关 ——
#define S_SELECT_TITLE      "选择关卡"
#define S_LEVEL_FMT         "第 %d 关"
#define S_DIFF_EASY         "简单"
#define S_DIFF_NORMAL       "普通"
#define S_DIFF_HARD         "困难"
#define S_MODE_NORMAL       "普通"
#define S_MODE_PRACTICE     "练习"
#define S_CLEARED           "已通关"
#define S_NO_MUSIC          "未内置音乐(静音游玩)"
#define S_SELECT_HINT       "长按 开始 返回标题"

// —— 游戏中 ——
#define S_ATTEMPT_FMT       "第 %u 次尝试"
#define S_HINT_FIRST        "左/右键:跳跃 · 中键:暂停"
#define S_HINT_PRACTICE     "中键:检查点 · 长按中键:暂停"
#define S_PRACTICE_TAG      "练习"
#define S_NEW_BEST_FMT      "新纪录 %d%%"
#define S_COMPLETE          "关卡完成!"
#define S_PRACTICE_COMPLETE "练习通关!"

// —— 暂停 ——
#define S_PAUSE_TITLE       "暂停"
#define S_PAUSE_RESUME      "继续"
#define S_PAUSE_PRACTICE_ON "练习模式:开"
#define S_PAUSE_PRACTICE_OFF "练习模式:关"
#define S_PAUSE_RESTART     "重新开始"
#define S_PAUSE_EXIT        "返回选关"
#define S_PROGRESS_FMT      "进度 %d%%"
#define S_BEST_FMT          "最佳 %d%%"

// —— 结算 ——
#define S_RESULT_ATTEMPTS   "尝试"
#define S_RESULT_JUMPS      "跳跃"
#define S_RESULT_TIME       "用时"
#define S_COUNT_FMT         "%u 次"
#define S_FIRST_CLEAR       "首次通关"
#define S_MIN_SEC_FMT       "%u 分 %02u 秒"

// —— 设置 ——
#define S_SET_TITLE         "设置"
#define S_SET_VOLUME        "音乐音量"
#define S_SET_SFX           "音效"
#define S_SET_BAR           "进度条"
#define S_SET_BRIGHT        "屏幕亮度"
#define S_SET_OFFSET        "音画偏移"
#define S_SET_CLEAR         "清除存档"
#define S_SET_ABOUT         "关于"
#define S_ON                "开"
#define S_OFF               "关"
#define S_SHOW              "显示"
#define S_HIDE              "隐藏"
#define S_BRIGHT_LOW        "低"
#define S_BRIGHT_MID        "中"
#define S_BRIGHT_HIGH       "高"
#define S_OFFSET_FMT        "%+d 毫秒"
#define S_CLEAR_HINT        "按住 确定 1.5 秒"
#define S_CLEAR_HOLDING     "继续按住…"
#define S_CLEAR_DONE        "存档已清除"
#define S_OFFSET_HELP       "画面比音乐慢就调大"
#define S_ABOUT_TITLE       "关于"
#define S_ABOUT_1           "几何冲刺 · 非官方致敬作品"
#define S_ABOUT_2           "关卡、画面与配乐均为原创"
#define S_ABOUT_3           "配乐由程序作曲合成,随固件分发"
#define S_ABOUT_4           "Geometry Dash 是 RobTop Games 的商标"

// —— 统计 ——
#define S_STATS_TITLE       "统计"
#define S_STATS_ATTEMPTS    "总尝试"
#define S_STATS_JUMPS       "总跳跃"
#define S_STATS_CLEARED     "已通关"
#define S_STATS_TIME        "游戏时长"
#define S_CLEARED_FMT       "%d / %d 关"
#define S_HOUR_MIN_FMT      "%u 小时 %u 分"
#define S_MIN_FMT           "%u 分钟"
