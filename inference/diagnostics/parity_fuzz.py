"""Fuzzed parity between the C++ DSP and the Python reference.

For each effect type in the catalogue this script draws N random parameter
sets (default 40, fixed seed; log-uniform for parameters whose
``protocol/effects/*.toml`` entry says ``scale = "log"``, uniform otherwise),
plus the all-minimum and all-maximum corners, and renders the Python reference
on two inputs: the synthetic signal from ``render_parity_fixtures.py`` and a
2 s real-guitar excerpt (``inference/benchmarks/audio/guitar.wav`` when
present, otherwise the committed ``web/public/original-guitar.wav`` demo
capture, resampled to 44100 Hz and averaged to mono when needed).

Cases are written to a temp directory in the ``case.txt`` + ``input.f32`` +
``expected.f32`` layout that ``semantifx_parity_tests`` reads, then the
existing binary is built (if stale) and invoked with ``--stats
--write-actual-on-fail``. Per case the script reports max abs error, RMS error
in dB relative to the expected-signal RMS, and pass/fail against the tolerance
in ``plugin/tests/ParityTests.cpp``. Failing cases get an error
characterization (gain fit, DC, latency estimate, transient/tail energy split).

The reverb renderer (``effects/reverb.py``) only supports 44100 Hz, while the
C++ ``ReverbEffect`` resamples its frozen-noise impulse response to the
session rate (``plugin/src/dsp/ReverbEffect.cpp`` ``rebuildImpulseResponse``).
A second reverb batch at 48 kHz therefore builds its expected output with the
documented equivalent procedure: build the 44100 Hz IR with the real renderer
helpers, linear-interpolate it to the 48 kHz tap count, apply the same
energy normalization, then FFT-convolve at 48 kHz. That batch checks the C++
resampling math, not the renderer itself; the 44100 Hz batch checks the
renderer.

This script never changes the C++ effects or the Python renderers; it only
reads them through the catalogue.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

import numpy as np
import torch

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "inference" / "src"))
sys.path.insert(0, str(REPO / "inference" / "diagnostics"))

from semantifx_inference import catalogue  # noqa: E402

import render_parity_fixtures as fixtures  # noqa: E402

SEED = 20260922
BASE_SAMPLE_RATE = 44_100
REVERB48_SAMPLE_RATE = 48_000
GUITAR_SECONDS = 2.0
SHORT_SECONDS = 0.5

EFFECT_ORDER = ("eq", "distortion", "compressor", "reverb")


def log_scaled_names() -> dict[str, set[str]]:
    """Parameter names marked ``scale = "log"`` in protocol/effects/*.toml."""
    result: dict[str, set[str]] = {}
    for toml_path in (REPO / "protocol" / "effects").glob("*.toml"):
        with toml_path.open("rb") as stream:
            data = tomllib.load(stream)
        names = {
            entry["name"]
            for entry in data.get("parameters", [])
            if entry.get("scale") == "log"
        }
        result[data["effect_type"]] = names
    return result


def draw_parameter_sets(
    effect_type: str, count: int, rng: np.random.Generator
) -> list[tuple[str, dict[str, float]]]:
    """Draw ``count`` random sets plus the all-min/all-max corners."""
    specs = catalogue.CATALOGUE[effect_type].specs
    log_names = log_scaled_names().get(effect_type, set())
    sets: list[tuple[str, dict[str, float]]] = []
    for _ in range(count):
        values: dict[str, float] = {}
        for spec in specs:
            if spec.name in log_names:
                if not spec.minimum > 0.0:
                    raise ValueError(
                        "log-uniform sampling needs a positive minimum for %s"
                        % spec.name
                    )
                low, high = np.log(spec.minimum), np.log(spec.maximum)
                values[spec.name] = float(np.exp(rng.uniform(low, high)))
            else:
                values[spec.name] = float(rng.uniform(spec.minimum, spec.maximum))
        sets.append(("rand", values))
    sets.append(("min", {spec.name: spec.minimum for spec in specs}))
    sets.append(("max", {spec.name: spec.maximum for spec in specs}))
    return sets


def load_guitar_excerpt(sample_rate: int) -> tuple[np.ndarray, str]:
    """Load a mono 2 s guitar excerpt, resampling only when needed."""
    import soundfile as sf

    preferred = REPO / "inference" / "benchmarks" / "audio" / "guitar.wav"
    fallback = REPO / "web" / "public" / "original-guitar.wav"
    path = preferred if preferred.exists() else fallback
    audio, file_rate = sf.read(str(path), dtype="float32", always_2d=True)
    mono = audio.mean(axis=1).astype(np.float32)
    if file_rate != sample_rate:
        import torchaudio

        tensor = torch.from_numpy(mono).unsqueeze(0)
        resampled = torchaudio.functional.resample(
            tensor, orig_freq=file_rate, new_freq=sample_rate
        )
        mono = resampled.squeeze(0).numpy().astype(np.float32)
    window = int(GUITAR_SECONDS * sample_rate)
    if mono.size <= window:
        excerpt = mono
    else:
        # Loudest 2 s window on a 0.1 s grid: guitar captures often start quiet.
        stride = sample_rate // 10
        energy = np.add.reduceat(
            mono[: mono.size // stride * stride] ** 2, np.arange(0, mono.size // stride * stride, stride)
        )
        frames_per_window = window // stride
        if energy.size <= frames_per_window:
            start_frame = 0
        else:
            totals = np.convolve(energy, np.ones(frames_per_window), mode="valid")
            start_frame = int(np.argmax(totals))
        excerpt = mono[start_frame * stride : start_frame * stride + window]
    return excerpt, str(path.relative_to(REPO))


def synth_input(sample_rate: int) -> np.ndarray:
    """Mono synthetic signal matching render_parity_fixtures.make_input."""
    tensor = fixtures.make_input(sample_rate)
    return tensor.numpy().reshape(-1).astype(np.float32)


def render_reference(
    effect_type: str, audio: np.ndarray, sample_rate: int, values: dict[str, float]
) -> np.ndarray:
    """Render one mono case through the catalogue renderer."""
    tensor = torch.from_numpy(audio).reshape(1, 1, -1)
    params = {
        name: torch.tensor([value], dtype=torch.float32)
        for name, value in values.items()
    }
    with torch.no_grad():
        rendered = catalogue.CATALOGUE[effect_type].render(tensor, sample_rate, params)
    return rendered.detach().cpu().numpy().reshape(-1).astype(np.float32)


def render_reverb_48k(
    audio: np.ndarray, values: dict[str, float]
) -> np.ndarray:
    """Expected 48 kHz reverb output via the C++-documented IR resampling.

    Mirrors ``ReverbEffect::rebuildImpulseResponse`` in
    ``plugin/src/dsp/ReverbEffect.cpp``: the frozen 44100 Hz IR built by
    ``effects/reverb.py`` is linear-interpolated to
    ``round(65536 * sr / 44100)`` taps with endpoint-pinned positions, then
    scaled to preserve time-integrated energy, then convolved at 48 kHz.
    """
    from semantifx_inference.effects import reverb as rev

    sample_rate = float(REVERB48_SAMPLE_RATE)
    base_count = rev.NUM_SAMPLES
    target_count = max(1, round(base_count * sample_rate / rev.SAMPLE_RATE))
    decay = torch.tensor([values["decay"]], dtype=torch.float32)
    damping = torch.tensor([values["damping"]], dtype=torch.float32)
    mix = float(values["mix"])
    band_gains, band_decays = rev.expand_macros(decay, damping)
    base = rev.build_impulse_response(
        band_gains, band_decays, rev.load_noise_table().to(dtype=torch.float32)
    )[0]  # drop the batch dim -> (2, 65536)

    channels = []
    for channel in range(rev.NUM_CHANNELS):
        source = base[channel]
        if target_count == base_count:
            resampled = source
        else:
            # Positions in float64: the C++ side maps taps in double
            # (ReverbEffect.cpp rebuildImpulseResponse) and float32
            # rounding here flips floor() at near-integer positions.
            positions = (
                torch.arange(target_count, dtype=torch.float64)
                * (base_count - 1)
                / (target_count - 1)
            )
            first = positions.floor().long().clamp(0, base_count - 1)
            second = (first + 1).clamp(0, base_count - 1)
            fraction = (positions - first.double()).to(dtype=torch.float32)
            src32 = source
            resampled = src32[first] + fraction * (src32[second] - src32[first])
            original_energy = double_energy(source)
            resampled_energy = double_energy(resampled)
            target_energy = original_energy * rev.SAMPLE_RATE / sample_rate
            if resampled_energy > 0.0 and target_energy > 0.0:
                resampled = resampled * float(np.sqrt(target_energy / resampled_energy))
        channels.append(resampled)
    impulse = torch.stack(channels).unsqueeze(0)  # (1, 2, L)
    tensor = torch.from_numpy(audio).reshape(1, 1, -1)
    impulse = impulse[:, : tensor.shape[1], :]
    with torch.no_grad():
        wet = rev.fft_convolve(tensor, impulse)
    out = (1.0 - mix) * tensor + mix * wet
    return out.detach().cpu().numpy().reshape(-1).astype(np.float32)


def double_energy(values: torch.Tensor) -> float:
    as64 = values.detach().cpu().to(dtype=torch.float64)
    return float((as64 * as64).sum().item())


def write_case(
    root: Path, effect_type: str, name: str, sample_rate: int, values: dict[str, float],
    audio: np.ndarray, expected: np.ndarray,
) -> None:
    directory = root / effect_type / name
    directory.mkdir(parents=True, exist_ok=True)
    directory.joinpath("input.f32").write_bytes(audio.astype("<f4", copy=False).tobytes())
    directory.joinpath("expected.f32").write_bytes(
        expected.astype("<f4", copy=False).tobytes()
    )
    lines = [
        f"sample_rate {sample_rate}",
        "channels 1",
        f"samples {audio.size}",
    ]
    specs = catalogue.CATALOGUE[effect_type].specs
    lines.extend(
        f"param {spec.name} {values[spec.name]:.17g}" for spec in specs
    )
    directory.joinpath("case.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def ensure_binary(repo: Path, plugin_dir: Path, build_dir: Path, binary: Path) -> Path:
    """Configure and build semantifx_parity_tests when it is missing or stale."""
    sources = [plugin_dir / "tests" / "ParityTests.cpp", *sorted(plugin_dir.glob("src/dsp/*"))]
    if binary.exists() and all(
        binary.stat().st_mtime >= source.stat().st_mtime for source in sources
    ):
        return binary
    print("building semantifx_parity_tests ...", flush=True)
    subprocess.run(
        [
            "cmake", "-S", str(plugin_dir), "-B", str(build_dir),
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DSEMANTIFX_DSP_ONLY=ON",
        ],
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(build_dir), "-j", "4",
         "--target", "semantifx_parity_tests"],
        check=True,
    )
    return binary


def run_binary(binary: Path, fixture_root: Path, noise_path: Path) -> list[dict]:
    proc = subprocess.run(
        [str(binary), str(fixture_root), str(noise_path),
         "--stats", "--write-actual-on-fail"],
        check=False, text=True, capture_output=True,
    )
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    results = []
    for line in proc.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) == 0:
            continue
        if parts[0].startswith("block-size"):
            continue
        if len(parts) != 8 or parts[7] not in ("PASS", "FAIL"):
            raise RuntimeError("unparseable parity output line: %r" % line)
        effect, name, max_abs, rms_abs, rms_db, exp_rms, tol, verdict = parts
        results.append(
            {
                "effect": effect, "name": name,
                "max_abs": float(max_abs),
                "rms_abs": float(rms_abs),
                "rms_db": float(rms_db),
                "expected_rms": float(exp_rms),
                "tolerance": float(tol),
                "passed": verdict == "PASS",
            }
        )
    if proc.returncode not in (0, 1):
        raise RuntimeError("parity binary exited with code %d" % proc.returncode)
    return results


def characterize(
    fixture_root: Path, effect: str, name: str, result: dict
) -> list[str]:
    """Describe the error signal of one failing case."""
    from scipy.signal import fftconvolve

    directory = fixture_root / effect / name
    expected = np.fromfile(directory / "expected.f32", dtype="<f4").astype(np.float64)
    actual = np.fromfile(directory / "actual.f32", dtype="<f4").astype(np.float64)
    error = actual - expected
    lines = []
    peak_index = int(np.argmax(np.abs(error)))
    lines.append(
        "  max abs %.3e at sample %d (expected %.3e, actual %.3e)"
        % (result["max_abs"], peak_index, expected[peak_index], actual[peak_index])
    )
    denom = float(np.dot(expected, expected))
    if denom > 0.0:
        gain = float(np.dot(actual, expected) / denom)
        residual = actual - gain * expected
        lines.append(
            "  best-fit gain %.6f (%+.2f dB), residual max abs after fit %.3e"
            % (gain, 20.0 * np.log10(abs(gain)) if gain != 0.0 else float("nan"),
               float(np.max(np.abs(residual))))
        )
    else:
        lines.append("  expected signal is silent; gain fit undefined")
    lines.append(
        "  DC: mean(error) %+.3e, mean(expected) %+.3e, mean(actual) %+.3e"
        % (float(np.mean(error)), float(np.mean(expected)), float(np.mean(actual)))
    )
    span = min(128, expected.size - 1)
    if span > 0:
        corr = fftconvolve(actual, expected[::-1], mode="full")
        center = expected.size - 1
        window = corr[center - span : center + span + 1]
        lag = int(np.argmax(window)) - span
        lines.append("  cross-correlation peak lag %+d samples (search ±%d)" % (lag, span))
    head = error[: max(1, error.size // 20)]
    head_share = float(np.dot(head, head) / max(np.dot(error, error), 1e-30))
    lines.append("  first-5%% error-energy share %.3f (transient-only if near 1)" % head_share)
    if effect == "reverb":
        half = error[error.size // 2 :]
        tail_share = float(np.dot(half, half) / max(np.dot(error, error), 1e-30))
        lines.append("  last-half error-energy share %.3f (tail issue if near 1)" % tail_share)
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Fuzzed C++/Python DSP parity.")
    parser.add_argument("--cases", type=int, default=40)
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--reverb48-sets", type=int, default=8,
                        help="reverb parameter sets rendered at 48 kHz")
    parser.add_argument("--keep-dir", type=str, default=None)
    parser.add_argument("--binary", type=str, default=None)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--report", type=str, default=None)
    args = parser.parse_args()

    torch.set_num_threads(1)
    rng = np.random.default_rng(args.seed)

    guitar, guitar_source = load_guitar_excerpt(BASE_SAMPLE_RATE)
    print("guitar source: %s (%d samples @ %d Hz)" % (guitar_source, guitar.size, BASE_SAMPLE_RATE))
    synth = synth_input(BASE_SAMPLE_RATE)
    inputs = [("synth", synth), ("guitar", guitar)]

    if args.keep_dir is None:
        temp = tempfile.TemporaryDirectory(prefix="parity-fuzz-")
        fixture_root = Path(temp.name)
    else:
        fixture_root = Path(args.keep_dir)
        fixture_root.mkdir(parents=True, exist_ok=True)
        temp = None

    total_cases = 0
    reverb_sets: list[dict[str, float]] = []
    for effect_type in EFFECT_ORDER:
        sets = draw_parameter_sets(effect_type, args.cases, rng)
        if effect_type == "reverb":
            reverb_sets = [values for _, values in sets[: args.reverb48_sets]]
        for index, (kind, values) in enumerate(sets):
            tag = kind if kind in ("min", "max") else f"{index:02d}"
            for input_name, audio in inputs:
                name = f"fuzz_{input_name}_{tag}"
                expected = render_reference(effect_type, audio, BASE_SAMPLE_RATE, values)
                write_case(fixture_root, effect_type, name, BASE_SAMPLE_RATE,
                           values, audio, expected)
                total_cases += 1

    # Reverb at 48 kHz with the noise table (see module docstring).
    import torchaudio

    synth48 = synth_input(REVERB48_SAMPLE_RATE)
    guitar48_full = torchaudio.functional.resample(
        torch.from_numpy(guitar).unsqueeze(0), orig_freq=BASE_SAMPLE_RATE,
        new_freq=REVERB48_SAMPLE_RATE,
    ).squeeze(0).numpy().astype(np.float32)
    short = int(SHORT_SECONDS * REVERB48_SAMPLE_RATE)
    inputs48 = [("synth", synth48), ("guitar", guitar48_full[:short])]
    for index, values in enumerate(reverb_sets):
        for input_name, audio in inputs48:
            name = f"fuzz48_{input_name}_{index:02d}"
            expected = render_reverb_48k(audio, values)
            write_case(fixture_root, "reverb", name, REVERB48_SAMPLE_RATE,
                       values, audio, expected)
            total_cases += 1

    print("rendered %d fuzz cases under %s" % (total_cases, fixture_root))
    plugin_dir = REPO / "plugin"
    build_dir = plugin_dir / "build-dsp"
    binary = Path(args.binary) if args.binary else build_dir / "semantifx_parity_tests"
    if not args.no_build:
        ensure_binary(REPO, plugin_dir, build_dir, binary)
    if not binary.exists():
        raise RuntimeError("parity binary not found: %s" % binary)
    noise_path = REPO / "protocol" / "reverb" / "noise_44100.f32"

    results = run_binary(binary, fixture_root, noise_path)

    report_lines = []
    failures = 0
    for effect_type in EFFECT_ORDER:
        effect_results = [r for r in results if r["effect"] == effect_type]
        passed = sum(1 for r in effect_results if r["passed"])
        report_lines.append(
            "%s: %d/%d passed" % (effect_type, passed, len(effect_results))
        )
        for result in sorted(effect_results, key=lambda r: -r["max_abs"])[:5]:
            report_lines.append(
                "  worst %s/%s maxAbs %.3e rmsErr %+.1f dB %s"
                % (result["effect"], result["name"], result["max_abs"],
                   result["rms_db"], "PASS" if result["passed"] else "FAIL")
            )
    print("\n".join(report_lines))

    for result in results:
        if result["passed"]:
            continue
        failures += 1
        print("FAIL %s/%s maxAbs %.3e rmsErr %+.1f dB (tol %.1e)" % (
            result["effect"], result["name"], result["max_abs"],
            result["rms_db"], result["tolerance"]))
        case_file = fixture_root / result["effect"] / result["name"] / "case.txt"
        print("  params: " + " | ".join(
            line.strip() for line in case_file.read_text().splitlines()
            if line.startswith("param")))
        for line in characterize(fixture_root, result["effect"], result["name"], result):
            print(line)

    summary = "\n".join(report_lines)
    if args.report is not None:
        Path(args.report).write_text(summary + "\n", encoding="utf-8")
    if temp is not None and failures == 0 and args.keep_dir is None:
        temp.cleanup()
    elif temp is not None:
        print("fuzz fixtures kept at %s" % fixture_root)
    print("fuzz result: %d/%d passed" % (len(results) - failures, len(results)))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
