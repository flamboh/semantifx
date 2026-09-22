#!/usr/bin/env python3
"""T12: offline render tool gallery and listening-test report.

Builds the JUCE-free ``semantifx_render`` tool, renders the benchmark guitar
recording through a gallery of single-effect presets and chains, and writes:

- ``/tmp/sfx/renders/<preset>.wav`` -- 32-bit float renders to audition
- ``/tmp/sfx/renders/report.md`` -- level/click/DC table for review
- ``/tmp/sfx/renders/spectrograms.png`` -- input/output log spectrograms
- ``/tmp/sfx/renders/responses.png`` -- EQ response + compressor GR plots

Run with the inference venv (numpy/scipy/soundfile/matplotlib) or via
``scripts/render-gallery``. Never modifies the effect sources; defects found
while listening-by-eye are described in the console report, not fixed here.
"""

from __future__ import annotations

import concurrent.futures
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO / "plugin" / "build-dsp"
RENDER_TOOL = BUILD_DIR / "semantifx_render"
OUT_DIR = Path("/tmp/sfx/renders")

SAMPLE_RATE = 44_100  # expected; verified against the actual input file

# (preset, [(effect type, {param: value})]). Empty chain = dry (bit-transparency).
EQ_MAX_MIN = {
    "low_shelf_gain_db": (-20.0, 20.0),
    "low_shelf_cutoff_freq": (20.0, 2000.0),
    "low_shelf_q_factor": (0.1, 6.0),
    "band0_gain_db": (-20.0, 20.0),
    "band0_cutoff_freq": (80.0, 2000.0),
    "band0_q_factor": (0.1, 6.0),
    "band1_gain_db": (-20.0, 20.0),
    "band1_cutoff_freq": (2000.0, 8000.0),
    "band1_q_factor": (0.1, 6.0),
    "band2_gain_db": (-20.0, 20.0),
    "band2_cutoff_freq": (8000.0, 12000.0),
    "band2_q_factor": (0.1, 6.0),
    "band3_gain_db": (-20.0, 20.0),
    "band3_cutoff_freq": (12000.0, 21050.0),
    "band3_q_factor": (0.1, 6.0),
    "high_shelf_gain_db": (-20.0, 20.0),
    "high_shelf_cutoff_freq": (4000.0, 21050.0),
    "high_shelf_q_factor": (0.1, 6.0),
}
DIST_MAX_MIN = {
    "drive_db": (0.0, 48.0),
    "character": (0.0, 1.0),
    "tone_db": (-12.0, 12.0),
    "mix": (0.0, 1.0),
}
COMP_MAX_MIN = {
    "threshold_db": (-60.0, 0.0),
    "ratio": (1.0, 20.0),
    "attack_ms": (5.0, 100.0),
    "knee_db": (1.0, 12.0),
    "makeup_gain_db": (0.0, 12.0),
}
REVERB_MAX_MIN = {
    "decay": (0.0, 1.0),
    "damping": (0.0, 1.0),
    "mix": (0.0, 1.0),
}


def _extreme(spec: dict[str, tuple[float, float]], which: int) -> dict[str, float]:
    return {name: bounds[which] for name, bounds in spec.items()}


EQ_WARM = {
    "low_shelf_gain_db": 4.0,
    "low_shelf_cutoff_freq": 200.0,
    "band1_gain_db": -3.0,
    "band1_cutoff_freq": 3500.0,
}
EQ_TELEPHONE = {
    "low_shelf_gain_db": -20.0,
    "low_shelf_cutoff_freq": 300.0,
    # Spec asks for a 3.4 kHz high-shelf cutoff, but EffectParameters.h limits
    # high_shelf_cutoff_freq to [4000, 21050]; use the minimum and note it.
    "high_shelf_gain_db": -20.0,
    "high_shelf_cutoff_freq": 4000.0,
    "band0_gain_db": 6.0,
    "band0_cutoff_freq": 1000.0,
    "band0_q_factor": 1.0,
}
COMP_GENTLE = {"threshold_db": -18.0, "ratio": 2.0, "attack_ms": 20.0}
REVERB_ROOM = {"decay": 0.3, "damping": 0.5, "mix": 0.25}
DIST_LIGHT = {"drive_db": 12.0, "mix": 0.5}

GALLERY: list[tuple[str, list[tuple[str, dict[str, float]]]]] = [
    ("dry", []),
    ("eq_default", [("eq", {})]),
    ("distortion_default", [("distortion", {})]),
    ("compressor_default", [("compressor", {})]),
    ("reverb_default", [("reverb", {})]),
    (
        "eq_bright",
        [("eq", {"high_shelf_gain_db": 6.0, "high_shelf_cutoff_freq": 8000.0})],
    ),
    ("eq_warm", [("eq", dict(EQ_WARM))]),
    ("eq_telephone", [("eq", dict(EQ_TELEPHONE))]),
    ("dist_light", [("distortion", dict(DIST_LIGHT))]),
    (
        "dist_heavy",
        [("distortion", {"drive_db": 40.0, "character": 0.9, "mix": 1.0})],
    ),
    ("comp_gentle", [("compressor", dict(COMP_GENTLE))]),
    (
        "comp_squash",
        [
            (
                "compressor",
                {
                    "threshold_db": -40.0,
                    "ratio": 20.0,
                    "attack_ms": 5.0,
                    "makeup_gain_db": 12.0,
                },
            )
        ],
    ),
    ("reverb_room", [("reverb", dict(REVERB_ROOM))]),
    (
        "reverb_cathedral",
        [("reverb", {"decay": 1.0, "damping": 0.1, "mix": 0.6})],
    ),
    (
        "chain_warm_room",
        [("eq", dict(EQ_WARM)), ("compressor", dict(COMP_GENTLE)),
         ("reverb", dict(REVERB_ROOM))],
    ),
    (
        "chain_lofi",
        [("eq", dict(EQ_TELEPHONE)), ("distortion", dict(DIST_LIGHT))],
    ),
    (
        "chain_everything_max",
        [
            ("eq", _extreme(EQ_MAX_MIN, 1)),
            ("distortion", _extreme(DIST_MAX_MIN, 1)),
            ("compressor", _extreme(COMP_MAX_MIN, 1)),
            ("reverb", _extreme(REVERB_MAX_MIN, 1)),
        ],
    ),
    (
        "chain_everything_min",
        [
            ("eq", _extreme(EQ_MAX_MIN, 0)),
            ("distortion", _extreme(DIST_MAX_MIN, 0)),
            ("compressor", _extreme(COMP_MAX_MIN, 0)),
            ("reverb", _extreme(REVERB_MAX_MIN, 0)),
        ],
    ),
]

BLOCK_CHECK_PRESETS = ("reverb_cathedral", "comp_squash", "chain_warm_room")
BLOCK_SIZES = (64, 4096)
BLOCK_TOLERANCE = 1e-5


def run(cmd: list[str]) -> str:
    completed = subprocess.run(cmd, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(cmd)}\n"
            f"{completed.stderr.strip()}"
        )
    return completed.stdout


def ensure_tool() -> None:
    run([
        "cmake", "-S", str(REPO / "plugin"), "-B", str(BUILD_DIR),
        "-DSEMANTIFX_DSP_ONLY=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
    ])
    run([
        "cmake", "--build", str(BUILD_DIR), "--target", "semantifx_render",
        "-j", "8",
    ])
    if not RENDER_TOOL.is_file():
        raise RuntimeError(f"render tool missing after build: {RENDER_TOOL}")


def pick_sources() -> tuple[Path, list[Path]]:
    """Return (primary, alternates-that-differ)."""
    candidates = [
        REPO / "inference" / "benchmarks" / "audio" / "guitar.wav",
        REPO / "web" / "public" / "original-guitar.wav",
    ]
    existing = [path for path in candidates if path.is_file()]
    if not existing:
        raise RuntimeError(
            "no input audio found; want inference/benchmarks/audio/guitar.wav "
            "or web/public/original-guitar.wav"
        )
    primary = existing[0]
    alternates = []
    primary_bytes = primary.read_bytes()
    for path in existing[1:]:
        if path.read_bytes() != primary_bytes:
            alternates.append(path)
    return primary, alternates


def effect_args(chain: list[tuple[str, dict[str, float]]]) -> list[str]:
    args: list[str] = []
    for effect_type, params in chain:
        if params:
            spec = effect_type + ":" + ",".join(
                f"{name}={value}" for name, value in params.items()
            )
        else:
            spec = effect_type
        args += ["--effect", spec]
    return args


def chain_string(chain: list[tuple[str, dict[str, float]]]) -> str:
    if not chain:
        return "dry (no effects)"
    parts = []
    for effect_type, params in chain:
        if params:
            parts.append(
                effect_type
                + "("
                + ", ".join(f"{name}={value:g}" for name, value in params.items())
                + ")"
            )
        else:
            parts.append(f"{effect_type}(defaults)")
    return " -> ".join(parts)


def render(
    source: Path, chain: list[tuple[str, dict[str, float]]],
    destination: Path, block: int = 512,
) -> str:
    cmd = [str(RENDER_TOOL), str(source), str(destination), "--block", str(block)]
    cmd += effect_args(chain)
    return run(cmd)


def to_mono(audio: np.ndarray) -> np.ndarray:
    if audio.ndim == 1:
        return audio.astype(np.float64)
    return np.mean(audio.astype(np.float64), axis=1)


def db(value: float) -> float:
    return 20.0 * np.log10(value) if value > 0 else float("-inf")


def fmt_db(value: float) -> str:
    return "-inf" if value == float("-inf") else f"{value:.2f}"


def analyse(
    input_mono: np.ndarray, output: np.ndarray, sample_rate: int
) -> dict[str, object]:
    _ = sample_rate
    flat = output.astype(np.float64).reshape(-1)
    peak = float(np.max(np.abs(flat))) if flat.size else 0.0
    rms = float(np.sqrt(np.mean(flat**2))) if flat.size else 0.0
    crest = peak / rms if rms > 0 else float("inf")
    clipping = int(np.sum(np.abs(flat) >= 1.0))
    bad = int(np.sum(~np.isfinite(flat)))
    finite = flat[np.isfinite(flat)]
    dc = float(np.mean(finite)) if finite.size else float("nan")
    dc_db = db(abs(dc)) if np.isfinite(dc) else float("nan")

    def max_jump(signal: np.ndarray) -> float:
        mono = to_mono(signal)
        if mono.size < 2:
            return 0.0
        with np.errstate(invalid="ignore"):
            jumps = np.abs(np.diff(mono[np.isfinite(mono)]))
        return float(np.max(jumps)) if jumps.size else 0.0

    jump_out = max_jump(output)
    jump_in = max_jump(input_mono)
    ratio = jump_out / jump_in if jump_in > 0 else (
        float("inf") if jump_out > 0 else 1.0)

    flags = []
    if clipping > 0:
        flags.append("CLIP")
    if bad > 0:
        flags.append("NAN/INF")
    if np.isfinite(dc_db) and dc_db > -40.0:
        flags.append("DC")
    if np.isfinite(ratio) and ratio > 3.0:
        flags.append("CLICK-RISK")
    return {
        "peak_db": db(peak),
        "rms_db": db(rms),
        "crest": crest,
        "clipping": clipping,
        "bad": bad,
        "dc": dc,
        "dc_db": dc_db,
        "jump_out": jump_out,
        "jump_in": jump_in,
        "jump_ratio": ratio,
        "flags": flags,
    }


def write_report(
    path: Path, source: Path, sample_rate: int,
    rows: list[tuple[str, str, dict[str, object]]],
    block_rows: list[tuple[str, int, float, bool]],
    alternates: list[Path],
) -> None:
    lines = [
        "# Render gallery report",
        "",
        f"Source: `{source}` (sample rate {sample_rate} Hz, block 512 unless noted).",
    ]
    if alternates:
        lines.append(
            "Also rendered (differing second source): "
            + ", ".join(f"`{p}`" for p in alternates)
            + " with an `alt_` filename prefix."
        )
    lines += [
        "",
        "| preset | chain | peak dBFS | RMS dBFS | crest | clip>=0dBFS | NaN/inf | "
        "DC offset | max jump out (in) [ratio] | flags |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for preset, chain, metrics in rows:
        assert isinstance(metrics["peak_db"], float)
        dc = metrics["dc"]
        dc_db = metrics["dc_db"]
        dc_text = (
            f"{dc:+.5f} ({fmt_db(dc_db)} dBFS)"
            if np.isfinite(dc) and np.isfinite(dc_db)
            else "n/a"
        )
        flags = ",".join(metrics["flags"]) if metrics["flags"] else "ok"  # type: ignore[union-attr]
        lines.append(
            f"| {preset} | {chain} | {fmt_db(metrics['peak_db'])} "
            f"| {fmt_db(metrics['rms_db'])} | {metrics['crest']:.2f} "
            f"| {metrics['clipping']} | {metrics['bad']} | {dc_text} "
            f"| {metrics['jump_out']:.4f} ({metrics['jump_in']:.4f}) "
            f"[{metrics['jump_ratio']:.2f}] | {flags} |"
        )
    lines += [
        "",
        "## Block-size independence",
        "",
        "Same preset rendered with `--block 64` / `--block 4096`, compared "
        f"sample-by-sample against the block-512 render (tolerance {BLOCK_TOLERANCE:g}):",
        "",
        "| preset | block | max abs diff | verdict |",
        "|---|---|---|---|",
    ]
    for preset, block, diff, passed in block_rows:
        lines.append(
            f"| {preset} | {block} | {diff:.3g} | {'PASS' if passed else 'FAIL'} |"
        )
    lines += [
        "",
        "## Flagged presets",
        "",
    ]
    flagged = [(preset, m) for preset, _, m in rows if m["flags"]]
    if not flagged:
        lines.append("None.")
    else:
        for preset, metrics in flagged:
            lines.append(f"- `{preset}`: {','.join(metrics['flags'])}")  # type: ignore[union-attr]
    lines += [
        "",
        "Flag rules: CLIP = any sample >= 0 dBFS; NAN/INF = non-finite sample; "
        "DC = |mean| above -40 dBFS; CLICK-RISK = output max single-sample jump "
        "more than 3x the input jump.",
        "",
        "## Notes",
        "",
        "- `inference/benchmarks/audio/guitar.wav` does not exist in the repo "
        "(audio is gitignored), so the gallery renders "
        "`web/public/original-guitar.wav` (mono 16-bit PCM, 44100 Hz).",
        "- `eq_telephone` (and `chain_lofi`) use high_shelf_cutoff_freq=4000 Hz, "
        "the catalogue minimum: the spec's 3.4 kHz is below the valid range "
        "[4000, 21050] and `semantifx_render` rejects it.",
        "- `dist_light` is deliberately subtle (max sample change 0.0016): the "
        "waveshapers normalize low-level gain to ~1, so drive adds harmonics "
        "without a loudness jump; mix 0.5 halves the rest. Checked against "
        "`DistortionMath::shape`, not a defect.",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def _spectrogram(
    mono: np.ndarray, sample_rate: int, nperseg: int = 4096
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    from scipy.signal import stft

    _, _, values = stft(
        mono, fs=sample_rate, window="hann", nperseg=nperseg,
        noverlap=nperseg * 3 // 4, boundary=None, padded=False,
    )
    magnitude_db = 20.0 * np.log10(np.abs(values) + 1e-12)
    freqs = np.fft.rfftfreq(nperseg, 1.0 / sample_rate)
    times = np.arange(values.shape[1]) * (nperseg // 4) / sample_rate
    return freqs, times, magnitude_db


def write_spectrograms(
    path: Path, source: Path, renders: list[tuple[str, Path]]
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    input_audio, sample_rate = sf.read(str(source), dtype="float32", always_2d=True)
    input_mono = to_mono(input_audio)
    _, _, input_spec = _spectrogram(input_mono, sample_rate)

    specs: list[tuple[str, np.ndarray]] = []
    for preset, wav in renders:
        audio, rate = sf.read(str(wav), dtype="float32", always_2d=True)
        assert rate == sample_rate, f"sample-rate mismatch in {wav}"
        _, _, spec = _spectrogram(to_mono(audio), sample_rate)
        specs.append((preset, spec))

    freqs = np.fft.rfftfreq(4096, 1.0 / sample_rate)
    band = (freqs >= 20.0) & (freqs <= 20000.0)
    # Shared scale anchored to the input peak so musical presets stay legible;
    # hotter-than-input presets (cathedral, everything_max) saturate honestly.
    vmax = float(np.max(input_spec[band]))
    vmin = vmax - 120.0

    rows = len(specs)
    figure, axes = plt.subplots(rows, 2, figsize=(20, 2.4 * rows), squeeze=False)
    mesh = None
    for row, (preset, spec) in enumerate(specs):
        for column, (title, data) in enumerate(
            (("input", input_spec), ("output", spec))
        ):
            axis = axes[row][column]
            mesh = axis.pcolormesh(
                np.arange(data.shape[1]) * 1024 / sample_rate,
                freqs, data, vmin=vmin, vmax=vmax, shading="auto",
            )
            axis.set_yscale("log")
            axis.set_ylim(20.0, 20000.0)
            axis.set_title(f"{preset} -- {title}", fontsize=10)
            if row == rows - 1:
                axis.set_xlabel("time (s)")
            if column == 0:
                axis.set_ylabel("freq (Hz)")
    figure.subplots_adjust(left=0.06, right=0.9, top=0.97, bottom=0.03, hspace=0.5)
    figure.suptitle(
        f"log spectrograms, shared {vmin:.0f}..{vmax:.0f} dB scale", fontsize=12
    )
    colorbar_axis = figure.add_axes([0.92, 0.05, 0.015, 0.9])
    figure.colorbar(mesh, cax=colorbar_axis, label="dB")
    figure.savefig(path, dpi=100)
    plt.close(figure)


# --- Intended EQ curves (port of plugin/src/EqMath.h, RBJ cookbook) ---

def _clamp_freq(freq: float, sample_rate: float) -> float:
    return min(max(freq, 10.0), sample_rate * 0.49)


def _peak(sample_rate: float, freq: float, q: float, gain_db: float):
    freq, q = _clamp_freq(freq, sample_rate), max(q, 0.01)
    big_a = 10.0 ** (gain_db / 40.0)
    omega = 2.0 * np.pi * freq / sample_rate
    alpha = np.sin(omega) / (2.0 * q)
    cosine = np.cos(omega)
    a0 = 1.0 + alpha / big_a
    return (
        (1.0 + alpha * big_a) / a0, (-2.0 * cosine) / a0,
        (1.0 - alpha * big_a) / a0, (-2.0 * cosine) / a0,
        (1.0 - alpha / big_a) / a0,
    )


def _low_shelf(sample_rate: float, freq: float, q: float, gain_db: float):
    freq, q = _clamp_freq(freq, sample_rate), max(q, 0.01)
    big_a = 10.0 ** (gain_db / 40.0)
    omega = 2.0 * np.pi * freq / sample_rate
    cosine, sine = np.cos(omega), np.sin(omega)
    alpha = sine / (2.0 * q)
    root_a = np.sqrt(big_a)
    a0 = (big_a + 1.0) + (big_a - 1.0) * cosine + 2.0 * root_a * alpha
    return (
        big_a * ((big_a + 1.0) - (big_a - 1.0) * cosine + 2.0 * root_a * alpha) / a0,
        2.0 * big_a * ((big_a - 1.0) - (big_a + 1.0) * cosine) / a0,
        big_a * ((big_a + 1.0) - (big_a - 1.0) * cosine - 2.0 * root_a * alpha) / a0,
        -2.0 * ((big_a - 1.0) + (big_a + 1.0) * cosine) / a0,
        ((big_a + 1.0) + (big_a - 1.0) * cosine - 2.0 * root_a * alpha) / a0,
    )


def _high_shelf(sample_rate: float, freq: float, q: float, gain_db: float):
    freq, q = _clamp_freq(freq, sample_rate), max(q, 0.01)
    big_a = 10.0 ** (gain_db / 40.0)
    omega = 2.0 * np.pi * freq / sample_rate
    cosine, sine = np.cos(omega), np.sin(omega)
    alpha = sine / (2.0 * q)
    root_a = np.sqrt(big_a)
    a0 = (big_a + 1.0) - (big_a - 1.0) * cosine + 2.0 * root_a * alpha
    return (
        big_a * ((big_a + 1.0) + (big_a - 1.0) * cosine + 2.0 * root_a * alpha) / a0,
        -2.0 * big_a * ((big_a - 1.0) + (big_a + 1.0) * cosine) / a0,
        big_a * ((big_a + 1.0) + (big_a - 1.0) * cosine - 2.0 * root_a * alpha) / a0,
        2.0 * ((big_a - 1.0) - (big_a + 1.0) * cosine) / a0,
        ((big_a + 1.0) - (big_a - 1.0) * cosine - 2.0 * root_a * alpha) / a0,
    )


def _magnitude_db(coeffs, freq: float, sample_rate: float) -> float:
    b0, b1, b2, a1, a2 = coeffs
    omega = 2.0 * np.pi * freq / sample_rate
    z1r, z1i = np.cos(omega), -np.sin(omega)
    z2r, z2i = np.cos(2.0 * omega), -np.sin(2.0 * omega)
    num = (b0 + b1 * z1r + b2 * z2r) ** 2 + (b1 * z1i + b2 * z2i) ** 2
    den = (1.0 + a1 * z1r + a2 * z2r) ** 2 + (a1 * z1i + a2 * z2i) ** 2
    return 10.0 * np.log10(num / den)


EQ_DEFAULT_TRIPLES = [  # (gain, freq, q) x6 bands; gains default to 0
    (0.0, 200.0, 0.7071),
    (0.0, 500.0, 0.7071),
    (0.0, 3500.0, 0.7071),
    (0.0, 9000.0, 0.7071),
    (0.0, 14000.0, 0.7071),
    (0.0, 12000.0, 0.7071),
]
EQ_PARAM_NAMES = [
    "low_shelf_gain_db", "low_shelf_cutoff_freq", "low_shelf_q_factor",
    "band0_gain_db", "band0_cutoff_freq", "band0_q_factor",
    "band1_gain_db", "band1_cutoff_freq", "band1_q_factor",
    "band2_gain_db", "band2_cutoff_freq", "band2_q_factor",
    "band3_gain_db", "band3_cutoff_freq", "band3_q_factor",
    "high_shelf_gain_db", "high_shelf_cutoff_freq", "high_shelf_q_factor",
]


def intended_eq_curve(
    overrides: dict[str, float], frequencies: np.ndarray, sample_rate: float
) -> np.ndarray:
    values = dict(zip(EQ_PARAM_NAMES, [v for triple in EQ_DEFAULT_TRIPLES for v in triple]))
    values.update(overrides)
    triples = [
        (values[EQ_PARAM_NAMES[i]], values[EQ_PARAM_NAMES[i + 1]], values[EQ_PARAM_NAMES[i + 2]])
        for i in range(0, 18, 3)
    ]
    curve = np.zeros_like(frequencies)
    for band, (gain, freq, q) in enumerate(triples):
        coeffs = (
            _low_shelf(sample_rate, freq, q, gain)
            if band == 0
            else _high_shelf(sample_rate, freq, q, gain)
            if band == 5
            else _peak(sample_rate, freq, q, gain)
        )
        for i, frequency in enumerate(frequencies):
            curve[i] += _magnitude_db(coeffs, float(frequency), sample_rate)
    return curve


def write_responses(path: Path, sample_rate: int) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from scipy.signal import csd, welch

    rng = np.random.default_rng(20260922)
    seconds = 10.0
    noise = rng.standard_normal(round(seconds * sample_rate)).astype(np.float32)
    noise *= 0.5 / np.max(np.abs(noise))
    noise_path = OUT_DIR / "_white_noise.wav"
    sf.write(str(noise_path), noise, sample_rate, subtype="FLOAT")

    eq_presets = [
        (name, params)
        for name, chain in GALLERY
        if name.startswith("eq_") and len(chain) == 1 and chain[0][0] == "eq"
        for params in [chain[0][1]]
    ]
    frequencies = np.geomspace(20.0, 20000.0, 400)
    figure, axes = plt.subplots(3, 2, figsize=(20, 13))
    axes = axes.reshape(-1)
    for index, (name, params) in enumerate(eq_presets):
        out_path = OUT_DIR / f"_noise_{name}.wav"
        render(noise_path, [("eq", dict(params))], out_path)
        wet, _ = sf.read(str(out_path), dtype="float64", always_2d=True)
        wet_mono = to_mono(wet)
        _, dry_power = welch(noise.astype(np.float64), fs=sample_rate, nperseg=16384)
        freqs_welch, cross = csd(
            noise.astype(np.float64), wet_mono, fs=sample_rate, nperseg=16384
        )
        measured = 20.0 * np.log10(np.abs(cross) / (dry_power + 1e-20) + 1e-12)
        measured_interp = np.interp(frequencies, freqs_welch, measured)
        intended = intended_eq_curve(params, frequencies, sample_rate)
        axis = axes[index]
        axis.semilogx(frequencies, measured_interp, label="measured (noise)")
        axis.semilogx(frequencies, intended, "--", label="intended (params)")
        axis.set_xlim(20.0, 20000.0)
        axis.set_title(f"{name}: magnitude response")
        axis.set_xlabel("freq (Hz)")
        axis.set_ylabel("gain (dB)")
        axis.legend(fontsize=9)
        axis.grid(True, which="both", alpha=0.3)
        axis.ticklabel_format(axis="y", useOffset=False)
        span = float(
            np.max([np.max(measured_interp), np.max(intended)])
            - np.min([np.min(measured_interp), np.min(intended)])
        )
        if span < 1.0:
            middle = float(np.mean([measured_interp, intended]))
            axis.set_ylim(middle - 1.0, middle + 1.0)
        out_path.unlink()

    # Gain reduction over time: stepped sine through both compressor presets.
    comp_chains = [
        (name, chain) for name, chain in GALLERY
        if name in ("comp_gentle", "comp_squash")
    ]
    duration = 6.0
    count = round(duration * sample_rate)
    time = np.arange(count) / sample_rate
    envelope = np.where((time >= 1.0) & (time < 3.0) | (time >= 4.0), 0.5, 0.05)
    tone = (np.sin(2.0 * np.pi * 220.0 * time) * envelope).astype(np.float32)
    tone_path = OUT_DIR / "_stepped_sine.wav"
    sf.write(str(tone_path), tone, sample_rate, subtype="FLOAT")
    window = max(1, sample_rate // 100)  # 10 ms RMS windows

    def envelope_db(signal: np.ndarray) -> np.ndarray:
        mono = to_mono(signal)
        frames = mono[: len(mono) // window * window].reshape(-1, window)
        rms = np.sqrt(np.mean(frames**2, axis=1)) + 1e-12
        return 20.0 * np.log10(rms)

    in_env = envelope_db(tone)
    centers = (np.arange(len(in_env)) + 0.5) * window / sample_rate
    for slot, (name, chain) in enumerate(comp_chains):
        out_path = OUT_DIR / f"_stepped_{name}.wav"
        render(tone_path, chain, out_path)
        wet, _ = sf.read(str(out_path), dtype="float64", always_2d=True)
        out_env = envelope_db(wet)
        makeup = float(chain[0][1].get("makeup_gain_db", 0.0))
        reduction = (out_env - makeup) - in_env
        audible = in_env > -60.0
        axis = axes[4 + slot]
        axis.plot(centers, in_env, label="input level")
        axis.plot(centers, out_env, label="output level")
        axis.plot(centers[audible], reduction[audible], label="est. gain reduction")
        axis.set_title(f"{name}: level and gain reduction over time")
        axis.set_xlabel("time (s)")
        axis.set_ylabel("dBFS / dB")
        axis.legend(fontsize=9)
        axis.grid(True, alpha=0.3)
        out_path.unlink()
    noise_path.unlink()
    tone_path.unlink()

    figure.suptitle("EQ responses (white noise, Welch) and compressor behaviour", fontsize=13)
    figure.tight_layout()
    figure.savefig(path, dpi=100)
    plt.close(figure)


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Render the T12 listening gallery.")
    parser.add_argument(
        "--skip-renders", action="store_true",
        help="Reuse existing WAVs in /tmp/sfx/renders; only rebuild report and figures.",
    )
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    ensure_tool()
    primary, alternates = pick_sources()
    info = sf.info(str(primary))
    sample_rate = info.samplerate
    print(f"source: {primary} ({sample_rate} Hz, {info.channels} ch)")

    by_name = dict(GALLERY)
    if args.skip_renders:
        missing = [
            preset for preset, _ in GALLERY
            if not (OUT_DIR / f"{preset}.wav").is_file()
        ]
        if missing:
            raise SystemExit(f"--skip-renders with missing WAVs: {missing}")
        print("reusing existing renders")
    else:
        jobs: list[tuple[str, Path, list[tuple[str, dict[str, float]]], int]] = []
        for preset, chain in GALLERY:
            jobs.append((preset, OUT_DIR / f"{preset}.wav", chain, 512))
        for alternate in alternates:
            for preset, chain in GALLERY:
                jobs.append((preset, OUT_DIR / f"alt_{preset}.wav", chain, 512))

        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            futures = {
                pool.submit(render, primary if not dest.name.startswith("alt_") else alternates[0],
                            chain, dest, block): (preset, dest)
                for preset, dest, chain, block in jobs
            }
            for future in concurrent.futures.as_completed(futures):
                preset, dest = futures[future]
                stdout = future.result()
                print(f"[{preset} -> {dest.name}] {stdout.strip()}")

    input_audio, _ = sf.read(str(primary), dtype="float32", always_2d=True)
    input_mono = to_mono(input_audio)

    # Dry must be transparent: bit-identical to float-converted input.
    dry_audio, _ = sf.read(str(OUT_DIR / "dry.wav"), dtype="float32", always_2d=True)
    if not np.array_equal(input_audio, dry_audio):
        max_diff = float(np.max(np.abs(input_audio - dry_audio)))
        raise SystemExit(f"FAIL: dry render is not bit-identical (max diff {max_diff:g})")
    print("dry render is bit-identical to the input")

    rows: list[tuple[str, str, dict[str, object]]] = []
    for preset, chain in GALLERY:
        output, rate = sf.read(str(OUT_DIR / f"{preset}.wav"), dtype="float32", always_2d=True)
        assert rate == sample_rate
        rows.append((preset, chain_string(chain), analyse(input_mono, output, rate)))

    block_rows: list[tuple[str, int, float, bool]] = []
    block_failed = False
    for preset in BLOCK_CHECK_PRESETS:
        reference, _ = sf.read(str(OUT_DIR / f"{preset}.wav"), dtype="float32", always_2d=True)
        for block in BLOCK_SIZES:
            candidate_path = OUT_DIR / f"_blockcheck_{preset}_{block}.wav"
            render(primary, by_name[preset], candidate_path, block)
            candidate, _ = sf.read(str(candidate_path), dtype="float32", always_2d=True)
            diff = float(np.max(np.abs(reference - candidate)))
            passed = diff <= BLOCK_TOLERANCE
            block_failed = block_failed or not passed
            block_rows.append((preset, block, diff, passed))
            print(f"block check {preset} block={block}: max diff {diff:.3g} "
                  f"{'PASS' if passed else 'FAIL'}")
            candidate_path.unlink()

    write_report(OUT_DIR / "report.md", primary, sample_rate, rows, block_rows, alternates)

    render_list = [(preset, OUT_DIR / f"{preset}.wav") for preset, _ in GALLERY]
    write_spectrograms(OUT_DIR / "spectrograms.png", primary, render_list)
    write_responses(OUT_DIR / "responses.png", sample_rate)

    print("\nflagged presets:")
    any_flagged = False
    for preset, _, metrics in rows:
        if metrics["flags"]:
            any_flagged = True
            print(f"  {preset}: {','.join(metrics['flags'])}")  # type: ignore[union-attr]
    if not any_flagged:
        print("  none")
    if block_failed:
        raise SystemExit("FAIL: block-size independence breach (see report.md)")
    print(f"\ndone: {len(rows)} renders + report.md + spectrograms.png + responses.png")


if __name__ == "__main__":
    sys.exit(main())
