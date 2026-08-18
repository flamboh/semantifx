"""SemantiFX-owned differentiable distortion DSP."""

import dasp_pytorch
import torch


TONE_FREQUENCY_HZ = 3000.0
TONE_Q = 0.7071


def _view(parameter: torch.Tensor) -> torch.Tensor:
    return parameter.reshape(-1, 1, 1)


def _tone_filter(audio: torch.Tensor, sample_rate: float, tone_db: torch.Tensor) -> torch.Tensor:
    gain = _view(tone_db)
    frequency = torch.full_like(gain, TONE_FREQUENCY_HZ)
    q = torch.full_like(gain, TONE_Q)
    b, a = dasp_pytorch.signal.biquad(gain, frequency, q, sample_rate, "high_shelf")
    sos = torch.cat((b, a), dim=-1).reshape(audio.shape[0], 1, 6)
    return dasp_pytorch.signal.sosfilt_via_fsm(sos, audio)


def process(
    audio: torch.Tensor,
    sample_rate: float,
    drive_db: torch.Tensor,
    character: torch.Tensor,
    tone_db: torch.Tensor,
    mix: torch.Tensor,
) -> torch.Tensor:
    """Apply a bounded, continuously variable distortion to ``audio``.

    Character crossfades from normalized arctangent saturation to normalized
    hyperbolic-tangent saturation. Both curves meet the input at +/-1, which
    prevents drive from becoming an implicit peak-gain control.
    """
    drive = _view(drive_db)
    curve = _view(character)
    wet_mix = _view(mix)
    amount = 0.04 * torch.pow(torch.full_like(drive, 10.0), drive / 20.0)
    soft = torch.atan(amount * audio) / torch.atan(amount)
    hard = torch.tanh(amount * audio) / torch.tanh(amount)
    shaped = soft + curve * (hard - soft)
    toned = _tone_filter(shaped, sample_rate, tone_db)
    return audio + wet_mix * (toned - audio)
