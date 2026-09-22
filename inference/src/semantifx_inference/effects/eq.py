"""Parametric EQ renderer."""

from typing import Any, Mapping

import torch

# (gain, frequency, Q parameter names, dasp filter type) in the order dasp's
# parametric_eq applies them: low shelf, four peaking bands, high shelf.
_SECTIONS = (
    ("low_shelf_gain_db", "low_shelf_cutoff_freq", "low_shelf_q_factor", "low_shelf"),
    ("band0_gain_db", "band0_cutoff_freq", "band0_q_factor", "peaking"),
    ("band1_gain_db", "band1_cutoff_freq", "band1_q_factor", "peaking"),
    ("band2_gain_db", "band2_cutoff_freq", "band2_q_factor", "peaking"),
    ("band3_gain_db", "band3_cutoff_freq", "band3_q_factor", "peaking"),
    ("high_shelf_gain_db", "high_shelf_cutoff_freq", "high_shelf_q_factor", "high_shelf"),
)


def _build_sos(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    """Rebuild dasp parametric_eq's SOS with identical coefficient math.

    The formulas are dasp's; they run in float64 to mirror the plugin, which
    evaluates the same RBJ expressions in double from float parameters.
    """
    import dasp_pytorch

    sos = torch.zeros(
        audio.size(0), len(_SECTIONS), 6, dtype=torch.float64, device=audio.device
    )
    for index, (gain_key, freq_key, q_key, kind) in enumerate(_SECTIONS):
        b, a = dasp_pytorch.signal.biquad(
            params[gain_key].reshape(-1, 1, 1).to(torch.float64),
            params[freq_key].reshape(-1, 1, 1).to(torch.float64),
            params[q_key].reshape(-1, 1, 1).to(torch.float64),
            sample_rate,
            kind,
        )
        sos[:, index, :] = torch.cat((b, a), dim=-1)
    return sos


def render(audio: Any, sample_rate: float, params: Mapping[str, Any], *, exact=None) -> Any:
    """Render physical EQ parameters through dasp's functional implementation.

    Pass ``exact=True`` (or set ``SEMANTIFX_EQ_EXACT=1``) to apply the same
    RBJ coefficients with a true recursive filter instead of dasp's FSM
    approximation; the default path is unchanged.
    """
    import dasp_pytorch

    from ..sos_exact import exact_enabled, sosfilt_exact

    if exact_enabled(exact):
        return sosfilt_exact(_build_sos(audio, sample_rate, params), audio)
    return dasp_pytorch.functional.parametric_eq(audio, sample_rate, **params)
