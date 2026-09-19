"""Dasp-based dynamic range compressor renderer."""

from typing import Any, Mapping

import torch


def render(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    """Render physical compressor parameters through dasp's implementation."""
    import dasp_pytorch

    release_ms = torch.full_like(params["attack_ms"], 50.0)
    return dasp_pytorch.functional.compressor(
        audio,
        sample_rate,
        release_ms=release_ms,
        **params,
    )
