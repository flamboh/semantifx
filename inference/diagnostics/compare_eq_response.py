#!/usr/bin/env python3
"""Compare two rendered EQ responses against their respective dry inputs.

Run this with the Text2FX virtual environment, which supplies NumPy, SciPy,
and SoundFile. Inputs may have different durations; the comparison is between
their estimated transfer functions rather than their waveforms.
"""

import argparse
from pathlib import Path

import numpy as np
from scipy.signal import csd, welch
import soundfile as sf


def read_mono(path: Path):
    audio, sample_rate = sf.read(path)
    if audio.ndim == 2:
        audio = audio.mean(axis=1)
    return np.asarray(audio, dtype=np.float64), sample_rate


def estimate_response(dry_path: Path, wet_path: Path):
    dry, dry_rate = read_mono(dry_path)
    wet, wet_rate = read_mono(wet_path)
    if dry_rate != wet_rate:
        raise ValueError(f"sample-rate mismatch: {dry_rate} != {wet_rate}")
    if dry.shape != wet.shape:
        raise ValueError(f"sample-count mismatch: {dry.shape} != {wet.shape}")

    frequencies, dry_power = welch(
        dry, fs=dry_rate, nperseg=16384, noverlap=12288
    )
    _, cross_power = csd(
        dry, wet, fs=dry_rate, nperseg=16384, noverlap=12288
    )
    response_db = 20.0 * np.log10(np.abs(cross_power / (dry_power + 1e-20)) + 1e-12)
    return frequencies, response_db


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference_dry", type=Path)
    parser.add_argument("reference_wet", type=Path)
    parser.add_argument("candidate_dry", type=Path)
    parser.add_argument("candidate_wet", type=Path)
    parser.add_argument("--max-rmse-db", type=float, default=6.0)
    parser.add_argument(
        "--align-gain",
        action="store_true",
        help="Ignore a constant output-gain difference when deciding pass/fail.",
    )
    args = parser.parse_args()

    ref_frequencies, ref_response = estimate_response(
        args.reference_dry, args.reference_wet
    )
    candidate_frequencies, candidate_response = estimate_response(
        args.candidate_dry, args.candidate_wet
    )

    comparison_frequencies = np.geomspace(50.0, 20000.0, 1000)
    ref = np.interp(comparison_frequencies, ref_frequencies, ref_response)
    candidate = np.interp(
        comparison_frequencies, candidate_frequencies, candidate_response
    )
    rmse_db = float(np.sqrt(np.mean(np.square(ref - candidate))))
    max_error_db = float(np.max(np.abs(ref - candidate)))
    gain_offset_db = float(np.median(candidate - ref))
    aligned_error = candidate - gain_offset_db - ref
    aligned_rmse_db = float(np.sqrt(np.mean(np.square(aligned_error))))
    print(f"response_rmse_db={rmse_db:.3f}")
    print(f"response_max_error_db={max_error_db:.3f}")
    print(f"gain_offset_db={gain_offset_db:.3f}")
    print(f"gain_aligned_response_rmse_db={aligned_rmse_db:.3f}")
    verdict_rmse_db = aligned_rmse_db if args.align_gain else rmse_db
    if verdict_rmse_db > args.max_rmse_db:
        raise SystemExit(
            f"FAIL: response RMSE {verdict_rmse_db:.3f} dB exceeds "
            f"{args.max_rmse_db:.3f} dB"
        )
    print("PASS")


if __name__ == "__main__":
    main()
