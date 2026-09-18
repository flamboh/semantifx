"""Parametric EQ renderer."""

from typing import Any, Mapping


def render(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    """Render physical EQ parameters through dasp's functional implementation."""
    import dasp_pytorch

    return dasp_pytorch.functional.parametric_eq(audio, sample_rate, **params)
