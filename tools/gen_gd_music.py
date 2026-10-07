#!/usr/bin/env python3
"""几何冲刺音乐包生成:原创配乐 → 16 kHz 单声道 IMA-ADPCM → build/gd_music/gd_music.bin。

配乐全部由 tools/gd_music_synth.py 按 assets/levels/tracks.json 的参数作曲合成(原创,随固件一起分发),
BPM 与首拍位置和对应关卡一致,障碍按拍点编排。需要 numpy。

用法(仓库根目录):
  python3 tools/gen_gd_music.py [--out build/gd_music] [--wav-dir DIR]
    --wav-dir  另外写出每首的 16 kHz WAV,便于试听(开发辅助)。
生成后重新构建固件:main/CMakeLists.txt 发现音乐包就把它注册到 "music" 分区,
./tools/validate.sh 的 merge-bin 会把它并入 build/FoloToy-AI-Passport-full.bin。包格式见 main/gd_mpack.h。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import wave
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

STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
]
ADJ = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


@dataclass
class Track:
    number: int
    sample_rate: int
    block_align: int
    samples: int
    data: bytes


def samples_per_block(block_align: int) -> int:
    return (block_align - 4) * 2 + 1


def encode_ima(pcm: list[int], block_align: int) -> bytes:
    """IMA-ADPCM(WAV 单声道块格式)。重建用 ((2Δ+1)·step)/8,与 main/gd_mpack.c 的解码器一致。"""
    spb = samples_per_block(block_align)
    out = bytearray()
    idx = 0
    pos = 0
    total = len(pcm)
    while pos < total:
        block = pcm[pos:pos + spb]
        if len(block) < spb:
            block = block + [block[-1] if block else 0] * (spb - len(block))
        pred = block[0]
        out += struct.pack("<hBB", pred, idx, 0)
        nibbles = []
        for s in block[1:]:
            step = STEP[idx]
            diff = s - pred
            sign = 8 if diff < 0 else 0
            mag = min(7, abs(diff) * 4 // step)
            pred += -(((2 * mag + 1) * step) >> 3) if sign else (((2 * mag + 1) * step) >> 3)
            pred = 32767 if pred > 32767 else (-32768 if pred < -32768 else pred)
            nib = sign | mag
            idx += ADJ[nib]
            idx = 0 if idx < 0 else (88 if idx > 88 else idx)
            nibbles.append(nib)
        for i in range(0, len(nibbles), 2):
            out.append(nibbles[i] | (nibbles[i + 1] << 4))
        pos += spb
    return bytes(out)


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
    head = MAGIC + struct.pack("<HHII", VERSION, len(tracks), header_size, offset)
    crc = zlib.crc32(head + b"\x00" * 16 + entries) & 0xFFFFFFFF
    header = head + struct.pack("<I", crc) + b"\x00" * 12 + entries
    assert len(header) == header_size
    return header + payload


def music_partition_size() -> int:
    for line in PARTITIONS.read_text(encoding="utf-8").splitlines():
        fields = [f.strip() for f in line.split(",")]
        if fields and fields[0] == "music":
            return int(fields[4], 0)
    raise SystemExit("ERROR: partitions.csv has no 'music' partition")


def load_config() -> dict:
    return json.loads(TRACKS_JSON.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "build" / "gd_music"))
    parser.add_argument("--wav-dir", help="also write each track as a 16 kHz WAV for listening")
    args = parser.parse_args()
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import gd_music_synth as synth
    except ImportError as exc:
        raise SystemExit(f"ERROR: {exc}; the composer needs numpy (pip install numpy)")

    cfg = load_config()
    rate, align = int(cfg["sample_rate"]), int(cfg["block_align"])
    tracks: list[Track] = []
    info: dict[str, dict] = {}
    for spec in cfg["tracks"]:
        pcm = synth.render_track(spec, rate)
        samples = len(pcm)
        data = encode_ima(pcm.tolist(), align)
        tracks.append(Track(int(spec["track"]), rate, align, samples, data))
        info[str(spec["track"])] = {"title": spec["title"], "bytes": len(data), "samples": samples,
                                    "pcm_sha256": hashlib.sha256(pcm.tobytes()).hexdigest()}
        if args.wav_dir:
            wav_dir = Path(args.wav_dir)
            wav_dir.mkdir(parents=True, exist_ok=True)
            with wave.open(str(wav_dir / f"track_{spec['track']}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(pcm.tobytes())
        print(f"track {spec['track']}: {spec['title']} ({spec['bpm']} BPM) -> {len(data)} bytes, {samples / rate:.1f} s")

    pack = build_pack(tracks)
    capacity = music_partition_size()
    if len(pack) > capacity:
        raise SystemExit(f"ERROR: music pack is {len(pack)} bytes; the music partition holds {capacity}")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "gd_music.bin").write_bytes(pack)
    manifest = {"pack_bytes": len(pack), "partition_bytes": capacity, "pack_sha256": hashlib.sha256(pack).hexdigest(),
                "tracks": info, "note": "Original Geometry Sprint soundtrack generated by tools/gd_music_synth.py."}
    (out / "gd_music.manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out / 'gd_music.bin'}: {len(pack)} bytes ({len(pack) * 100 // capacity}% of the music partition)")
    print("Rebuild the firmware (./tools/validate.sh) to merge the music pack into the full image.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
