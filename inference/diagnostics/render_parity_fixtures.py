"""Render deterministic Python reference audio for the JUCE-free DSP tests."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import torch


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "inference" / "src"))

from semantifx_inference import catalogue  # noqa: E402


SEED = 20260918
BASE_SAMPLE_RATE = 44_100
VARIANT_SAMPLE_RATE = 48_000
DURATION_SECONDS = 0.5


def make_input(sample_rate: int) -> torch.Tensor:
    samples = round(DURATION_SECONDS * sample_rate)
    time = torch.arange(samples, dtype=torch.float32) / sample_rate
    torch.manual_seed(SEED)
    noise = torch.randn(samples, dtype=torch.float32)
    signal = (
        0.4 * torch.sin(2.0 * torch.pi * 110.0 * time)
        + 0.3 * torch.sin(2.0 * torch.pi * 440.0 * time)
        + 0.1 * torch.sin(2.0 * torch.pi * 3000.0 * time)
        + 0.05 * noise
    )
    envelope = torch.ones(samples, dtype=torch.float32)
    envelope[round(0.25 * sample_rate) :] = 0.1
    return (signal * envelope).reshape(1, 1, samples)


def values_for(effect_type: str, case: str) -> dict[str, float]:
    specs = catalogue.CATALOGUE[effect_type].specs
    if case == "defaults":
        return {spec.name: spec.default for spec in specs}
    if case == "minimum":
        return {spec.name: spec.minimum for spec in specs}
    if case == "maximum":
        return {spec.name: spec.maximum for spec in specs}
    if case == "mid":
        return {spec.name: (spec.minimum + spec.maximum) / 2.0 for spec in specs}
    if case == "dry":
        values = {spec.name: spec.default for spec in specs}
        values["mix"] = 0.0
        return values
    if case == "wet":
        values = {spec.name: spec.default for spec in specs}
        values.update(decay=0.8, damping=0.5, mix=1.0)
        return values
    raise ValueError(f"Unknown fixture case: {case}")


def render_case(effect_type: str, case: str, sample_rate: int) -> None:
    values = values_for(effect_type, case)
    audio = make_input(sample_rate)
    params = {
        name: torch.tensor([value], dtype=torch.float32)
        for name, value in values.items()
    }
    rendered = catalogue.CATALOGUE[effect_type].render(audio, sample_rate, params)
    rendered = rendered.detach().cpu().reshape(-1).contiguous()
    output = rendered.numpy().astype("<f4", copy=False)

    suffix = "" if sample_rate == BASE_SAMPLE_RATE else "_48k"
    directory = REPO / "plugin" / "tests" / "fixtures" / "parity" / effect_type / (
        case + suffix
    )
    directory.mkdir(parents=True, exist_ok=True)
    directory.joinpath("input.f32").write_bytes(
        make_input(sample_rate).reshape(-1).numpy().astype("<f4", copy=False).tobytes()
    )
    directory.joinpath("expected.f32").write_bytes(output.tobytes())
    lines = [
        f"sample_rate {sample_rate}",
        "channels 1",
        f"samples {audio.shape[-1]}",
    ]
    lines.extend(f"param {name} {value:.17g}" for name, value in values.items())
    directory.joinpath("case.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    torch.set_num_threads(1)
    cases = {
        "eq": ("defaults", "minimum", "maximum", "mid"),
        "distortion": ("defaults", "minimum", "maximum", "mid"),
        "compressor": ("defaults", "minimum", "maximum", "mid"),
        "reverb": ("defaults", "minimum", "maximum", "mid", "dry", "wet"),
    }
    for effect_type, effect_cases in cases.items():
        for case in effect_cases:
            render_case(effect_type, case, BASE_SAMPLE_RATE)
            print(f"rendered {effect_type}/{case} at {BASE_SAMPLE_RATE} Hz")
            if effect_type != "reverb" and case in {"defaults", "mid"}:
                render_case(effect_type, case, VARIANT_SAMPLE_RATE)
                print(f"rendered {effect_type}/{case} at {VARIANT_SAMPLE_RATE} Hz")


if __name__ == "__main__":
    main()
