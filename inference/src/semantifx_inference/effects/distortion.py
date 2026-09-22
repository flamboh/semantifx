"""Distortion renderer."""

from typing import Any, Mapping


def render(audio: Any, sample_rate: float, params: Mapping[str, Any], *, exact=None) -> Any:
    """Render physical distortion parameters through the SemantiFX DSP.

    Pass ``exact=True`` (or set ``SEMANTIFX_EQ_EXACT=1``) to apply the tone
    filter with a true recursive filter instead of dasp's FSM approximation;
    the default path is unchanged.
    """
    from ..distortion import process

    return process(audio, sample_rate, exact=exact, **params)
