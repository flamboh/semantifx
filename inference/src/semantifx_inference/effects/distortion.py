"""Distortion renderer."""

from typing import Any, Mapping


def render(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    """Render physical distortion parameters through the SemantiFX DSP."""
    from ..distortion import process

    return process(audio, sample_rate, **params)
