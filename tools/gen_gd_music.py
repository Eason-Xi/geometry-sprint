#!/usr/bin/env python3
"""几何冲刺音乐包生成:本地原曲 → 16 kHz 单声道 IMA-ADPCM → build/gd_music/gd_music.bin。

原曲有版权:只在本地转码打包,音乐包与原曲都不进 git,带音乐的固件仅供个人设备使用(PRD §2)。

用法(仓库根目录):
  python3 tools/gen_gd_music.py [--bgm-dir BGM] [--out build/gd_music]
    --bgm-dir 默认取环境变量 GD_BGM_DIR,否则为仓库根目录下的 BGM/。
    ffmpeg 取环境变量 FFMPEG,否则从 PATH 查找。
曲目表 assets/levels/tracks.json 给出每首的文件名匹配规则(不区分大小写的子串)、截取长度与淡出长度。
找不到某首原曲时给出警告并只打包找到的曲目;一首都没有时失败。

生成后重新构建固件:main/CMakeLists.txt 发现音乐包就把它注册到 "music" 分区,
./tools/validate.sh 的 merge-bin 会把它并入 build/FoloToy-AI-Passport-full.bin。
包格式见 main/gd_mpack.h。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACKS_JSON = ROOT / "assets" / "levels" / "tracks.json"
PARTITIONS = ROOT / "partitions.csv"
MAGIC = b"GDMU"
VERSION = 1
HEADER = 32
ENTRY = 32
MAX_TRACKS = 8
AUDIO_EXT = {".mp3", ".ogg", ".wav", ".m4a", ".flac", ".aac"}


@dataclass
class Track:
    number: int
    sample_rate: int
    block_align: int
    samples: int
    data: bytes


def samples_per_block(block_align: int) -> int:
    return (block_align - 4) * 2 + 1


def parse_ima_wav(raw: bytes, expect_samples: int | None = None) -> tuple[int, int, int, bytes]:
    """解析 WAV(IMA ADPCM,单声道),返回 (采样率, 块长, 采样数, 数据)。

    ffmpeg 输出到管道时无法回写 fact 块与长度字段:此时采样数取 expect_samples(按截取时长算),
    不超过整块容量。"""
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE stream")
    pos = 12
    fmt = None
    fact = None
    data = None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack_from("<I", raw, pos + 4)[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"fact":
            fact = struct.unpack_from("<I", body, 0)[0]
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("WAV is missing fmt or data")
    tag, channels, rate, _, align, _ = struct.unpack_from("<HHIIHH", fmt, 0)
    if tag != 0x11 or channels != 1:
        raise ValueError(f"expected mono IMA ADPCM (0x11), got tag 0x{tag:x} with {channels} channels")
    whole = len(data) // align * align
    data = data[:whole]
    max_samples = whole // align * samples_per_block(align)
    if fact:
        samples = min(fact, max_samples)
    elif expect_samples:
        samples = min(expect_samples, max_samples)
    else:
        samples = max_samples
    return rate, align, samples, data


def build_pack(tracks: list[Track]) -> bytes:
    if not tracks or len(tracks) > MAX_TRACKS:
        raise ValueError("music pack needs 1..8 tracks")
    header_size = HEADER + ENTRY * len(tracks)
    entries = b""
    payload = b""
    offset = header_size
    for t in tracks:
        if len(t.data) % t.block_align:
            raise ValueError(f"track {t.number}: data is not a whole number of blocks")
        entries += struct.pack("<BBHIIIII8x", t.number, 0, t.block_align, t.sample_rate, offset, len(t.data),
                               t.samples, zlib.crc32(t.data) & 0xFFFFFFFF)
        payload += t.data
        offset += len(t.data)
    total = offset
    head = MAGIC + struct.pack("<HHII", VERSION, len(tracks), header_size, total)
    without_crc = head + b"\x00" * 4 + b"\x00" * 12 + entries
    crc = zlib.crc32(without_crc) & 0xFFFFFFFF
    header = head + struct.pack("<I", crc) + b"\x00" * 12 + entries
    assert len(header) == header_size
    return header + payload


def music_partition_size() -> int:
    for line in PARTITIONS.read_text(encoding="utf-8").splitlines():
        fields = [f.strip() for f in line.split(",")]
        if fields and fields[0] == "music":
            return int(fields[4], 0)
    raise SystemExit("ERROR: partitions.csv has no 'music' partition")


def find_source(bgm: Path, match: str) -> Path | None:
    hits = sorted(p for p in bgm.iterdir() if p.suffix.lower() in AUDIO_EXT and match.lower() in p.name.lower())
    if len(hits) > 1:
        raise SystemExit(f"ERROR: {match!r} matches several files: {[h.name for h in hits]}")
    return hits[0] if hits else None


def transcode(ffmpeg: str, src: Path, cfg: dict, entry: dict) -> bytes:
    cut, fade = float(entry["cut_s"]), float(entry["fade_s"])
    filters = f"volume={cfg.get('volume', 1.0)},afade=t=out:st={cut - fade}:d={fade}"
    cmd = [ffmpeg, "-v", "error", "-nostdin", "-i", str(src), "-t", str(cut), "-af", filters, "-ac", "1",
           "-ar", str(cfg["sample_rate"]), "-c:a", "adpcm_ima_wav", "-block_size", str(cfg["block_align"]),
           "-f", "wav", "-"]
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(f"ERROR: ffmpeg failed for {src.name}:\n{result.stderr.decode(errors='replace')}")
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bgm-dir", default=os.environ.get("GD_BGM_DIR", str(ROOT / "BGM")))
    parser.add_argument("--out", default=str(ROOT / "build" / "gd_music"))
    args = parser.parse_args()

    cfg = json.loads(TRACKS_JSON.read_text(encoding="utf-8"))
    ffmpeg = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ERROR: ffmpeg not found; install it or set FFMPEG=/path/to/ffmpeg")
    bgm = Path(args.bgm_dir)
    if not bgm.is_dir():
        raise SystemExit(f"ERROR: BGM directory not found: {bgm}")

    tracks: list[Track] = []
    sources: dict[str, dict] = {}
    for entry in cfg["tracks"]:
        src = find_source(bgm, entry["match"])
        if src is None:
            print(f"WARNING: track {entry['track']} ({entry['match']}) not found in {bgm}; it will play silently",
                  file=sys.stderr)
            continue
        expect = int(round(float(entry["cut_s"]) * cfg["sample_rate"]))
        rate, align, samples, data = parse_ima_wav(transcode(ffmpeg, src, cfg, entry), expect)
        if rate != cfg["sample_rate"] or align != cfg["block_align"]:
            raise SystemExit(f"ERROR: ffmpeg produced {rate} Hz / block {align} for {src.name}")
        tracks.append(Track(int(entry["track"]), rate, align, samples, data))
        sources[str(entry["track"])] = {
            "file": src.name, "source_sha256": hashlib.sha256(src.read_bytes()).hexdigest(),
            "bytes": len(data), "samples": samples, "seconds": round(samples / rate, 2),
        }
        print(f"track {entry['track']}: {src.name} -> {len(data)} bytes, {samples / rate:.1f} s")
    if not tracks:
        raise SystemExit("ERROR: no source tracks found; nothing to pack")

    pack = build_pack(tracks)
    capacity = music_partition_size()
    if len(pack) > capacity:
        raise SystemExit(f"ERROR: music pack is {len(pack)} bytes; the music partition holds {capacity}")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "gd_music.bin").write_bytes(pack)
    manifest = {"pack_bytes": len(pack), "partition_bytes": capacity, "pack_sha256": hashlib.sha256(pack).hexdigest(),
                "tracks": sources, "note": "Local private build artifact. Do not commit or redistribute."}
    (out / "gd_music.manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out / 'gd_music.bin'}: {len(pack)} bytes ({len(pack) * 100 // capacity}% of the music partition)")
    print("Rebuild the firmware (./tools/validate.sh) to merge the music pack into the full image.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
