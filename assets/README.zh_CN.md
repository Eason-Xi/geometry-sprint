<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

几何冲刺的字库子集:

| 文件 | 字号与字重 | 字符集 |
| --- | --- | --- |
| `fonts/gd_zh14.c`、`fonts/gd_zh18.c` | 14 / 18 px,Bold,4 bpp | `main/gd_strings.h` 全部字符 + 可打印 ASCII + 界面符号(245 个) |
| `fonts/gd_zh24.c` | 24 px,Heavy,4 bpp | 同上 |
| `fonts/gd_zh40.c` | 40 px,Heavy,4 bpp | 标题、关卡完成、暂停大字与数字(27 个) |

来源为思源黑体 Source Han Sans SC 2.005(Adobe,SIL Open Font License 1.1),由
`tools/gen_gd_fonts.py generate` 调用 `lv_font_conv` 1.5.3 生成;命令、码点范围与 SHA-256 记录在
`fonts/gd_fonts.manifest.json`,`tools/gen_gd_fonts.py check` 校验生成物未过期。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/geometry-sprint-preview.png`](images/geometry-sprint-preview.png) | 992 × 752，PNG RGB | 几何冲刺九个界面的总览图,用于中英文 README。由 `tools/render_gd_preview.py --sheet` 在主机上用真实 LVGL 渲染固件界面代码生成,画面全部为原创绘制。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

几何冲刺的背景音乐是用户本地 `BGM/` 目录中的原曲(版权归原作者与 RobTop Games),**不提交到仓库**:
`tools/gen_gd_music.py` 按 `levels/tracks.json` 的匹配规则转码为 16 kHz、单声道 IMA-ADPCM,写到已被忽略的
`build/gd_music/gd_music.bin`,构建时并入合并镜像的 `music` 分区。带音乐的固件仅供个人设备使用,不得分发。
音效由 `main/gd_sfx.c` 实时合成,不占素材存储。

## 关卡(levels)

`levels/level_1.txt` … `levels/level_6.txt` 是几何冲刺的 6 个原创关卡(按各曲节拍编排,不复刻官方关卡),
格式见 `tools/gen_gd_levels.py`。`tools/gen_gd_levels.py generate` 把它们编译为 `main/gd_levels_data.c`,并用主机
求解器生成通关录像 `main/gd_replays_data.c`;关卡、物理源码与生成物的 SHA-256 记录在 `levels/levels.manifest.json`。
`levels/tracks.json` 只包含曲目文件名的匹配规则与截取参数,不含任何音频。
