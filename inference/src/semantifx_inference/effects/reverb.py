"""Frozen-noise, noise-shaped reverberation.

The committed table contains two filtered-noise channels. Mono input uses IR
channel 0 and stays mono. Stereo input uses IR channels 0 and 1. This differs
from dasp's renderer, which duplicates mono input to stereo.
"""

from pathlib import Path
from typing import Mapping

import numpy as np
import torch


SAMPLE_RATE = 44_100
NUM_BANDS = 12
NUM_SAMPLES = 65_536
NUM_CHANNELS = 2

_NOISE_PATH = Path(__file__).resolve().parents[4] / "protocol" / "reverb" / "noise_44100.f32"
_NOISE_CPU: torch.Tensor | None = None
_NOISE_DEVICE_CACHE: dict[tuple[str, torch.dtype], torch.Tensor] = {}


def load_noise_table() -> torch.Tensor:
    """Load the committed table once on CPU, with shape ``(2, 12, 65536)``."""
    global _NOISE_CPU
    if _NOISE_CPU is None:
        values = np.fromfile(_NOISE_PATH, dtype="<f4")
        expected = NUM_CHANNELS * NUM_BANDS * NUM_SAMPLES
        if values.size != expected:
            raise RuntimeError(
                "Frozen reverb noise table has %d values; expected %d." % (values.size, expected)
            )
        _NOISE_CPU = torch.from_numpy(values.reshape(NUM_CHANNELS, NUM_BANDS, NUM_SAMPLES))
    return _NOISE_CPU


def _noise_for(audio: torch.Tensor) -> torch.Tensor:
    key = (str(audio.device), audio.dtype)
    if key not in _NOISE_DEVICE_CACHE:
        _NOISE_DEVICE_CACHE[key] = load_noise_table().to(device=audio.device, dtype=audio.dtype)
    return _NOISE_DEVICE_CACHE[key]


def expand_macros(
    decay: torch.Tensor, damping: torch.Tensor
) -> tuple[torch.Tensor, torch.Tensor]:
    """Expand batch-shaped macro tensors into twelve band gains and decays."""
    if decay.ndim != 1 or damping.ndim != 1 or decay.shape != damping.shape:
        raise ValueError("decay and damping must have the same shape (batch,)")
    positions = torch.arange(
        NUM_BANDS, device=decay.device, dtype=decay.dtype
    ) / (NUM_BANDS - 1)
    band_decays = (1.0 - decay[:, None] + damping[:, None] * positions).clamp(0.0, 1.0)
    band_gains = (1.0 - damping[:, None] * positions).clamp(0.0, 1.0)
    return band_gains, band_decays


def build_impulse_response(
    band_gains: torch.Tensor,
    band_decays: torch.Tensor,
    noise: torch.Tensor,
) -> torch.Tensor:
    """Build one or more ``(channels, 65536)`` impulse responses."""
    if band_gains.shape != band_decays.shape or band_gains.ndim not in (1, 2):
        raise ValueError("band gains and decays must have shape (12,) or (batch, 12)")
    if band_gains.shape[-1] != NUM_BANDS:
        raise ValueError("reverb needs twelve band gains and decays")
    if noise.ndim != 3 or tuple(noise.shape) != (NUM_CHANNELS, NUM_BANDS, NUM_SAMPLES):
        raise ValueError("noise must have shape (2, 12, 65536)")

    batched = band_gains.ndim == 2
    gains = band_gains if batched else band_gains.unsqueeze(0)
    decays = band_decays if batched else band_decays.unsqueeze(0)
    batch_noise = noise.unsqueeze(0).expand(gains.shape[0], -1, -1, -1)
    time = torch.linspace(0.0, 1.0, steps=NUM_SAMPLES, device=noise.device, dtype=noise.dtype)
    envelope = torch.exp(
        -(10.0 * decays + 1.0)[:, None, :, None] * time[None, None, None, :]
    )
    impulse = (batch_noise * gains[:, None, :, None] * envelope).mean(dim=2)
    return impulse if batched else impulse[0]


def fft_convolve(audio: torch.Tensor, impulse: torch.Tensor) -> torch.Tensor:
    """Apply causal full convolution and return the input-length prefix."""
    if audio.ndim != 3 or impulse.ndim != 3:
        raise ValueError("audio and impulse must have shape (batch, channels, samples)")
    if audio.shape[:2] != impulse.shape[:2]:
        raise ValueError("audio and impulse batch/channel dimensions must match")
    output_samples = audio.shape[-1]
    full_samples = output_samples + impulse.shape[-1] - 1
    fft_size = 1 << (full_samples - 1).bit_length()
    spectrum = torch.fft.rfft(audio, n=fft_size) * torch.fft.rfft(impulse, n=fft_size)
    return torch.fft.irfft(spectrum, n=fft_size)[..., :output_samples]


def render(audio: torch.Tensor, sample_rate: float, params: Mapping[str, torch.Tensor]) -> torch.Tensor:
    """Render physical ``decay``, ``damping``, and ``mix`` tensors."""
    if sample_rate != SAMPLE_RATE:
        raise ValueError("Frozen reverb noise is only available at 44100 Hz")
    if audio.ndim != 3 or audio.shape[1] not in (1, 2):
        raise ValueError("reverb audio must have shape (batch, 1|2, samples)")
    decay = params["decay"]
    damping = params["damping"]
    mix = params["mix"]
    if any(value.ndim != 1 or value.shape[0] != audio.shape[0] for value in (decay, damping, mix)):
        raise ValueError("reverb parameters must have shape (batch,)")

    band_gains, band_decays = expand_macros(decay, damping)
    impulse = build_impulse_response(band_gains, band_decays, _noise_for(audio))
    impulse = impulse[:, : audio.shape[1], :]
    wet = fft_convolve(audio, impulse)
    return (1.0 - mix[:, None, None]) * audio + mix[:, None, None] * wet


__all__ = [
    "build_impulse_response",
    "expand_macros",
    "fft_convolve",
    "load_noise_table",
    "render",
]
