#!/usr/bin/env python3
"""几何冲刺关卡工具:ASCII 关卡 → C 数据、通关录像、节拍标尺与预览长图。

关卡源文件 assets/levels/level_<n>.txt(原创,随仓库提交)。格式:

    ; 注释行
    name: Stereo Madness          头部:键值对,直到 "---"
    artist: ForeverBound
    difficulty: easy              easy / normal / hard
    theme: 0                      配色主题编号(main/gd_theme.h)
    track: 1                      音乐包曲目号
    bpm: 160.00
    first_beat_ms: 351
    ---
    @0000 +---|---|---|...        节拍标尺(由 ruler 子命令重写,解析时忽略)
    8    |........                网格:行号 8..0 自上而下,第 0 行紧贴地面,每字符一格
    ...
    0    |....^...

  每个分块 = 可选标尺行 + 9 行网格,分块从左到右拼接。字符:
    .  空        #  实心方块     ^  地刺(朝上)   v  顶刺(朝下)
    y  黄跳板    B  蓝跳板       o  黄跳环       b  蓝跳环
    G  蓝重力门  g  黄重力门     S  飞船门       C  方块门       E  终点线(取最左一列)
  跳板贴在下方有支撑(第 0 行或下方是 #)的格子底部,否则贴在上方有支撑的格子顶部。

子命令:
  generate  生成 main/gd_levels_data.c,编译主机求解器(tests/gd_bot_main.c)求出每关的
            通关录像写入 main/gd_replays_data.c,并更新 assets/levels/levels.manifest.json。
            任一关无解时失败。
  check     不需要编译器:核对生成数据与关卡源一致,物理源码与关卡未在生成后改动。纳入 validate.sh。
  ruler     按每关 bpm / first_beat_ms 重写标尺行("+" 小节首拍,"|" 拍,"-" 其余)。
            标尺标的是"拍点时刻玩家中心所在的列";按拍起跳的障碍放在标尺后约 2 格。
  render    把每关渲染成 PNG 长图到 build/levels/(开发辅助)。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEVEL_DIR = ROOT / "assets" / "levels"
DATA_C = ROOT / "main" / "gd_levels_data.c"
REPLAYS_C = ROOT / "main" / "gd_replays_data.c"
MANIFEST = LEVEL_DIR / "levels.manifest.json"
PHYSICS_SOURCES = ["main/gd_level.h", "main/gd_level.c", "main/gd_sim.h", "main/gd_sim.c", "main/gd_replay.h",
                   "tests/gd_solver.h", "tests/gd_solver.c", "tests/gd_bot_main.c"]
LEVEL_COUNT = 6
ROWS = 9
CELL_PX = 20
MAX_RUN = 16
MAX_OBJS = 1024
TICK_HZ = 240
FP_ONE = 4096
START_X_PX = 10
DIFFICULTIES = {"easy": "GD_DIFF_EASY", "normal": "GD_DIFF_NORMAL", "hard": "GD_DIFF_HARD"}

# 字符 → (C 枚举名, 是否可水平合并)
TYPES = {
    "#": ("GD_OBJ_BLOCK", True),
    "^": ("GD_OBJ_SPIKE_UP", True),
    "v": ("GD_OBJ_SPIKE_DOWN", True),
    "y": ("GD_OBJ_PAD_YELLOW", False),
    "B": ("GD_OBJ_PAD_BLUE", False),
    "o": ("GD_OBJ_ORB_YELLOW", False),
    "b": ("GD_OBJ_ORB_BLUE", False),
    "G": ("GD_OBJ_PORTAL_FLIP", False),
    "g": ("GD_OBJ_PORTAL_NORMAL", False),
    "S": ("GD_OBJ_PORTAL_SHIP", False),
    "C": ("GD_OBJ_PORTAL_CUBE", False),
}
TYPE_ORDER = [v[0] for v in TYPES.values()]
GRID_RE = re.compile(r"^([0-8])    \|(.*)$")
RULER_RE = re.compile(r"^@(\d{4}) (.*)$")


def speed_px_per_s() -> float:
    """与 gd_level.h 的 GD_SPEED 一致的实际速度(整数取整后)。"""
    per_tick = (208 * FP_ONE + TICK_HZ // 2) // TICK_HZ
    return per_tick * TICK_HZ / FP_ONE


@dataclass
class Obj:
    x: int
    y: int
    type: str
    length: int = 1
    flags: int = 0


@dataclass
class Level:
    path: Path
    header: dict[str, str]
    rows: list[str]                      # rows[r] = 第 r 行(0 = 地面行)的完整字符串
    objs: list[Obj] = field(default_factory=list)
    finish_col: int = -1


class LevelError(Exception):
    pass


def parse_level(path: Path) -> Level:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    header: dict[str, str] = {}
    i = 0
    while i < len(lines) and lines[i].strip() != "---":
        line = lines[i].strip()
        i += 1
        if not line or line.startswith(";"):
            continue
        key, sep, value = line.partition(":")
        if not sep:
            raise LevelError(f"{path.name}:{i}: header line needs 'key: value'")
        header[key.strip()] = value.strip()
    if i >= len(lines):
        raise LevelError(f"{path.name}: missing '---' after header")
    i += 1
    rows = [""] * ROWS
    chunk: dict[int, str] = {}
    chunk_line = 0

    def flush_chunk(lineno: int) -> None:
        if not chunk:
            return
        if sorted(chunk) != list(range(ROWS)):
            raise LevelError(f"{path.name}:{lineno}: chunk needs rows 8..0, got {sorted(chunk, reverse=True)}")
        widths = {len(v) for v in chunk.values()}
        if len(widths) != 1:
            raise LevelError(f"{path.name}:{lineno}: chunk rows have different widths {sorted(widths)}")
        for r in range(ROWS):
            rows[r] += chunk[r]
        chunk.clear()

    for n, line in enumerate(lines[i:], start=i + 1):
        if not line.strip() or line.startswith(";"):
            flush_chunk(chunk_line)
            continue
        m = RULER_RE.match(line)
        if m:
            flush_chunk(chunk_line)
            if int(m.group(1)) != len(rows[0]):
                raise LevelError(f"{path.name}:{n}: ruler says column {m.group(1)}, chunk starts at {len(rows[0])}")
            continue
        m = GRID_RE.match(line)
        if not m:
            raise LevelError(f"{path.name}:{n}: unrecognized line {line[:20]!r}")
        r = int(m.group(1))
        if r in chunk:
            flush_chunk(n)
        if not chunk:
            chunk_line = n
        chunk[r] = m.group(2)
    flush_chunk(chunk_line)
    level = Level(path=path, header=header, rows=rows)
    build_objects(level)
    validate_header(level)
    return level


def build_objects(level: Level) -> None:
    rows = level.rows
    width = len(rows[0])
    finish = -1
    for r in range(ROWS):
        c = 0
        row = rows[r]
        while c < width:
            ch = row[c]
            if ch == ".":
                c += 1
                continue
            if ch == "E":
                finish = c if finish < 0 else min(finish, c)
                c += 1
                continue
            if ch not in TYPES:
                raise LevelError(f"{level.path.name}: unknown character {ch!r} at column {c}, row {r}")
            name, mergeable = TYPES[ch]
            length = 1
            if mergeable:
                while c + length < width and row[c + length] == ch and length < MAX_RUN:
                    length += 1
            flags = 0
            if ch in "yB":
                below = r == 0 or rows[r - 1][c] == "#"
                above = r == ROWS - 1 or rows[r + 1][c] == "#"
                if not below and not above:
                    raise LevelError(f"{level.path.name}: pad at column {c}, row {r} has no surface to sit on")
                if not below:
                    flags = 1
            level.objs.append(Obj(c, r, name, length, flags))
            c += length
    if finish < 0:
        raise LevelError(f"{level.path.name}: no finish line 'E'")
    level.finish_col = finish
    level.objs.sort(key=lambda o: (o.x, o.y, TYPE_ORDER.index(o.type)))
    if len(level.objs) > MAX_OBJS:
        raise LevelError(f"{level.path.name}: {len(level.objs)} objects exceed the limit {MAX_OBJS}")
    if any(o.x >= finish for o in level.objs):
        raise LevelError(f"{level.path.name}: objects after the finish line")


def validate_header(level: Level) -> None:
    h = level.header
    for key in ("name", "artist", "difficulty", "theme", "track", "bpm", "first_beat_ms"):
        if key not in h:
            raise LevelError(f"{level.path.name}: header missing {key!r}")
    if h["difficulty"] not in DIFFICULTIES:
        raise LevelError(f"{level.path.name}: difficulty must be one of {sorted(DIFFICULTIES)}")
    for key in ("name", "artist"):
        if not h[key].isascii() or '"' in h[key] or "\\" in h[key]:
            raise LevelError(f"{level.path.name}: {key} must be plain ASCII")
    float(h["bpm"])
    int(h["first_beat_ms"])
    int(h["theme"])
    int(h["track"])


def level_paths() -> list[Path]:
    return [LEVEL_DIR / f"level_{n}.txt" for n in range(1, LEVEL_COUNT + 1)]


def load_levels() -> list[Level]:
    return [parse_level(p) for p in level_paths()]


def render_data_c(levels: list[Level]) -> str:
    out = ["// main/gd_levels_data.c —— 由 tools/gen_gd_levels.py 从 assets/levels/*.txt 生成,请勿手改。",
           '#include "gd_level.h"', ""]
    for n, lv in enumerate(levels, start=1):
        out.append(f"static const gd_obj_t L{n}[] = {{")
        for o in lv.objs:
            out.append(f"    {{ {o.x}, {o.y}, {o.type}, {o.length}, {o.flags} }},")
        out.append("};")
        out.append("")
    out.append("const gd_level_t GD_LEVELS[GD_LEVEL_COUNT] = {")
    for n, lv in enumerate(levels, start=1):
        h = lv.header
        bpm = round(float(h["bpm"]) * 100)
        out.append("    {")
        out.append(f'        .name = "{h["name"]}",')
        out.append(f'        .artist = "{h["artist"]}",')
        out.append(f'        .difficulty = {DIFFICULTIES[h["difficulty"]]},')
        out.append(f'        .theme = {int(h["theme"])},')
        out.append(f'        .track = {int(h["track"])},')
        out.append(f"        .bpm_x100 = {bpm},")
        out.append(f'        .first_beat_ms = {int(h["first_beat_ms"])},')
        out.append(f"        .finish_col = {lv.finish_col},")
        out.append(f"        .obj_count = {len(lv.objs)},")
        out.append(f"        .objs = L{n},")
        out.append("    },")
    out.append("};")
    return "\n".join(out) + "\n"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def physics_hash() -> str:
    h = hashlib.sha256()
    for rel in PHYSICS_SOURCES:
        h.update(rel.encode())
        h.update((ROOT / rel).read_bytes())
    return h.hexdigest()


def manifest_for(data_c: str, replays_c: str) -> dict:
    return {
        "generator": "tools/gen_gd_levels.py",
        "levels": {p.name: sha256(p.read_bytes()) for p in level_paths()},
        "physics_sources": PHYSICS_SOURCES,
        "physics_sha256": physics_hash(),
        "levels_data_sha256": sha256(data_c.encode()),
        "replays_data_sha256": sha256(replays_c.encode()),
    }


def build_bot(workdir: Path) -> Path:
    exe = workdir / "gd_bot"
    sources = ["tests/gd_bot_main.c", "tests/gd_solver.c", "main/gd_sim.c", "main/gd_level.c", "main/gd_levels_data.c"]
    cmd = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Imain", "-Itests",
           *[str(ROOT / s) for s in sources], "-o", str(exe)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ERROR building solver:\n{result.stderr}")
    return exe


def cmd_generate(_: argparse.Namespace) -> int:
    levels = load_levels()
    data_c = render_data_c(levels)
    DATA_C.write_text(data_c, encoding="utf-8")
    with tempfile.TemporaryDirectory() as tmp:
        exe = build_bot(Path(tmp))
        result = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        raise SystemExit("ERROR: at least one level has no solution; replays not updated")
    REPLAYS_C.write_text(result.stdout, encoding="utf-8")
    MANIFEST.write_text(json.dumps(manifest_for(data_c, result.stdout), indent=2) + "\n", encoding="utf-8")
    for n, lv in enumerate(levels, start=1):
        print(f"level {n}: {lv.header['name']}: {len(lv.objs)} objects, finish column {lv.finish_col} "
              f"({finish_seconds(lv):.1f} s)")
    return 0


def finish_seconds(lv: Level) -> float:
    return (lv.finish_col * CELL_PX - START_X_PX) / speed_px_per_s()


def run_check() -> list[str]:
    errors: list[str] = []
    try:
        levels = load_levels()
    except (LevelError, OSError, ValueError) as exc:
        return [str(exc)]
    data_c = render_data_c(levels)
    if not DATA_C.is_file() or DATA_C.read_text(encoding="utf-8") != data_c:
        errors.append("main/gd_levels_data.c is stale; run tools/gen_gd_levels.py generate")
    if not MANIFEST.is_file() or not REPLAYS_C.is_file():
        errors.append("levels manifest or replays missing; run tools/gen_gd_levels.py generate")
        return errors
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    expected = manifest_for(data_c, REPLAYS_C.read_text(encoding="utf-8"))
    for key in ("levels", "physics_sha256", "levels_data_sha256", "replays_data_sha256"):
        if manifest.get(key) != expected[key]:
            errors.append(f"levels manifest {key} out of date (levels, physics or generated files changed); "
                          "run tools/gen_gd_levels.py generate")
    names = [lv.header["name"] for lv in levels]
    if len(set(names)) != len(names):
        errors.append("level names must be unique")
    return errors


def cmd_check(_: argparse.Namespace) -> int:
    errors = run_check()
    for e in errors:
        print(f"ERROR: {e}", file=sys.stderr)
    if errors:
        return 1
    print("Geometry Sprint levels: PASS")
    return 0


def ruler_text(lv: Level, start: int, width: int) -> str:
    bpm = float(lv.header["bpm"])
    first = int(lv.header["first_beat_ms"]) / 1000.0
    v = speed_px_per_s()
    chars = ["-"] * width
    beat = 60.0 / bpm
    k = 0
    while True:
        t = first + k * beat
        col = int((START_X_PX + v * t) // CELL_PX)
        if col >= start + width:
            break
        if col >= start:
            chars[col - start] = "+" if k % 4 == 0 else "|"
        k += 1
    return "".join(chars)


def cmd_ruler(_: argparse.Namespace) -> int:
    for path in level_paths():
        lv = parse_level(path)
        lines = path.read_text(encoding="utf-8").splitlines()
        out: list[str] = []
        col = 0
        in_body = False
        idx = 0
        while idx < len(lines):
            line = lines[idx]
            if not in_body:
                out.append(line)
                if line.strip() == "---":
                    in_body = True
                idx += 1
                continue
            if RULER_RE.match(line):
                idx += 1
                continue
            m = GRID_RE.match(line)
            if m and m.group(1) == "8":
                width = len(m.group(2))
                out.append(f"@{col:04d} {ruler_text(lv, col, width)}")
                for _ in range(ROWS):
                    out.append(lines[idx])
                    idx += 1
                col += width
                continue
            out.append(line)
            idx += 1
        path.write_text("\n".join(out) + "\n", encoding="utf-8")
        print(f"{path.name}: ruler updated ({col} columns)")
    return 0


COLORS = {
    ".": (24, 30, 60), "#": (10, 10, 14), "^": (240, 240, 250), "v": (240, 240, 250), "y": (255, 214, 0),
    "B": (60, 160, 255), "o": (255, 214, 0), "b": (60, 160, 255), "G": (60, 160, 255), "g": (255, 214, 0),
    "S": (255, 80, 200), "C": (80, 230, 120), "E": (255, 255, 255),
}


def write_png(path: Path, width: int, height: int, rgb: bytearray) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = width * 3
    raw = b"".join(b"\x00" + bytes(rgb[y * stride:(y + 1) * stride]) for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def cmd_render(args: argparse.Namespace) -> int:
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cell, strip_cols = 5, 240
    for n, lv in enumerate(load_levels(), start=1):
        width = len(lv.rows[0])
        strips = (width + strip_cols - 1) // strip_cols
        strip_h = (ROWS + 2) * cell + 6
        W, H = strip_cols * cell, strips * strip_h
        img = bytearray(W * H * 3)
        beats = set()
        for s in range(strips):
            ruler = ruler_text(lv, s * strip_cols, strip_cols)
            beats |= {s * strip_cols + i for i, ch in enumerate(ruler) if ch in "+|"}
        for s in range(strips):
            for c in range(strip_cols):
                col = s * strip_cols + c
                for r in range(ROWS + 2):
                    if col >= width:
                        color = (0, 0, 0)
                    elif r == ROWS:
                        color = (40, 60, 120)                        # 地面带
                    elif r == ROWS + 1:
                        color = (255, 255, 255) if col in beats else (20, 20, 20)   # 节拍标记
                    else:
                        ch = lv.rows[ROWS - 1 - r][col]
                        color = COLORS.get(ch, (255, 0, 0))
                        if ch == "." and col in beats:
                            color = (34, 42, 80)
                    for py in range(cell):
                        y = s * strip_h + r * cell + py
                        base = (y * W + c * cell) * 3
                        for px in range(cell):
                            img[base + px * 3:base + px * 3 + 3] = bytes(color)
        path = out_dir / f"level_{n}.png"
        write_png(path, W, H, img)
        print(f"wrote {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("generate").set_defaults(func=cmd_generate)
    sub.add_parser("check").set_defaults(func=cmd_check)
    sub.add_parser("ruler").set_defaults(func=cmd_ruler)
    render = sub.add_parser("render")
    render.add_argument("--out", default=str(ROOT / "build" / "levels"))
    render.set_defaults(func=cmd_render)
    args = parser.parse_args()
    try:
        return args.func(args)
    except LevelError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
