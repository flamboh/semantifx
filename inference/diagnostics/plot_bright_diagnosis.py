#!/usr/bin/env python3
"""Plot spectrograms and transfer curves for the bright-guitar diagnosis."""

import argparse
from pathlib import Path

import librosa
import librosa.display
import matplotlib.pyplot as plt
import numpy as np

from compare_eq_response import estimate_response, read_mono


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference_dry", type=Path)
    parser.add_argument("reference_wet", type=Path)
    parser.add_argument("broken_dry", type=Path)
    parser.add_argument("broken_wet", type=Path)
    parser.add_argument("fixed_dry", type=Path)
    parser.add_argument("fixed_wet", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    renders = [
        ("Published checkpoint", args.reference_wet),
        ("Broken: 2 s, unseeded", args.broken_wet),
        ("Fixed: 10 s, seed 0", args.fixed_wet),
    ]
    figure = plt.figure(figsize=(14, 8), constrained_layout=True)
    grid = figure.add_gridspec(2, 3, height_ratios=(1.15, 1.0))

    for index, (title, path) in enumerate(renders):
        audio, sample_rate = read_mono(path)
        audio = audio[: 2 * sample_rate]
        mel = librosa.feature.melspectrogram(
            y=audio, sr=sample_rate, n_fft=2048, hop_length=256, n_mels=128
        )
        mel_db = librosa.power_to_db(mel, ref=np.max)
        axis = figure.add_subplot(grid[0, index])
        librosa.display.specshow(
            mel_db,
            sr=sample_rate,
            hop_length=256,
            x_axis="time",
            y_axis="mel",
            vmin=-70,
            vmax=0,
            cmap="magma",
            ax=axis,
        )
        axis.set_title(title)
        axis.set_xlabel("Time (s)")
        if index:
            axis.set_ylabel("")

    response_axis = figure.add_subplot(grid[1, :])
    response_pairs = [
        ("Published checkpoint", args.reference_dry, args.reference_wet, "#6f42c1"),
        ("Broken: 2 s, unseeded", args.broken_dry, args.broken_wet, "#d73a49"),
        ("Fixed: 10 s, seed 0", args.fixed_dry, args.fixed_wet, "#22863a"),
    ]
    for label, dry, wet, colour in response_pairs:
        frequencies, response = estimate_response(dry, wet)
        response_axis.semilogx(frequencies, response, label=label, color=colour, linewidth=2)
    response_axis.set_xlim(50, 20000)
    response_axis.set_ylim(-35, 35)
    response_axis.axhline(0, color="#777", linewidth=0.8)
    response_axis.grid(True, which="both", alpha=0.2)
    response_axis.set_title("Estimated EQ transfer response")
    response_axis.set_xlabel("Frequency (Hz)")
    response_axis.set_ylabel("Gain (dB)")
    response_axis.legend(loc="lower left")

    figure.suptitle("SemantiFX 'bright' guitar: published vs. broken vs. fixed", fontsize=16)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, dpi=180)
    print(args.output)


if __name__ == "__main__":
    main()
