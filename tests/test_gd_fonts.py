#!/usr/bin/env python3
"""几何冲刺字体资产与应用契约测试:字形覆盖、负例、过期检测、字面量约束、启动不进基线测试界面。"""

from __future__ import annotations

import importlib.util
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_gd_fonts", ROOT / "tools" / "gen_gd_fonts.py")
gen = importlib.util.module_from_spec(SPEC)
sys.modules["gen_gd_fonts"] = gen
SPEC.loader.exec_module(gen)

TEXT_FONTS = ("gd_zh14", "gd_zh18", "gd_zh24")


class CharsetTest(unittest.TestCase):
    def test_committed_assets_pass_check(self):
        self.assertEqual(gen.run_check(), [])

    def test_every_text_font_covers_every_ui_string(self):
        needed = set(gen.text_charset())
        for name in TEXT_FONTS:
            covered = gen.parse_font_codepoints(gen.FONT_DIR / f"{name}.c")
            self.assertTrue(needed <= covered, name)

    def test_display_font_covers_titles(self):
        covered = gen.parse_font_codepoints(gen.FONT_DIR / "gd_zh40.c")
        for text in ("几何冲刺", "关卡完成!", "练习通关!", "暂停", "0123456789%"):
            self.assertTrue({ord(c) for c in text} <= covered, text)

    def test_known_missing_glyph_is_absent(self):
        # 负例:U+9F98(龘)不在界面文字里,也不应出现在任何字体中,保证覆盖检查不会无条件通过。
        for name in (*TEXT_FONTS, "gd_zh40"):
            covered = gen.parse_font_codepoints(gen.FONT_DIR / f"{name}.c")
            self.assertNotIn(0x9F98, covered)

    def test_charset_contains_core_ui_text(self):
        chars = {chr(p) for p in gen.text_charset()}
        for text in ("选择关卡", "第 1 次尝试", "练习模式:开", "新纪录", "按住 确定 1.5 秒", "未内置音乐(静音游玩)",
                     "◀", "▶", "★", "◆", "…", "·", "Neon Takeoff"):
            self.assertTrue(set(text) <= chars, text)

    def test_glyph_header_matches(self):
        self.assertEqual(gen.GLYPH_HEADER.read_text(encoding="utf-8"), gen.render_glyph_header())

    def test_no_stray_literals_in_app_sources(self):
        self.assertEqual(gen.stray_literals(), [])


class AppContractTest(unittest.TestCase):
    """二次开发 UI 必须重新设计:固件启动与编译都不能再进入基线测试菜单 / ui_pixel 外壳。"""

    def test_firmware_compiles_only_app_sources(self):
        lines = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8").splitlines()
        cmake = "\n".join(line.split("#", 1)[0] for line in lines)     # 只看代码,不看注释
        self.assertIn('file(GLOB GD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/gd_*.c")', cmake)
        self.assertNotRegex(cmake, r"demo_\w+\.c|ui_pixel")

    def test_entry_and_app_do_not_use_baseline_ui(self):
        for path in [ROOT / "main" / "main.c", *sorted((ROOT / "main").glob("gd_*.[ch]"))]:
            text = path.read_text(encoding="utf-8")
            self.assertNotRegex(text, r'#include\s+"(demo|ui_pixel|demo_navigation)[^"]*"', path.name)
            self.assertNotIn("ui_pixel_", text, path.name)

    def test_startup_sets_landscape_before_building_ui(self):
        main = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
        rot = main.index("bsp_lvgl_set_orientation(BSP_LVGL_LANDSCAPE_KEYS_TOP)")
        start = main.index("gd_app_start(")
        self.assertLess(rot, start)

    def test_button_callback_only_enqueues(self):
        app = (ROOT / "main" / "gd_app.c").read_text(encoding="utf-8")
        body = re.search(r"static void on_key\([^)]*\)\s*\{(.*?)\n\}", app, flags=re.S).group(1)
        self.assertIn("xQueueSend", body)
        for forbidden in ("lv_", "bsp_lvgl_lock", "gd_audio_", "gd_play_", "gd_game_", "vTaskDelay"):
            self.assertNotIn(forbidden, body)

    def test_nvs_writes_wait_for_quiet_audio(self):
        # PRD §8.4:只在音乐停止时写 NVS
        app = (ROOT / "main" / "gd_app.c").read_text(encoding="utf-8")
        self.assertRegex(app, r"s_save_pending && gd_audio_quiet\(\)")
        self.assertEqual(app.count("gd_store_save("), 1)


if __name__ == "__main__":
    unittest.main()
