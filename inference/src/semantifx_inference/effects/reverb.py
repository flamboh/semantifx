"""Frozen-noise, noise-shaped reverberation.

The committed table contains two filtered-noise channels. Mono input uses IR
channel 0 and stays mono. Stereo input uses IR channels 0 and 1. This differs
from dasp's renderer, which duplicates mono input to stereo.
"""

from pathlib import Path
from typing import Mapping

import math

import numpy as np
import torch


SAMPLE_RATE = 44_100
NUM_BANDS = 12
NUM_SAMPLES = 65_536
NUM_CHANNELS = 2

# Pink-weighted spectral normalization (T16): the built IR is scaled by
# s = sqrt(sum_k W_k / sum_k W_k P_k) with K = 64 log-spaced bins in
# [20 Hz, 20 kHz], P_k the mean over channels of |DFT{h}(f_k)|^2 (plain
# unwindowed DFT at exactly those frequencies), and W_k = 1 / f_k.
# Bin definition (geometric, endpoints inclusive):
# f_k = 20 * (20000 / 20) ** (k / (K - 1)). The C++ side in
# plugin/src/dsp/ReverbMath.h uses the identical definition in float64.
NORMALIZATION_BINS = 64
NORMALIZATION_FMIN = 20.0
NORMALIZATION_FMAX = 20_000.0

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


_NORMALIZATION_CACHE: dict[str, tuple[torch.Tensor, torch.Tensor]] = {}


def _normalization_basis(device: torch.device) -> tuple[torch.Tensor, torch.Tensor]:
    """Return the cached (complex DFT basis, pink weights) for a device.

    The basis has shape ``(64, 65536)`` complex128 with entry
    ``exp(-2j * pi * f_k * n / 44100)``; weights are ``1 / f_k`` float64.
    Cached per device; constants carry no gradient.
    """
    key = str(device)
    if key not in _NORMALIZATION_CACHE:
        bins = torch.logspace(
            math.log10(NORMALIZATION_FMIN),
            math.log10(NORMALIZATION_FMAX),
            steps=NORMALIZATION_BINS,
            device=device,
            dtype=torch.float64,
        )
        samples = torch.arange(NUM_SAMPLES, device=device, dtype=torch.float64)
        basis = torch.exp(
            -2j * math.pi * torch.outer(bins / SAMPLE_RATE, samples)
        )
        weights = 1.0 / bins
        _NORMALIZATION_CACHE[key] = (basis, weights)
    return _NORMALIZATION_CACHE[key]


def pink_normalization_scale(impulse: torch.Tensor) -> torch.Tensor:
    """Pink-weighted spectral normalization scale for an impulse response.

    Accepts ``(channels, samples)`` or ``(batch, channels, samples)`` and
    returns a scalar or ``(batch,)`` scale ``s`` computed in float64 with
    vectorized torch ops, so autograd gradients flow through ``s`` into the
    IR. Mirrors ``pinkNormalizationScale`` in ``plugin/src/dsp/ReverbMath.h``.
    """
    basis, weights = _normalization_basis(impulse.device)
    flat = impulse.to(dtype=torch.float64)
    batched = flat.ndim == 3
    if not batched:
        flat = flat.unsqueeze(0)
    spectrum = torch.einsum(
        "kn,bcn->bck", basis, torch.complex(flat, torch.zeros_like(flat))
    )
    power = spectrum.abs().square().mean(dim=1)
    scales = torch.sqrt(weights.sum() / (weights * power).sum(dim=-1))
    return scales if batched else scales[0]


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

    audio = torch.nan_to_num(audio, nan=0.0, posinf=0.0, neginf=0.0)
    band_gains, band_decays = expand_macros(decay, damping)
    impulse = build_impulse_response(band_gains, band_decays, _noise_for(audio))
    scales = pink_normalization_scale(impulse)
    impulse = impulse * scales.to(dtype=impulse.dtype)[:, None, None]
    impulse = impulse[:, : audio.shape[1], :]
    wet = fft_convolve(audio, impulse)
    half_pi = math.pi / 2.0
    dry_gain = torch.cos(mix[:, None, None] * half_pi)
    wet_gain = torch.sin(mix[:, None, None] * half_pi)
    return dry_gain * audio + wet_gain * wet


__all__ = [
    "build_impulse_response",
    "expand_macros",
    "fft_convolve",
    "load_noise_table",
    "pink_normalization_scale",
    "render",
]
