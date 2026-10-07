<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 几何冲刺(Geometry Sprint)

把 FoloToy AI Passport 横过来,变成一台口袋里的节奏跑酷机。玩法致敬《Geometry Dash》:
方块随着音乐自动向前冲,你只负责**跳**。越过尖刺、踩跳板、点跳环、开飞船、翻转重力,
死了立刻重来,直到把一关打到 100%。6 个原创关卡,全程离线。

<p align="center">
  <img src="assets/images/geometry-sprint-preview.png" alt="几何冲刺的九个界面:标题、选关、开局提示、飞船段、重力翻转、死亡与新纪录、暂停菜单、设置与结算。" width="100%">
</p>

> 截图由 `tools/render_gd_preview.py` 在电脑上用真实 LVGL 渲染,实机色彩与刷新以设备为准。
> 这是非官方致敬作品;"Geometry Dash" 是 RobTop Games 的商标。

需求与验收标准见 [需求文档](docs/geometry-dash-prd.zh_CN.md)。

## 握法与按键

横屏握持,**三个侧键朝上**(设备逆时针转 90°,挂绳孔在左)。顶边从左到右依次是
**上 / 下 / 确定**(下文称左键 / 中键 / 右键)。菜单页顶部的小标签始终对准对应的实体键;游戏中隐藏标签。

| 页面 | 左键 | 中键 | 右键 |
| --- | --- | --- | --- |
| 标题 | 设置 | 统计 | 开始(进入选关) |
| 选关 | 上一关 | 下一关 | 单击开始;长按返回标题 |
| 游戏中 | 跳跃 / 飞行,按住连跳 | 暂停 | 跳跃 / 飞行,按住连跳 |
| 练习模式 | 跳跃 / 飞行 | 按下放检查点;长按暂停 | 跳跃 / 飞行 |
| 暂停菜单 | 上移 | 下移 | 执行;长按 = 继续 |
| 结算 | 再玩一次 | — | 返回选关 |
| 设置 | 上移(调值时减少) | 下移(调值时增加) | 进入 / 确认;长按返回 |

## 六个关卡

| # | 关卡(曲目) | 难度 | 新机制 |
| --- | --- | --- | --- |
| 1 | Neon Takeoff | 简单 | 方块跳跃、平台、第一段飞船 |
| 2 | Pad Runner | 简单 | 黄色跳板 |
| 3 | Orb Drift | 普通 | 黄色跳环、连跳环 |
| 4 | Upside Dune | 普通 | 重力门、蓝色跳环 |
| 5 | Base Breaker | 困难 | 三连刺、蓝色跳板、重力翻转 |
| 6 | Final Pulse | 困难 | 综合:连跳环、重力切换、窄飞船通道 |

每关约 90 秒,障碍按各曲节拍编排。主机上的求解器证明每关都能通关,并测量每次起跳的容错窗口:
简单关最窄 141 ms,普通关最窄 54 ms,困难关最窄 58 ms(中位数 150–250 ms)。

- **练习模式**:暂停菜单里开启。稳定着地 2 秒自动放检查点,中键随时手动放;死亡后回到最近的检查点,
  音乐跳到对应位置接着播。普通与练习的最佳进度分开记录。
- **存档**:每关最佳进度、是否通关、尝试与跳跃次数、累计时长和设置项保存在 NVS,断电不丢失。
- **设置**:音乐音量(10 档)、音效开关、进度条显示、屏幕亮度(3 档)、音画偏移(±100 ms)、清除存档
  (按住确定 1.5 秒)、关于。

## 原创配乐

6 首配乐全部原创:`tools/gd_music_synth.py` 按 [`assets/levels/tracks.json`](assets/levels/tracks.json) 的参数
(BPM 与首拍和关卡一致,外加调式、和弦进行、音色与随机种子)作曲合成,有电子鼓组、贝斯、主旋律、琶音与铺底和弦,
按"前奏、主歌、高潮、间奏"编排。死亡、通关、检查点与菜单音效在设备上实时合成。

```bash
python3 tools/gen_gd_music.py
```

需要 numpy。它把 6 首曲子编码成 16 kHz 单声道 IMA-ADPCM,写出 `build/gd_music/gd_music.bin`(约 4.58 MB,
占 music 分区 73%);加 `--wav-dir <目录>` 可同时导出 WAV 试听。随后构建固件时,音乐包会自动写入 `music` 分区
并并入合并镜像;没有音乐包时照常构建,游戏以静音模式运行,选关卡片会提示"未内置音乐"。

## 构建、测试与烧录

需要已激活的 ESP-IDF 5.5.3(见 [环境准备](docs/development/engineering/environment-setup.zh_CN.md))。

```bash
./tools/validate.sh
```

完整门禁包含仓库检查、全部主机测试(物理、会话、关卡可通关证明、音乐包与音效、存档与状态机、
对局控制器、字库与工具链)、固件构建与合并镜像校验。产物 `build/FoloToy-AI-Passport-full.bin` 从 `0x0` 烧录:

```bash
python -m esptool --chip esp32c3 -p <端口> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

分区布局:`factory` 应用 2 MB(`0x10000`),`music` 数据分区约 5.94 MB(`0x210000`)。
合并烧录会覆盖 NVS,已存进度与设置会被重置,详见 [烧录与数据说明](docs/development/engineering/firmware-layout.zh_CN.md#烧录与已存数据)。

## 开发辅助

| 命令 | 作用 |
| --- | --- |
| `python3 tools/gen_gd_levels.py generate` | 关卡 ASCII → `main/gd_levels_data.c`,并用主机求解器生成通关录像 `main/gd_replays_data.c` |
| `python3 tools/gen_gd_levels.py ruler` | 按 BPM 与首拍重写关卡文件里的节拍标尺 |
| `python3 tools/gen_gd_levels.py render` | 把每关渲染成 PNG 长图(`build/levels/`) |
| `python3 tools/render_gd_preview.py` | 主机上用真实 LVGL 渲染全部页面,6 关自动通关;检查局部刷新与整屏重画逐像素一致、LVGL 内存池峰值,统计每帧推送像素量 |
| `python3 tools/gen_gd_fonts.py generate --lv-font-conv … --font-dir …` | 修改 `main/gd_strings.h` 后重新生成中文字库子集 |
| `GD_AUTOPLAY=1 ./tools/validate.sh --firmware` | 构建"自动游玩"调试固件:用通关录像自动玩,串口每秒输出 `perf` 行(帧率、刷新耗时、堆、音频欠载) |

关卡文件格式见 `tools/gen_gd_levels.py` 的说明;物理参数集中在 `main/gd_level.h`,改动后必须重新运行
`generate`(门禁会检查录像是否过期)。

## 代码结构

| 文件 | 内容 |
| --- | --- |
| `main/gd_level.*`、`gd_sim.*`、`gd_game.*` | 坐标与物理常量、240 Hz 整数定点物理与碰撞、一局的会话(输入队列、追步、尝试、练习检查点) |
| `main/gd_play.*` | 对局控制器:歌曲时钟 → 物理,死亡重开、通关结算、统计(纯 C,主机与预览共用) |
| `main/gd_model.*`、`gd_save.*` | 页面导航与菜单状态机、存档格式 |
| `main/gd_mpack.*`、`gd_sfx.*` | 音乐包格式与 IMA-ADPCM 解码、音效合成器 |
| `main/gd_gfx.*`、`gd_ui*.c`、`gd_theme.h`、`gd_strings.h`、`gd_fonts.*` | 自绘画布与脏矩形、各页面、配色、文案与字库自检 |
| `main/gd_audio.*`、`gd_music.*`、`gd_store.*`、`gd_app.*`、`main.c` | 音频任务与歌曲时钟、音乐分区、NVS、应用任务与入口(依赖 ESP-IDF) |
| `assets/levels/` | 6 个原创关卡、曲目映射表与生成清单 |
| `components/bsp` | 新增横屏接口 `bsp_lvgl_set_orientation()`、侧键位置表、按键松开事件 `BSP_BTN_RELEASE`、音频 DMA 常量 |

固件只编译 `main.c` 与 `gd_*.c`,启动直接进入几何冲刺;基线的 `demo_*.c` 测试页面仍留在仓库里供主机测试使用,但不进固件。

## 验证状态

- 已通过:`./tools/validate.sh --static`(全部主机测试)与 `./tools/validate.sh --firmware`
  (应用 860 KB / 2 MB,合并镜像含音乐包 6.74 MB)。
- 主机预览:6 关自动通关;局部刷新与整屏重画 0 像素差异;LVGL 内存池峰值约 9 KB / 40 KB;
  游戏中平均每帧只需推送约 33% 的屏幕像素。
- 真机测试:2026-10-07 开发者烧录交付的合并固件(SHA-256 `b2094002…be6437`)后在设备上完成测试,结果通过。
- 换成原创配乐后的固件(只改了音乐包、关卡名与"关于"文案)尚未重新上机试听。
- 仍未覆盖:测试为整体验收,帧率、内存与音频欠载的串口 `perf` 数据未逐项记录;侧键朝下的横屏方向
  (`BSP_LVGL_LANDSCAPE_KEYS_BOTTOM`)本应用未使用,未经实机验证。
