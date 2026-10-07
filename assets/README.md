<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

Geometry Sprint font subsets:

| File | Size and weight | Character set |
| --- | --- | --- |
| `fonts/gd_zh14.c`, `fonts/gd_zh18.c` | 14 / 18 px, Bold, 4 bpp | Every character in `main/gd_strings.h`, printable ASCII, and UI symbols (245) |
| `fonts/gd_zh24.c` | 24 px, Heavy, 4 bpp | Same as above |
| `fonts/gd_zh40.c` | 40 px, Heavy, 4 bpp | Title, level-complete and pause banners, and digits (27) |

The source is Source Han Sans SC 2.005 (Adobe, SIL Open Font License 1.1). `tools/gen_gd_fonts.py generate` runs
`lv_font_conv` 1.5.3; the commands, code point ranges, and SHA-256 hashes are recorded in
`fonts/gd_fonts.manifest.json`, and `tools/gen_gd_fonts.py check` verifies the generated files are current.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/geometry-sprint-preview.png`](images/geometry-sprint-preview.png) | 992 × 752, PNG RGB | Overview of nine Geometry Sprint screens for both README files. Rendered on the host from the firmware UI code with the real LVGL by `tools/render_gd_preview.py --sheet`; all artwork is original. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

Geometry Sprint's six soundtrack pieces are all original: `tools/gd_music_synth.py` composes and synthesizes them
deterministically from the parameters in `levels/tracks.json`, and `tools/gen_gd_music.py` encodes them as 16 kHz mono
IMA-ADPCM in the ignored `build/gd_music/gd_music.bin`, which the build merges into the `music` partition of the full
image. The soundtrack ships with the firmware under the repository license. Sound effects are synthesized at runtime
by `main/gd_sfx.c` and take no asset storage.

## Levels

`levels/level_1.txt` … `levels/level_6.txt` are Geometry Sprint's six original levels (choreographed to each
track's beat, not copies of the official levels); the format is described in `tools/gen_gd_levels.py`.
`tools/gen_gd_levels.py generate` compiles them into `main/gd_levels_data.c` and uses the host solver to produce the
completion replays in `main/gd_replays_data.c`; SHA-256 hashes of the levels, physics sources, and generated files
are recorded in `levels/levels.manifest.json`. `levels/tracks.json` holds the composition parameters of the six original
pieces (title, BPM, first beat, mode, chord progression, timbre, seed), no audio.
