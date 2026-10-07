#!/usr/bin/env python3
"""几何冲刺工具链测试:关卡解析与生成数据、音乐包格式与固件解析器互通、曲目表与关卡时长匹配。"""

from __future__ import annotations

import importlib.util
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


levels = load("gen_gd_levels")
music = load("gen_gd_music")

GRID_HEADER = "name: T\nartist: A\ndifficulty: easy\ntheme: 0\ntrack: 1\nbpm: 120\nfirst_beat_ms: 0\n---\n"


def grid(rows_top_down: list[str]) -> str:
    return "".join(f"{8 - i}    |{row}\n" for i, row in enumerate(rows_top_down))


class LevelToolTest(unittest.TestCase):
    def parse(self, body: str):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "level_x.txt"
            path.write_text(GRID_HEADER + body, encoding="utf-8")
            return levels.parse_level(path)

    def test_committed_levels_pass_check(self):
        self.assertEqual(levels.run_check(), [])

    def test_parse_runs_pads_and_finish(self):
        rows = ["." * 30] * 9
        rows = list(rows)
        rows[8] = "....B" + "." * 25                 # 第 8 行:贴天花板的蓝跳板
        rows[7] = "." * 30
        rows[6] = "." * 30
        rows[5] = "." * 30
        rows[4] = "." * 30
        rows[3] = "." * 30
        rows[2] = "......o" + "." * 23
        rows[1] = "....." + "##" + "." * 23
        rows[0] = "...y." + "##" + "^^^" + "." * 5 + "E" + "." * 14
        lv = self.parse(grid(rows[::-1]))   # rows 按行号排列,grid() 要自上而下
        kinds = [(o.x, o.y, o.type, o.length, o.flags) for o in lv.objs]
        self.assertIn((3, 0, "GD_OBJ_PAD_YELLOW", 1, 0), kinds)
        self.assertIn((4, 8, "GD_OBJ_PAD_BLUE", 1, 1), kinds)
        self.assertIn((5, 0, "GD_OBJ_BLOCK", 2, 0), kinds)
        self.assertIn((7, 0, "GD_OBJ_SPIKE_UP", 3, 0), kinds)
        self.assertIn((6, 2, "GD_OBJ_ORB_YELLOW", 1, 0), kinds)
        self.assertEqual(lv.finish_col, 15)

    def test_long_runs_are_split(self):
        rows = ["." * 40] * 8 + ["#" * 35 + "E" + "...."]
        lv = self.parse(grid(rows))
        self.assertEqual([o.length for o in lv.objs], [16, 16, 3])

    def test_errors(self):
        bad_char = ["." * 10] * 8 + ["..X..E...."]
        with self.assertRaises(levels.LevelError):
            self.parse(grid(bad_char))
        no_finish = ["." * 10] * 9
        with self.assertRaises(levels.LevelError):
            self.parse(grid(no_finish))
        floating_pad = ["." * 10] * 4 + ["..y......."] + ["." * 10] * 3 + [".....E...."]
        with self.assertRaises(levels.LevelError):
            self.parse(grid(floating_pad))
        ragged = ["." * 10] * 8 + ["......E.."]
        with self.assertRaises(levels.LevelError):
            self.parse(grid(ragged))
        with self.assertRaises(levels.LevelError):
            self.parse("@0005 ----------\n" + grid(["." * 10] * 8 + [".....E...."]))
        after_finish = ["." * 10] * 8 + ["..E..^...."]
        with self.assertRaises(levels.LevelError):
            self.parse(grid(after_finish))

    def test_ruler_marks_beats(self):
        rows = ["." * 80] * 8 + ["." * 70 + "E" + "." * 9]
        lv = self.parse(grid(rows))
        ruler = levels.ruler_text(lv, 0, 80)
        self.assertEqual(len(ruler), 80)
        # 120 BPM、首拍 0:每拍 0.5 s ≈ 104 px ≈ 5.2 格
        beats = [i for i, ch in enumerate(ruler) if ch in "+|"]
        self.assertEqual(beats[0], 0)
        self.assertTrue(all(5 <= b - a <= 6 for a, b in zip(beats, beats[1:])))
        self.assertEqual(ruler[beats[4]], "+")


class MusicToolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dump = Path(cls.tmp.name) / "gd_mpack_dump"
        result = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'main'}",
                                 str(ROOT / "tests" / "gd_mpack_dump.c"), str(ROOT / "main" / "gd_mpack.c"),
                                 "-o", str(cls.dump)], capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fake_track(self, number: int, blocks: int) -> "music.Track":
        data = bytes((i * 31 + number) & 0xFF for i in range(1024 * blocks))
        return music.Track(number, 16000, 1024, blocks * music.samples_per_block(1024) - 10, data)

    def test_pack_round_trips_through_firmware_parser(self):
        pack = music.build_pack([self.fake_track(1, 3), self.fake_track(4, 2)])
        path = Path(self.tmp.name) / "pack.bin"
        path.write_bytes(pack)
        out = subprocess.run([str(self.dump), str(path)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("OK 2 tracks", out.stdout)
        self.assertEqual(out.stdout.count("crc ok"), 2)
        # 改坏一个数据字节:固件解析器报告 CRC 错误
        broken = bytearray(pack)
        broken[-5] ^= 0xFF
        path.write_bytes(bytes(broken))
        out = subprocess.run([str(self.dump), str(path)], capture_output=True, text=True)
        self.assertNotEqual(out.returncode, 0)
        self.assertIn("crc BAD", out.stdout)

    def test_pack_header_layout(self):
        pack = music.build_pack([self.fake_track(2, 1)])
        self.assertEqual(pack[:4], b"GDMU")
        version, count, header, total = struct.unpack_from("<HHII", pack, 4)
        self.assertEqual((version, count, header, total), (1, 1, 64, 64 + 1024))
        crc = struct.unpack_from("<I", pack, 16)[0]
        self.assertEqual(crc, zlib.crc32(pack[:16] + b"\0" * 4 + pack[20:64]) & 0xFFFFFFFF)

    def test_parse_ima_wav(self):
        def wav(fact: int | None) -> bytes:
            fmt = struct.pack("<HHIIHHHH", 0x11, 1, 16000, 8110, 1024, 4, 2, 2041)
            chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
            if fact is not None:
                chunks += b"fact" + struct.pack("<I", 4) + struct.pack("<I", fact)
            data = bytes(1024 * 2 + 100)                     # 末尾半块会被丢弃
            chunks += b"data" + struct.pack("<I", len(data)) + data
            return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks

        self.assertEqual(music.parse_ima_wav(wav(3000))[2], 3000)
        rate, align, samples, data = music.parse_ima_wav(wav(None), 4000)
        self.assertEqual((rate, align, samples, len(data)), (16000, 1024, 4000, 2048))
        self.assertEqual(music.parse_ima_wav(wav(None))[2], 2 * 2041)
        with self.assertRaises(ValueError):
            music.parse_ima_wav(b"RIFF\0\0\0\0WAVEjunk")

    def test_tracks_cover_levels_and_fit_partition(self):
        cfg = json.loads(music.TRACKS_JSON.read_text(encoding="utf-8"))
        parsed = levels.load_levels()
        self.assertEqual(sorted(t["track"] for t in cfg["tracks"]), [int(lv.header["track"]) for lv in parsed])
        for t in cfg["tracks"]:
            lv = parsed[t["track"] - 1]
            # 音乐要比关卡长:通关后还有 1.5 s 的完成动画,淡出不能早于终点
            self.assertGreaterEqual(t["cut_s"] - t["fade_s"], levels.finish_seconds(lv) + 1.5, lv.header["name"])
        # 按 ADPCM 约 8 KB/s 估算,全部曲目放得进 music 分区
        estimate = sum(math.ceil(t["cut_s"] * cfg["sample_rate"] / music.samples_per_block(cfg["block_align"]))
                       * cfg["block_align"] for t in cfg["tracks"])
        self.assertLess(estimate + 1024, music.music_partition_size())

    @unittest.skipUnless(os.environ.get("FFMPEG") or shutil.which("ffmpeg"), "ffmpeg not installed")
    def test_decoder_matches_ffmpeg(self):
        ffmpeg = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
        tmp = Path(self.tmp.name)
        src = tmp / "tone.wav"
        subprocess.run([ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                        "aevalsrc=0.4*sin(2*PI*330*t)+0.2*sin(2*PI*1250*t)*sin(3*t):s=16000:d=3",
                        "-ac", "1", "-c:a", "adpcm_ima_wav", "-block_size", "1024", str(src)], check=True)
        reference = tmp / "ref.raw"
        subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(src), "-f", "s16le", str(reference)], check=True)
        rate, align, samples, data = music.parse_ima_wav(src.read_bytes())
        pack_path = tmp / "tone.bin"
        pack_path.write_bytes(music.build_pack([music.Track(1, rate, align, samples, data)]))
        decoded = tmp / "dec.raw"
        subprocess.run([str(self.dump), str(pack_path), "1", str(decoded)], check=True, capture_output=True)
        ref = reference.read_bytes()
        self.assertEqual(decoded.read_bytes()[:len(ref)], ref[:samples * 2])


if __name__ == "__main__":
    unittest.main(verbosity=1)
