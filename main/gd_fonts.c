// main/gd_fonts.c —— 字形覆盖自检(固件启动时与主机预览中都会运行)。
#include "gd_fonts.h"

#include "gd_font_glyphs.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "gd_fonts";
#define REPORT(...) ESP_LOGE(TAG, __VA_ARGS__)
#define REPORT_OK(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define REPORT(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define REPORT_OK(...) ((void)0)
#endif

static int check(const lv_font_t *font, const char *name, const uint32_t *points, int count) {
    int missing = 0;
    for (int i = 0; i < count; i++) {
        lv_font_glyph_dsc_t dsc;
        const bool found = lv_font_get_glyph_dsc(font, &dsc, points[i], 0);
        if (!found || dsc.is_placeholder) {
            REPORT("%s 缺字形 U+%04X", name, (unsigned)points[i]);
            missing++;
        }
    }
    return missing;
}

int gd_fonts_selfcheck(void) {
    int missing = 0;
    missing += check(&gd_zh14, "gd_zh14", GD_GLYPHS_TEXT, GD_GLYPHS_TEXT_COUNT);
    missing += check(&gd_zh18, "gd_zh18", GD_GLYPHS_TEXT, GD_GLYPHS_TEXT_COUNT);
    missing += check(&gd_zh24, "gd_zh24", GD_GLYPHS_TEXT, GD_GLYPHS_TEXT_COUNT);
    missing += check(&gd_zh40, "gd_zh40", GD_GLYPHS_DISPLAY, GD_GLYPHS_DISPLAY_COUNT);
    if (missing == 0) {
        REPORT_OK("字形自检通过:正文 %d 个码点 × 3 个字号,展示字 %d 个", GD_GLYPHS_TEXT_COUNT,
                  GD_GLYPHS_DISPLAY_COUNT);
    }
    return missing;
}
