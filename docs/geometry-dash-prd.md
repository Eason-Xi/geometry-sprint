<p align="right">
  <a href="geometry-dash-prd.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Geometry Sprint Product Requirements (PRD)

| Item | Content |
| --- | --- |
| Application | Geometry Sprint: an offline, single-player side-scrolling rhythm runner that pays tribute to *Geometry Dash* |
| Target device | FoloToy AI Passport: ESP32-C3, 8 MB Flash, no PSRAM, ST7789P3 240×320, three-button ADC, ES8311 audio |
| Branch | `feature/geometry-dash` (from `main` `0b9e4c8`) |
| Status | Implemented per this document on 2026-10-07 (M0–M3 and most P1 items); host tests, firmware build, and device test all pass; see §16 for the implementation record and deviations |
| Confirmed decisions | Landscape 320×240 with side keys on top; cube + ship + jump pads/orbs + gravity flip; six original soundtrack pieces (the first draft used local original tracks; replaced for publishing, see §2) |

This document is the requirements and acceptance basis for the application. Implementation follows
[`AGENTS.md`](../AGENTS.md), the [AI development guide](development/ai-guide.md), and the
[game demo to device acceptance SOP](development/engineering/game-demo-to-device-acceptance.md).
Priorities: **P0** must ship in the first deliverable version, **P1** should ship, **P2** only if time allows.

The Chinese version is the primary working copy. The in-app user interface is Chinese; this
English version paraphrases UI copy in English, and the exact Chinese strings are listed in the
Chinese version.

## 1. Product goals and scope

### 1.1 Experience in one sentence

Hold the device sideways and a cube dashes right to the music. The player has one action, "jump",
and uses it on the beat to clear spikes, hit jump pads, tap jump orbs, fly a ship, and flip gravity.
Death restarts instantly until the level reaches 100%.

### 1.2 Goals

- **G1 Feel first**: jump timing lines up with the beat, input responds on press, and a retry starts within 1 s of failure.
- **G2 Music**: one original soundtrack piece per level, scrolling strictly synchronized with the music, with dropped frames never affecting judgment.
- **G3 Lasting challenge**: 6 levels of increasing difficulty, each introducing a new mechanic, plus a practice mode and progress records.
- **G4 Stable and verifiable**: the core gameplay is portable C tested on the host; a bot solver proves every level can be completed; performance, memory, and audio metrics are measurable on the device.

### 1.3 Non-goals

Out of scope for this version: online levels and accounts, an on-device level editor, ball / UFO /
wave / robot / spider modes, speed portals, mirror portals, two-player mode, coin and icon unlocks,
Wi-Fi / BLE features, and deep sleep.

## 2. Copyright and compliance (P0)

- The soundtrack is entirely original: `tools/gd_music_synth.py` composes and synthesizes it from the parameters in
  `assets/levels/tracks.json`, and it ships with the repository and the firmware. The first draft used the original
  *Geometry Dash* tracks from the user's local `BGM/`; to allow public publishing, they were replaced with the original
  soundtrack on 2026-10-07. The original tracks never enter git or the firmware; `BGM/` stays local and is listed in
  `.git/info/exclude`.
- Level and track names are original (Neon Takeoff and so on) and do not reuse the original song titles; level layouts
  are original designs, not grid-for-grid copies of the official levels; characters, icons, and the interface are original artwork.
- "Geometry Dash" is a trademark of RobTop Games. The app uses only its Chinese title and states on the About page that
  it is an unofficial tribute.
- Fonts use Source Han Sans SC (SIL OFL 1.1). Record the sources and licenses of fonts, levels, and the soundtrack in
  `assets/README.md`.

## 3. Hardware and BSP prerequisites

### 3.1 Screen orientation

Fixed landscape 320×240 with the **side keys on top**: the device is rotated 90° counter-clockwise
with the lanyard hole on the left. The top edge, from left to right, holds the physical keys
**UP / DOWN / OK**, called the **left / middle / right key** below.

### 3.2 BSP changes to port (P0, first implementation step)

| Source | Content | Status |
| --- | --- | --- |
| Rhythm project commit `472027c` (`feat(bsp): add landscape orientation and side-key positions`) | `bsp_lvgl_set_orientation()`; corner mask uses the logical resolution; `BSP_LCD_ROTATION_KEYS_TOP/BOTTOM`; `BSP_BTN_EDGE_POS_TABLE {43,164,280}` | KEYS_TOP confirmed on hardware on 2026-10-06; the key position table was scaled from the product render (±10 px) and is not yet confirmed on hardware |
| metronome project commit `332bb67` (`feat(bsp): add button release event and expose I2S DMA size`) | Adds `BSP_BTN_RELEASE` (iot_button `BUTTON_PRESS_UP`) for hold-to-jump and the ship | That firmware was tested as a whole; this event was not individually recorded |
| Rhythm project commit `47c39b6` | `tools/validate.sh` uses `-dead_strip` instead of `--gc-sections` on macOS | Without it, `--static` fails on macOS while linking the demo runtime tests |

Both BSP commits add `BSP_AUDIO_DMA_DESC_NUM` / `BSP_AUDIO_DMA_FRAME_NUM`; merge them into one
definition by hand. Update the BSP host tests and both language versions of the hardware guide.
Never copy pin or voltage constants from demo code.

### 3.3 Hardware constraints the implementation must respect

- Keys: all three share one ADC resistor ladder on GPIO0 (`BSP_BTN_MV_TABLE`), so **only one key can be
  recognized at a time**. Do not design any key combination. The button component polls about every
  5 ms; `BSP_BTN_PRESS` fires on the press edge with the lowest latency.
- Button callbacks run on the shared `esp_timer` task. They may only record an `esp_timer_get_time()`
  timestamp and enqueue; they must not block or touch LVGL.
- Display: SPI at 80 MHz with one 40-line DMA draw buffer. A full 320×240 RGB565 frame is 153.6 KB,
  about 15 ms of SPI transfer alone, and rendering and transfer run in series. The BSP exposes no TE pin,
  so tear-free output cannot be guaranteed.
- The four corners have a 30 px rounded mask (`BSP_LVGL_SCREEN_RADIUS`); keep important content out of the corners.
- Audio: the I2S DMA queue is 6×240 frames, about 90 ms at 16 kHz. `bsp_audio_write()` blocks and must
  be called by one audio task only.
- Memory: no PSRAM, so no full-frame buffer; do not initialize Wi-Fi or BLE.
- The firmware compiles only the application's own sources (as Rhythm does with `main.c` and its app
  files) and boots straight into the game; the baseline test menu must never appear.

## 4. Controls (P0)

Every page shows key labels along the top edge, aligned with the physical keys (x from
`BSP_BTN_EDGE_POS_TABLE`; with the keys on top, x is approximately the table value). Label text changes
per page. **Labels are hidden during gameplay** so they do not block the view.

| Context | Left key (UP) | Middle key (DOWN) | Right key (OK) |
| --- | --- | --- | --- |
| Title | Settings | Statistics | Start (go to level select) |
| Level select | Previous level | Next level | Click to start; long press returns to title |
| Gameplay (normal) | Jump/fly, hold to keep jumping | Pause | Jump/fly, hold to keep jumping |
| Gameplay (practice) | Jump/fly | Click places a checkpoint; long press pauses | Jump/fly |
| Pause menu | Move up | Move down | Select; long press equals Resume |
| Level complete | Play again | — | Back to level select |
| Settings / Statistics | Move up (decrease when editing a value) | Move down (increase when editing a value) | Enter/confirm; long press goes back |

Input rules:

- **Jump input uses PRESS (press edge) and RELEASE (release edge)**, not CLICK, to avoid the 180 ms click
  window. "Held" means a jump-key PRESS has arrived without a later RELEASE.
- Key timestamps are recorded in the callback. The physics applies the input at the physics step matching
  that timestamp, not when a task gets around to it, so scheduling jitter does not affect judgment.
- In menus, one physical press triggers one action: a PRESS and the CLICK that follows it must not both act
  (as the Tetris demo does).
- The long-press threshold stays at `BSP_BTN_LONG_PRESS_MS` (500 ms).

## 5. Pages and flow (P0, fully redesigned)

Every page is newly designed for this application. Do not use the `main` test menu, the `demo_*.c`
pages, or the `ui_pixel` shell.

### 5.1 Visual style

- Neon geometry: solid or vertical-gradient backgrounds, black solid blocks with bright outlines, spikes as
  black triangles with bright edges, and the ground as a color band with a bright line.
- One theme per level: background, ground, and outline colors come from the theme (see §6).
- The player icon is an original cube character: primary yellow `#FFD400`, secondary cyan `#00E5FF`, with a
  simple face. The ship is an original triangular hull with the cube seated inside.
- Text uses the Chinese font subset (see §11). English level names use the Latin glyphs of the same font.

### 5.2 Page list

| Page | Content | Priority |
| --- | --- | --- |
| Title | Large Chinese title, an idle animation of the cube hopping on the ground, slowly drifting background squares; battery in the top right; three key labels on the top edge | P0 |
| Level select | Horizontal card carousel: level number, English track name, artist, difficulty badge (easy / normal / hard), normal-mode progress bar and percentage, practice-mode progress bar and percentage, completed mark. The card background uses the level theme and slides smoothly between cards | P0 |
| Gameplay | Full-screen scrolling scene; a thin progress bar and percentage at the top center (can be hidden in settings); each run shows "Attempt N" in world space at the start, scrolling away with the scene; practice mode shows checkpoint diamonds | P0 |
| Pause | A translucent overlay on the frozen frame with the menu Resume / Practice mode: on/off / Restart / Back to level select; shows the level name, current progress, and battery | P0 |
| Death feedback | The cube shatters into 8–12 pieces with a flash ring, then restarts automatically after 0.6 s and adds one attempt; music stops on death | P0 |
| Level complete | The scene freezes after the player enters the finish and "Level complete!" floats up; after 1.5 s the results page shows attempts, jumps, time, and a "New best" mark | P0 |
| Settings | Music volume (levels 0–10, default 6); sound effects on/off; progress bar show/hide; screen brightness (3 levels); audio offset (−100 to +100 ms in 10 ms steps); clear save data (hold the right key 1.5 s to confirm); About and copyright notice | P0 (audio offset is P1) |
| Statistics | Total attempts, total jumps, completed levels, total play time | P1 |
| Silent-mode notice | Without a valid music pack, level cards show "No music built in (silent play)" and the game keeps running on an `esp_timer` clock | P0 |
| First-run hint | At the start of the first attempt of level 1, "Left/right: jump · Middle: pause" appears and fades after 3 s | P1 |

### 5.3 Main flow

```text
Power on -> Title --right--> Level select --right--> Gameplay --middle--> Pause --> Resume / Restart / Level select
             |                   ^                      |
             |-left--> Settings  |                      |-hit obstacle--> Death 0.6 s --> auto restart (attempt +1)
             '-middle> Stats     '--right-- Results <---'-reach finish--> Level complete
```

- The battery appears in the top right of the title, level select, and pause pages, below the key labels
  and not overlapping them. When `bsp_battery_soc()` returns −1, show no number and never invent a percentage.
- After 2 minutes idle on a menu page the backlight dims to 20%; any key restores it, and that press only
  wakes the screen without triggering its function (P1).

## 6. Levels (P0)

### 6.1 Levels and tracks

Each level is designed to last 85–95 s. Music is cut from the start of the track to the end of the level
plus a 3 s fade-out; each track in the music pack is at most 95 s.

| # | Level (track) | BPM | First beat | Difficulty | New mechanic | Theme |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Neon Takeoff | 160 | 351 ms | Easy | Cube jumps, block platforms, one ship section | Blue |
| 2 | Pad Runner | 142 | 346 ms | Easy | Yellow jump pads | Purple |
| 3 | Orb Drift | 155 | 63 ms | Normal | Yellow jump orbs | Green |
| 4 | Upside Dune | 145 | 245 ms | Normal | Gravity portals, blue jump orbs | Orange |
| 5 | Base Breaker | 141 | 428 ms | Hard | Gravity flips inside ship sections, blue jump pads | Teal |
| 6 | Final Pulse | 170 | 216 ms | Hard | Combined: dense orb chains, gravity switches | Magenta |

BPM and first beat were first estimated from the draft's original tracks, and the levels were choreographed to them;
the original soundtrack is composed at exactly the same BPM and first beat, so beats and obstacles stay aligned.

### 6.2 Level design rules

- Start with at least 3 s of flat run-up. Split each level into 6–10 sections with 1–2 beats of rest between them.
- A new mechanic must first appear in a safe demonstration: failing the first pad or orb costs little.
- Obstacles sit on the grid (1 cell = 20 px). Beat positions are quantized with a maximum error of half a
  cell (about 48 ms); obstacles on strong beats use the cell closest to the beat.
- Difficulty ramps up: easy levels have no triple spikes and no orb chains; hard levels allow triple spikes,
  consecutive orbs, and narrow ship corridors (at least 3 cells).
- Every level must pass the bot completion proof in §12 and report its minimum timing window: at least
  50 ms for easy, 33 ms for normal, and 20 ms for hard levels.
- Finish: the last 2 s is an obstacle-free runway, followed by the finish portal.

## 7. Game rules and physics (P0)

### 7.1 Coordinates and view

- 1 cell = 20 px. The visible area is 16 cells wide × 12 cells high: the top cell is the ceiling band (holding the
  progress bar), the bottom 2 cells are the ground band, and the 9 cells (180 px) between them are the play area.
- The player is fixed at screen x = 4 cells, so 12 cells ahead are visible, about 1.15 s of look-ahead at normal speed.
- The camera only scrolls horizontally and is fixed vertically (vertical motion would force full-screen redraws and
  defeat the dirty-rectangle approach in §9.1). The floor and ceiling are solid in both modes: a cube with normal
  gravity simply stops rising at the ceiling and stands on it with inverted gravity; the ship corridor is these 9 cells.

### 7.2 Physics parameters (initial values, kept in one header, tuned after measurement)

| Parameter | Initial value | Notes |
| --- | --- | --- |
| Horizontal speed | 208 px/s (about 10.4 cells/s) | Constant throughout; this version has no speed portals |
| Physics step | Fixed 240 Hz | Integer fixed point (Q8 sub-pixels suggested), bit-identical on host, Wasm, and device |
| Cube jump velocity | 400 px/s | Jump height = 42 px (about 2.1 cells) |
| Cube gravity | 1905 px/s² | About 0.42 s airtime on flat ground, about 87 px (4.4 cells) horizontal distance |
| Yellow pad | Jump velocity × 1.35 | Jump height about 3.8 cells |
| Yellow orb | Restores one standard jump velocity in mid-air | — |
| Blue orb / blue pad | Flips gravity and sets vertical velocity to 0.4 × jump velocity toward the new direction | — |
| Ship rise acceleration | 1100 px/s² (while held) | — |
| Ship fall acceleration | 1000 px/s² (while released) | — |
| Ship max vertical speed | ±260 px/s | The ship nose angle follows velocity, visual only |
| Max fall speed (cube) | 650 px/s | — |

**Invariants that host tests must lock down:**

1. A cube on flat ground clears 3 consecutive ground spikes but not 4.
2. A cube can jump onto block platforms 1 and 2 cells high, but not 3.
3. The same input sequence yields the same trajectory regardless of the calling frame rate (fixed-step determinism).

### 7.3 Collision

- The player's outer box is 20×20 px and is used for landing and for touching portals, pads, and orbs.
- The player's inner core is 6×6 px (centered). The core touching a solid block is death: hitting a wall
  from the side or a block underside with the head.
- Landing: while falling, if the bottom of the outer box is no more than 7 px below a block top and the core
  does not touch the block, the player snaps onto the top. With inverted gravity the block underside acts as the ground.
- Spike hitboxes are reduced: 4 px wide and 8 px high, centered in the lower part of the spike, more
  forgiving than the drawn triangle. The outer box touching one is death.
- Orb hitbox is a circle of radius 12 px; pad hitbox is a 16 px × 4 px rectangle on the surface; portal
  hitbox is a 10 px × 60 px (3-cell) rectangle.
- Space beyond the floor and ceiling is a kill zone, so the player cannot leave the corridor.

### 7.4 Mechanics

| Mechanic | Rule |
| --- | --- |
| Jump | Works only when grounded. While held, the cube jumps again on every landing (hold-to-jump) |
| Orb | Triggers on a PRESS while the outer box overlaps the orb. A PRESS up to 50 ms before the overlap that is still held triggers on entry (input buffer). Each orb triggers at most once per run |
| Pad | Triggers on contact, no key needed |
| Gravity portal | Blue = flip to inverted gravity, yellow = restore normal gravity; only acts if the direction changes |
| Mode portal | Pink = switch to ship, green = switch to cube; switching keeps 50% of vertical velocity |
| Ship | Hold to rise, release to fall; touching the ceiling, floor, block tops, or block undersides is not death, the ship slides along them; block sides and spikes are death |
| Finish | Crossing the finish line completes the level; all collisions are ignored afterwards |

### 7.5 Clock and synchronization

- **Audio is the master clock** (following Rhythm's `rh_audio`): current song time = samples written to I2S
  minus the DMA queue capacity, interpolated with `esp_timer` between writes and extrapolated by at most 20 ms.
  Write one full queue of silence before every start or resume.
- At the start of each frame, the physics steps from its last step up to the step for "song time + audio offset
  − 6 ms input grace" (the grace lets key events arrive first). At most 40 steps (about 167 ms) are run per frame;
  when further behind, the physics catches up over several frames without teleporting, and the music never rewinds
  or speeds up.
- Dropped frames only make the picture choppier; they never change judgment or trajectory.
- Without a music pack, or if audio initialization fails, an `esp_timer` virtual clock is used and the game stays playable.

### 7.6 Progress, attempts, and practice mode

- Progress percentage = floor(player x / finish x × 100), from 0 to 100. On a normal-mode death that beats the
  best progress, the best is updated and "New best xx%" is shown.
- Normal mode: death restarts from the start of the level, the music restarts from 0, and attempts increase by 1.
- Practice mode (P0):
  - Automatic checkpoints: placed when the player has been grounded steadily for at least 0.1 s and at least 2 s have passed since the last checkpoint.
  - Manual checkpoints: a middle-key click places one if the current position is safe (grounded, or the ship inside its corridor) (P1).
  - A checkpoint stores the full player state (position, velocity, gravity, mode, held state); up to 32 are kept, dropping the oldest.
  - Death returns to the latest checkpoint and the music seeks to the matching song time; orbs after the checkpoint become usable again.
  - Practice best progress is recorded separately. Finishing in practice mode only marks "practice complete", not a real completion.
- Toggling practice mode from the pause menu restarts the current level from its start.

## 8. Audio (P0)

### 8.1 Format and budget

- Format: **IMA-ADPCM 4-bit, 16 kHz, mono**. The decoder costs almost no Flash or CPU, and the block structure
  naturally supports practice-mode seeking. MP3 or Opus would compete with rendering for the single C3 core
  and add 30–80 KB of Flash, so this version does not use them.
- Measured (2026-10-07): each 95 s track (3 s fade-out) is 762,880 B (about 8.0 KB/s); the 6 tracks total about 4.58 MB.

### 8.2 Partition layout

| Partition | Type/subtype | Offset | Size | Notes |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/nvs | `0x9000` | `0x6000` | Unchanged |
| `phy_init` | data/phy | `0xF000` | `0x1000` | Unchanged |
| `factory` | app/factory | `0x10000` | `0x200000` (2 MB) | The baseline app measured 1,524,336 B (with BT/Wi-Fi); this app is expected to be at most 1.2 MB |
| `music` | data/0x40 (custom) | `0x210000` | `0x5F0000` (about 5.94 MB) | Music pack; the 6 tracks use about 77%, leaving about 1.6 MB for 2 more tracks (P2) |

The layout ends at `0x800000`, exactly 8 MB. After changing `partitions.csv`, the `verify_firmware.py` checks
and the mandatory validation in [firmware layout](development/engineering/firmware-layout.md) must pass,
and the project documentation must state the flashing instructions.

### 8.3 Music pack generation and build

- Tool `tools/gen_gd_music.py` (P0, needs numpy): calls `tools/gd_music_synth.py` to compose and synthesize every
  track from `assets/levels/tracks.json` (title, BPM, first beat, mode, chord progression, timbre, random seed, length,
  and fade), encodes it as IMA-ADPCM (with the same reconstruction formula as the firmware decoder), and writes
  `build/gd_music/gd_music.bin` plus a manifest; `--wav-dir` also saves WAV files for listening.
- Music pack format (little endian): a header with magic `GDMU`, version, track count, and header CRC32;
  an index table with level number, sample rate, block alignment, samples per block, data offset, byte count,
  total samples, and data CRC32 per entry; then the IMA-ADPCM blocks.
- CMake registers an existing music pack to the `music` partition with `esptool_py_flash_to_partition`, so the
  `merge-bin` step of `./tools/validate.sh` automatically includes it in `FoloToy-AI-Passport-full.bin`. When the
  pack is missing, CMake only prints a warning and the build still succeeds (CI and other environments without a generated pack can build; the firmware then runs silently).
- During development, the app partition can be flashed alone without rewriting the roughly 4.6 MB of music every time.

### 8.4 Runtime

- At boot, read the music pack header and index and verify their CRCs; if invalid, enter silent mode (§5.2).
  The first time a track plays, verify its data CRC in the background; on failure that level plays silently
  and the failure is logged (P1).
- The audio task is the only caller of `bsp_audio_*` and runs above the LVGL task (as in Rhythm: audio 6, LVGL 4).
  It reads at least 1 KB per `esp_partition_read()` or maps in segments, and **must not map the whole partition
  at once**; decoded audio is written in 240-frame units.
- Volume: `bsp_audio_set_volume()` plus software gain, 10 levels. The sound-effect switch affects effects only
  (P1: shatter sound, completion jingle, and menu ticks, from a small synthesizer mixed with the music).
- Enable `CONFIG_I2S_ISR_IRAM_SAFE=y`. **Write NVS only while music is not advancing** (stopped or paused): during
  the death animation, after completion, while paused, and when leaving a level. Sound effects use synthesized
  in-memory data and may play while Flash is written.

## 9. Rendering and performance (P0)

### 9.1 Rendering requirements

- The background may only be a solid color or a vertical gradient (each row a single color). Horizontal scrolling
  then only needs to redraw the bounding boxes of moving objects at their old and new positions (dirty rectangles)
  instead of the whole screen. Distant decorations (P2) may only update at a low rate.
- Recommended approach, following Rhythm: one full-screen `lv_obj` with an `LV_EVENT_DRAW_MAIN` callback drawing
  in immediate mode. Each frame invalidates the union of the previous and current bounding boxes, and redraws the
  whole screen when the dirty-rectangle count exceeds its cap. Sample time once per frame so every 40-line chunk
  sees the same state.
- **M0 performance probe**: let the bot auto-play the densest section of a level and measure the frame rate of the
  approach above on the device. If it misses the target, push dirty rectangles directly through
  `bsp_display_panel()` + `esp_lcd_panel_draw_bitmap()` with a line buffer (big-endian RGB565; see the
  [display refresh notes](reference/shinku-chen/display-refresh-and-deep-sleep.md)). The measured result decides.
- No full-frame buffer. Visible objects are found by binary search in the level's x-sorted array, so per-frame
  work does not grow with level length.

### 9.2 Measurable targets

| Metric | Target | How to measure |
| --- | --- | --- |
| Gameplay frame rate | Average ≥ 30 fps in every 10 s window (40–50 fps expected); at most 3 frame gaps over 66 ms per minute | Serial log line every second: `fps / max_frame_ms / render_ms` |
| Input to picture | ≤ 50 ms from press to visible jump | 240 fps phone slow-motion video (P1; list as unverified until measured) |
| Input to judgment | 0 ms game-time error (applied by timestamp) | Host tests plus a log comparing key timestamps with the jump physics step |
| Audio continuity | 0 underruns during a full level | The audio task counts write gaps longer than the queue duration |
| Memory | In gameplay, minimum free heap ≥ 40 KB, largest free block ≥ 16 KB; LVGL pool peak ≤ 80% | Log `heap_caps_get_free_size` / `largest_free_block` every 5 s |
| Boot | Title page usable within 2 s of power-on | Serial timestamps |
| Stability | 30 minutes of continuous play with no reboot, watchdog, or allocation failure, and no steady decline of free heap | Long-run log |

## 10. Save data (P0)

- One blob in the NVS namespace `gdash`, with a schema version. On load, check version and length, and clamp
  invalid fields into range.
- Contents: per level, normal best progress, practice best progress, real completion, practice completion,
  attempts, and jumps; plus total play time and settings (volume, sound effects, progress bar, brightness, audio offset).
- When to write:
  - On a new best progress, write during the death animation.
  - Attempts and jumps are written on completion, pause, and leaving a level; an unexpected power loss loses at most the current level's unsaved counts.
  - Settings are written when leaving the settings page.
- "Clear save data" requires a long-press confirmation and restores defaults while keeping settings.
- Flashing the merged image at `0x0` resets save data; the README and delivery notes must say so.

## 11. Chinese fonts (P0)

- All UI text lives in `main/gd_strings.h`. A tool check rejects non-ASCII string literals outside that file (as in Rhythm).
- The generator `tools/gen_gd_fonts.py` runs `npx lv_font_conv@1.5.3` with `--bpp 4 --no-compress` on Source Han
  Sans SC (local `~/esp/fonts/source-han-sans-2.005/`) and produces these subsets:
  - Bold 14 / 16 / 20 px: all UI text plus ASCII
  - Heavy 36 px: the title, the level-complete banner, and digits plus `%`
  - Output goes to `assets/fonts/` with a manifest and SHA-256 hashes
- Set the font explicitly on every widget part and state; show `LV_SYMBOL_*` through a fallback to Montserrat.
- Acceptance:
  - A host test checks that every code point gets a non-placeholder glyph from `lv_font_get_glyph_dsc()`, including the negative case U+9F98.
  - A boot self-check verifies glyph coverage of all strings.
  - On the device, visually confirm there are no boxes or blanks.

## 12. Level data and toolchain

- Level sources (P0): `assets/levels/level_<n>.txt`, original and committed. The header holds BPM, first beat,
  theme, and length; the body is an ASCII grid with one character per cell and the ground layer on the bottom row:

  | Char | Meaning | Char | Meaning |
  | --- | --- | --- | --- |
  | `.` | Empty | `#` | Solid block |
  | `^` | Ground spike (pointing up) | `v` | Ceiling spike (pointing down) |
  | `y` | Yellow pad | `o` | Yellow orb |
  | `B` | Blue pad | `b` | Blue orb |
  | `G` | Gravity portal (flip) | `g` | Gravity portal (restore) |
  | `S` | Ship portal | `C` | Cube portal |
  | `E` | Finish | `=` | Ship-section ceiling/floor boundary |

- `tools/gen_gd_levels.py` (P0):
  - Compiles levels into compact x-sorted object arrays in `main/gd_levels_data.c`. The generated file is
    committed with the source hashes, and the check fails when it is stale.
  - The `ruler` subcommand prints a beat ruler above the grid from the BPM and first beat, for aligning obstacles while designing.
  - The `render` subcommand renders a whole level as a long PNG for review.
- Bot solver (P0): searches input sequences on the host with the same physics code to prove each level can be
  completed. It outputs a replayable input recording and reports the minimum timing window at each point (§6.2).
  Replaying the recording is a regression test.
- Auto-play debug mode (P1): behind a build switch, the device replays the recording to finish a level, so the §9.2
  performance metrics can be measured repeatedly in a fixed scenario.
- Host preview (used instead of the H5/Wasm preview): `tools/render_gd_preview.py` compiles the firmware UI and play
  code with the real LVGL on a computer, renders every page with the same chunked partial refresh as the firmware,
  auto-plays all six levels with the replays, checks that partial refresh matches a full redraw pixel for pixel, and
  reports the LVGL pool peak and pixels pushed per frame. The H5/Wasm preview was not built (P2).

## 13. Testing and acceptance

### 13.1 Host tests (part of `./tools/validate.sh --static`, P0)

| Module | Coverage |
| --- | --- |
| Physics | The three invariants in §7.2; jump, landing, hold-to-jump; ship rise, fall, and speed limit; landing surface after a gravity flip |
| Collision | Reduced spike hitbox edges; block-top landing and side death; inner core versus outer box; kill zone |
| Mechanics | Pads, orbs (including the 50 ms buffer and single trigger), gravity portals, mode portals, finish |
| Clock | Catch-up cap, stall drop, silent virtual clock, audio offset |
| Practice mode | Automatic and manual checkpoints, state restore, orb reset, the 32-checkpoint cap, music seek position |
| Levels | Parser handling of every character and of bad input; all 6 levels completable; deterministic replay |
| Audio | IMA-ADPCM decode round-trip error against the reference encoder; music pack header, index, and CRC parsing; fallback when missing or corrupt |
| Save data | Defaults, version migration, clamping of invalid fields, settings kept when clearing data |
| UI logic | Page navigation state machine; key label x positions match `BSP_BTN_EDGE_POS_TABLE`; PRESS and CLICK never both act |
| Fonts | Glyph coverage of all strings; the U+9F98 negative case; generated files not stale |

### 13.2 Device acceptance scenario table (SOP stage 0 contract)

| Case | Setup and action | Expected result | Evidence |
| --- | --- | --- | --- |
| Boot orientation | Power on after flashing, hold sideways with keys on top | Title page within 2 s, picture not upside down, three key labels aligned with the physical keys | Photo + boot log |
| Menu navigation | Title → level select → browse all 6 cards → back | One action per press; long press on the right key goes back | Video |
| Start and jump | Enter level 1, click the right key, then the left key | Both keys jump; the jump is in sync with the press | Slow-motion video |
| Hold-to-jump | Hold the right key on flat ground for 3 s | Jumps again on every landing; stops after release | Video |
| Ship | Level 1 ship section, hold and release | Rises while held, falls when released, no death at the ceiling | Video |
| Pads, orbs, gravity | Matching sections in levels 2–4 | Behavior matches §7.4 | Video |
| Death and restart | Hit a spike on purpose | Shatter for 0.6 s, automatic restart, attempts +1, music restarts | Video + log |
| Pause and resume | Press the middle key in gameplay, choose Resume | Picture and music freeze together; no jump or pop on resume | Video |
| Practice mode | Enable practice, die several times | Returns to the checkpoint, music seeks to match, practice progress recorded separately | Video |
| Completion and records | Complete level 1 | Results page shown; the card is marked complete at 100% | Photo |
| Power-loss persistence | Cut power after a new best and reboot | Best progress kept | Photo |
| Silent mode | Flash firmware without a music pack (or erase the `music` partition) | Silent notice shown, game playable | Photo + log |
| Audio sync | A full run of level 1 | No noticeable lag by ear; fix with the audio offset if needed | Player feedback |
| Performance and stability | Auto-play the densest section, plus 30 minutes of continuous play | All §9.2 targets met | Serial log |
| Chinese display | Visit every page | No boxes or blanks; long text stays within bounds | Photos |
| Battery | Both with a reading and with −1 | Shown when available, hidden at −1 | Photos |

Run at least one blind playthrough as well: a player who has not read any instructions plays levels 1–2 in full,
and the session records whether jump, pause, and practice mode were understood.

### 13.3 Delivery format

After each implementation round, report `Build`, `Host tests`, `Device tests`, and `Unverified` separately, and
deliver `build/FoloToy-AI-Passport-full.bin` for flashing at `0x0` together with the `build/firmware/<sha256>/`
archive. A successful build is not device verification. Flashing requires the user's approval first.

## 14. Milestones

| Milestone | Content | Done when |
| --- | --- | --- |
| M0 Foundations and probes | Port the three commits in §3.2; new partition table; app skeleton booting straight to the title page; rendering performance probe; ADPCM playback and audio clock probe | Full gate PASS; correct landscape orientation on the device; probe frame rate and underrun data recorded and the rendering approach decided |
| M1 Core gameplay | Portable C physics, collision, mechanics, clock; level parser and solver; level 1 | Host completion proof for level 1; physics invariant tests PASS |
| M2 Application shell | All P0 pages; music pack tool and runtime; save data; fonts | Level 1 fully playable on the device with music |
| M3 More levels | Design levels 2–6, calibrate BPM, completion proofs | Solver PASS for all 6 levels with timing windows met |
| M4 Polish and acceptance | P1 items (sound effects, statistics, first-run hint, audio offset, auto-play); device scenario table and performance measurements | All of §13.2 passes; the unverified list is empty or accepted by the user |

## 15. Risks and open items

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Scrolling frame rate too low | Choppy picture, hard to anticipate | Restrict the background to vertical gradients; M0 probe; switch to direct panel pushes if needed; fewer particles |
| Tearing without a TE pin | Diagonal shearing during fast scrolling | Keep the frame rate steady; the user judges visually; record as a known limitation |
| Audio pops or underruns | Breaks the sense of rhythm | High-priority audio task; Flash writes only while music is stopped; IRAM-safe ISR; count underruns |
| ADC key release latency | The ship feels "sticky" | Measure RELEASE latency; tune ship parameters to compensate if needed |
| BPM estimation error | Obstacles off the strong beats | Calibrate by ear with the beat ruler; audio offset in settings |
| Large level design effort | Schedule slips | Build level 1 end to end first; the solver checks completability automatically; review with long PNGs |
| Copyright | Original tracks ending up in the firmware | The soundtrack is now original; `gen_gd_music.py` no longer reads `BGM/`; the About page states it is an unofficial tribute |
| Key position table not confirmed on hardware | Key labels may be off by ±10 px | Check on the device in M0; adjust `BSP_BTN_EDGE_POS_TABLE` if needed |

## 16. Implementation record (2026-10-07)

| Item | Result |
| --- | --- |
| Levels | Six levels of about 90 s each in `assets/levels/level_*.txt`; the solver clears all of them, and the replays trigger every orb (the route is forced) |
| Timing windows (narrowest / median) | Easy 141–154 ms / 250 ms; normal 54 ms / 187 ms and 137 ms / 158 ms; hard 58 ms / 154–200 ms; all above the §6.2 minimums |
| Physics | Jump height 41 px, airtime 420 ms; pad height 75 px; triple spikes clearable, quadruple not; 1- and 2-cell platforms reachable, 3 not |
| Music pack | Six original tracks × 95 s, 16 kHz IMA-ADPCM, 4,577,504 bytes (73% of the music partition); 23–25 dB SNR after encoding, detected BPM matches every level, no clipping |
| Firmware | App 860,304 bytes (41% of the 2 MB partition); merged image with the music pack 6,740,192 bytes |
| Host preview | Partial refresh differs from a full redraw by 0 pixels; LVGL pool peak about 9 KB of 40 KB; gameplay pushes about 33% of the screen's pixels per frame on average |

Deviations from the first draft:

- The ship may slide along block tops and undersides (as in the original game); only block sides and spikes are fatal (§7.4 updated).
- The physics catches up over several frames instead of dropping time (§7.5 updated).
- NVS writes are allowed whenever music is not advancing (§8.4 updated): the death animation lasts only 0.6 s, and waiting for the sound-effect tail would miss the write window.
- The play area is a fixed 9 cells tall and the camera does not follow vertically (§7.1 updated).
- The solver replays are "centered": each press moves to the middle of its feasible range, for auto-play and window statistics.
- A host LVGL preview replaces the H5/Wasm preview (§12 updated).
- The soundtrack changed from local original tracks to an original synthesized score and the levels got original names (§2, §6.1, §8 updated) so the game can be published.

Device: on 2026-10-07 the developer flashed the delivered merged image (SHA-256 `b2094002…be6437`) and tested it on
the device; the result passed. It was an overall acceptance: the §13.2 scenario table and the §9.2 frame rate, memory,
and audio underrun figures were not recorded item by item; the keys-at-bottom landscape orientation is unused by
this app and not verified on hardware. The firmware with the original soundtrack (only the music pack, level names,
and About text changed) has not been listened to on the device yet.

## Appendix A: Reference implementations

| Reference | What to borrow |
| --- | --- |
| Rhythm (`../Rhythm`, `feature/rhythm-game`) | Landscape BSP, audio master clock (`main/rh_audio.*`), immediate-mode drawing and dirty rectangles (`main/rh_gfx.*`, `main/rh_ui.c`), font generation and glyph self-check (`tools/gen_rhythm_fonts.py`) |
| FlappyBird (`../FlappyBird`, `feature/flappy-bird`) | Q8 fixed-point physics with a catch-up cap (`main/flappy_model.*`), reduced hitboxes, NVS save data, font coverage tests |
| metronome (`../metronome`, `feature/metronome`) | `BSP_BTN_RELEASE`, writing NVS only while audio is quiet |
| `demo/tetris-game` (gitee remote) | Low-latency PRESS handling, PRESS/CLICK de-duplication, partial refresh |
| [Audio compression trade-offs](reference/shinku-chen/audio-compression-trade-offs.md) | Capacity and CPU comparison of IMA-ADPCM, MP3, and Opus |
| [Landscape rotation notes](reference/shinku-chen/landscape-rotation-and-deep-sleep-key-wake.md) | Rotate before building pages; orientation can only be confirmed on the device |
