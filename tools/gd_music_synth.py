#!/usr/bin/env python3
"""几何冲刺原创配乐的作曲与合成(numpy,确定性)。

每首曲子由 assets/levels/tracks.json 中的参数决定:BPM 与首拍位置和对应关卡完全一致(关卡障碍按这些拍点编排),
调式、和弦进行、音色与随机种子决定旋律。结构是"前奏 → 铺垫 → 主歌 A → 主歌 B → 高潮 → 间奏 → 主歌 A' →
高潮 → … → 尾声",乐器为电子鼓组(底鼓、军鼓、踩镲、镲片)、带侧链起伏的贝斯、脉冲波主旋律、琶音与铺底和弦。
振荡器用加法合成,只取奈奎斯特频率以下的谐波,16 kHz 采样也不产生混叠。

render_track(spec, sample_rate) 返回 int16 numpy 数组(单声道)。相同参数与 numpy 版本得到相同结果。
"""

from __future__ import annotations

import math
import random

import numpy as np

SCALES = {"minor": [0, 2, 3, 5, 7, 8, 10], "major": [0, 2, 4, 5, 7, 9, 11], "dorian": [0, 2, 3, 5, 7, 9, 10]}
KEYS = {"C": 48, "C#": 49, "D": 50, "D#": 51, "E": 52, "F": 53, "F#": 54, "G": 55, "G#": 56, "A": 57, "A#": 58, "B": 59}

# 每小节 16 个十六分音符位置的主旋律节奏型("x" = 起音)
RHYTHMS = [
    "x.x.x.x.x.x.x.x.", "x..x..x.x..x..x.", "x.xx.xx.x.xx.x..", "x...x.x.x...x.x.",
    "x..x..x...x.x...", "x.x..x.xx.x..x..", "xx.x.x.xx.x.x...", "x...x...x.x.x.x.",
]
# 主歌 / 间奏用的节奏型更舒展,高潮用更密的
CALM = [0, 3, 4, 7]
BUSY = [1, 2, 5, 6]


def midi_hz(m: float) -> float:
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


class Mixer:
    def __init__(self, n: int, sr: int) -> None:
        self.sr = sr
        self.buf = np.zeros(n, dtype=np.float64)

    def add(self, start_s: float, sig: np.ndarray, gain: float) -> None:
        i = int(round(start_s * self.sr))
        if i >= len(self.buf) or len(sig) == 0:
            return
        if i < 0:
            sig = sig[-i:]
            i = 0
        end = min(len(self.buf), i + len(sig))
        self.buf[i:end] += sig[:end - i] * gain


def osc(freq: float, n: int, sr: int, kind: str, hmax: int = 64, vibrato: float = 0.0) -> np.ndarray:
    """带限振荡器:saw / square / pulse25 / tri / sine,谐波截止在奈奎斯特频率的 90% 以下。"""
    t = np.arange(n) / sr
    f = np.full(n, freq)
    if vibrato:
        f = freq * (1.0 + vibrato * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - 0.12) / 0.2, 0.0, 1.0))
    phase = 2 * np.pi * np.cumsum(f) / sr
    kmax = max(1, min(hmax, int(sr * 0.45 // freq)))
    out = np.zeros(n)
    for k in range(1, kmax + 1):
        if kind == "sine" and k > 1:
            break
        if kind == "saw":
            out += ((-1) ** (k + 1)) * np.sin(k * phase) / k
        elif kind == "square":
            if k % 2:
                out += np.sin(k * phase) / k
        elif kind == "tri":
            if k % 2:
                out += ((-1) ** ((k - 1) // 2)) * np.sin(k * phase) / (k * k)
        elif kind == "pulse25":
            # 占空比 25% 的脉冲波:Σ sin(πkd)/k · cos(kφ − πkd)
            out += math.sin(math.pi * k * 0.25) / k * np.cos(k * phase - math.pi * k * 0.25)
        else:
            out += np.sin(k * phase) / (k if k > 1 else 1)
    peak = np.max(np.abs(out)) or 1.0
    return out / peak


def adsr(n: int, sr: int, a: float, d: float, s: float, r: float) -> np.ndarray:
    env = np.full(n, s)
    na, nd, nr = int(a * sr), int(d * sr), int(r * sr)
    na = min(na, n)
    if na:
        env[:na] = np.linspace(0, 1, na, endpoint=False)
    nd = min(nd, n - na)
    if nd > 0:
        env[na:na + nd] = np.linspace(1, s, nd, endpoint=False)
    nr = min(nr, n)
    if nr:
        env[n - nr:] *= np.linspace(1, 0, nr)
    return env


def note(freq: float, dur: float, sr: int, kind: str, hmax: int, a: float, d: float, s: float, r: float,
         vibrato: float = 0.0) -> np.ndarray:
    n = max(1, int(dur * sr))
    return osc(freq, n, sr, kind, hmax, vibrato) * adsr(n, sr, a, d, s, r)


class Drums:
    def __init__(self, sr: int, rng: np.random.Generator) -> None:
        self.sr = sr
        t = np.arange(int(0.35 * sr)) / sr
        f = 46 + 120 * np.exp(-t / 0.028)
        self.kick = np.sin(2 * np.pi * np.cumsum(f) / sr) * np.exp(-t / 0.16)
        self.kick[: int(0.004 * sr)] += 0.6 * rng.uniform(-1, 1, int(0.004 * sr))
        t = np.arange(int(0.22 * sr)) / sr
        noise = rng.uniform(-1, 1, len(t))
        self.snare = 0.65 * noise * np.exp(-t / 0.075) + 0.5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t / 0.05)
        t = np.arange(int(0.08 * sr)) / sr
        hn = rng.uniform(-1, 1, len(t) + 1)
        self.hat = np.diff(hn) * 0.5 * np.exp(-t / 0.018)
        t = np.arange(int(0.3 * sr)) / sr
        on = rng.uniform(-1, 1, len(t) + 1)
        self.open_hat = np.diff(on) * 0.5 * np.exp(-t / 0.09)
        t = np.arange(int(1.4 * sr)) / sr
        cn = rng.uniform(-1, 1, len(t) + 1)
        self.crash = np.diff(cn) * 0.5 * np.exp(-t / 0.45)
        self.noise = rng.uniform(-1, 1, sr * 4)


def chord_tones(scale: list[int], degree: int) -> list[int]:
    """音阶第 degree 级上的三和弦(相对主音的半音数)。"""
    out = []
    for step in (0, 2, 4):
        d = degree + step
        out.append(scale[d % 7] + 12 * (d // 7))
    return out


def scale_note(scale: list[int], idx: int) -> int:
    return scale[idx % 7] + 12 * (idx // 7)


def make_phrase(rng: random.Random, scale: list[int], prog: list[int], bar0: int, busy: bool) -> list[tuple]:
    """生成 2 小节旋律:返回 [(小节偏移, 起始 16 分位置, 长度 16 分, 音阶序号)]。"""
    notes = []
    idx = 7 + prog[bar0 % len(prog)]   # 从中音区的和弦根音开始(音阶序号)
    for b in range(2):
        pat = RHYTHMS[rng.choice(BUSY if busy else CALM)]
        onsets = [i for i, c in enumerate(pat) if c == "x"]
        chord = prog[(bar0 + b) % len(prog)]
        tones = [chord + s for s in (0, 2, 4)]
        for j, pos in enumerate(onsets):
            length = (onsets[j + 1] if j + 1 < len(onsets) else 16) - pos
            if pos % 4 == 0:
                # 强拍:落到最近的和弦音
                cands = [t + 7 * o for t in tones for o in (0, 1, 2)]
                idx = min(cands, key=lambda c: (abs(c - idx), c))
            else:
                idx += rng.choice([-2, -1, -1, 1, 1, 2, 0])
            idx = max(5, min(16, idx))
            notes.append((b, pos, length, idx))
    return notes


def render_track(spec: dict, sr: int) -> np.ndarray:
    bpm = float(spec["bpm"])
    first = spec["first_beat_ms"] / 1000.0
    length = float(spec["length_s"])
    fade = float(spec["fade_s"])
    beat = 60.0 / bpm
    bar = 4 * beat
    six = beat / 4
    root = KEYS[spec["key"]]
    scale = SCALES[spec["mode"]]
    prog = list(spec["progression"])
    lead_kind = spec.get("lead", "pulse25")
    rng = random.Random(int(spec["seed"]))
    nrng = np.random.default_rng(int(spec["seed"]))
    drums = Drums(sr, nrng)
    n = int(length * sr)
    mix = Mixer(n, sr)
    bars = int(math.ceil((length - first) / bar))

    # —— 结构:每段 (名称, 小节数) ——
    plan: list[tuple[str, int]] = [("intro", 4), ("build", 4), ("A", 8), ("B", 8), ("drop", 8), ("break", 4)]
    tail = [("A", 8), ("drop", 8), ("B", 8), ("drop", 8), ("break", 4)]
    while sum(b for _, b in plan) < bars:
        plan += tail
    sections: list[str] = []
    for name, cnt in plan:
        sections += [name] * cnt
    sections = sections[:bars]
    sections[-2:] = ["outro"] * min(2, len(sections))

    # 每段的旋律动机(同名段落复用同一动机,形成记忆点)
    motifs: dict[str, list[tuple]] = {}
    for name in ("A", "B", "drop", "break"):
        motifs[name] = make_phrase(rng, scale, prog, 0, busy=name in ("drop", "B"))
        motifs[name + "'"] = make_phrase(rng, scale, prog, 2, busy=name in ("drop", "B"))

    kick_times: list[float] = []
    prev = None
    for b, sec in enumerate(sections):
        t0 = first + b * bar
        chord_deg = prog[b % len(prog)]
        tones = chord_tones(scale, chord_deg)
        new_section = sec != prev
        prev = sec
        if new_section and sec in ("A", "drop", "B") and b > 0:
            mix.add(t0, drums.crash, 0.22)
        # —— 鼓 ——
        if sec not in ("break",):
            for q in range(4):
                if sec == "intro" and q % 2:
                    continue
                mix.add(t0 + q * beat, drums.kick, 0.95)
                kick_times.append(t0 + q * beat)
        if sec in ("A", "B", "drop", "outro"):
            for q in (1, 3):
                mix.add(t0 + q * beat, drums.snare, 0.42)
        if sec == "build":
            pos = b - sections.index("build") if "build" in sections else 0
            hits = 4 if pos < 2 else (8 if pos == 2 else 16)
            for h in range(hits):
                mix.add(t0 + h * bar / hits, drums.snare, 0.18 + 0.22 * h / hits)
            if pos == 3:
                ramp = np.linspace(0, 1, int(bar * sr)) ** 2
                riser = drums.noise[: len(ramp)] * ramp
                mix.add(t0, np.diff(np.concatenate([[0.0], riser])), 0.25)
        if sec != "intro":
            step = 1 if sec == "drop" else 2
            for s16 in range(0, 16, step):
                mix.add(t0 + s16 * six, drums.hat, 0.16 if s16 % 4 == 2 else 0.09)
            if sec == "drop":
                for q in range(4):
                    mix.add(t0 + q * beat + beat / 2, drums.open_hat, 0.09)
        # —— 贝斯:八分音符,高潮段八度跳动 ——
        if sec != "break":
            bass_root = root - 12 + tones[0]
            for e in range(8):
                f = midi_hz(bass_root + (12 if sec == "drop" and e % 2 else 0))
                mix.add(t0 + e * beat / 2, note(f, beat / 2 * 0.92, sr, "saw", 14, 0.004, 0.06, 0.6, 0.02), 0.26)
        # —— 铺底和弦 ——
        if sec in ("intro", "build", "B", "break", "drop"):
            for tn in tones:
                for det in (-0.06, 0.06):
                    f = midi_hz(root + 12 + tn + det)
                    mix.add(t0, note(f, bar, sr, "saw", 5, 0.25, 0.3, 0.8, 0.3), 0.035)
        # —— 琶音:十六分音符 ——
        if sec in ("B", "drop", "break", "build"):
            arp = [tones[0], tones[1], tones[2], tones[1] + 0]
            for s16 in range(16):
                m = root + 24 + arp[s16 % 4] + (12 if (s16 // 4) % 2 and sec == "drop" else 0)
                mix.add(t0 + s16 * six, note(midi_hz(m), six * 0.9, sr, "square", 6, 0.002, 0.05, 0.35, 0.02),
                        0.07 if sec != "break" else 0.09)
        # —— 主旋律 ——
        if sec in ("A", "B", "drop", "break"):
            local = b - (sections.index(sec) if sec in sections else 0)
            key = sec if (local // 2) % 2 == 0 else sec + "'"
            phrase = motifs[key]
            half = (local % 2)
            octave = 12 if sec == "drop" else 0
            for nb, pos, ln, idx in phrase:
                if nb != half:
                    continue
                m = root + 12 + scale_note(scale, idx) + octave
                dur = ln * six
                sig = note(midi_hz(m), dur * 0.95, sr, lead_kind, 12, 0.006, 0.08, 0.72, 0.04, vibrato=0.004)
                mix.add(t0 + pos * six, sig, 0.2 if sec != "break" else 0.14)
                if sec == "drop":   # 高潮段叠一层低八度的方波加厚
                    sig2 = note(midi_hz(m - 12), dur * 0.95, sr, "square", 8, 0.006, 0.08, 0.6, 0.04)
                    mix.add(t0 + pos * six, sig2, 0.08)

    # 侧链:底鼓之后贝斯与铺底一起"呼吸"(对整体做轻微起伏)
    duck = np.ones(n)
    win = np.exp(-np.arange(int(0.12 * sr)) / (0.05 * sr))
    for kt in kick_times:
        i = int(kt * sr)
        j = min(n, i + len(win))
        if i < n:
            duck[i:j] = np.minimum(duck[i:j], 1 - 0.35 * win[: j - i])
    out = mix.buf * duck
    # 母带:软削波、归一化、淡出
    out = np.tanh(1.3 * out) / np.tanh(1.3)
    peak = np.max(np.abs(out)) or 1.0
    out = out / peak * 0.89
    nf = int(fade * sr)
    out[n - nf:] *= np.linspace(1, 0, nf)
    return np.round(out * 32767).astype(np.int16)
