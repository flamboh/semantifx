"""Exact recursive application of RBJ biquad cascades.

dasp's ``sosfilt_via_fsm`` applies a biquad cascade as a truncated FIR via
circular convolution, which time-aliases for low-frequency, high-Q, high-gain
sections. The plugin instead runs exact DF-II-T biquads with double state, so
the optimizer tunes EQ parameters against an approximation of what the plugin
will play (T11 parity fuzz: 32/84 EQ cases fail, worst divergence -30.5 dB
RMS).

This module applies the *same* dasp RBJ coefficients with a true recursive
filter (``torchaudio.functional.lfilter`` per section, which is differentiable
on CPU and CUDA), so only the application changes, never the coefficient math.

Gated behind ``SEMANTIFX_EQ_EXACT=1`` (or an explicit ``exact=`` keyword at the
call sites); every renderer's default behaviour is unchanged.
"""

import os

import torch

ENV_VAR = "SEMANTIFX_EQ_EXACT"


def exact_enabled(exact=None) -> bool:
    """Resolve the exact-filter switch: an explicit keyword wins, else env."""
    if exact is not None:
        return bool(exact)
    return os.environ.get(ENV_VAR, "0") == "1"


def sosfilt_exact(sos, audio):
    """Apply an SOS cascade recursively along the last dimension of ``audio``.

    ``sos`` has shape ``(batch, n_sections, 6)`` with ``[b0, b1, b2, a0, a1,
    a2]`` rows as produced by ``dasp_pytorch.signal.biquad``; coefficients are
    per-batch and shared across any channel dimensions, matching the
    broadcasting of ``sosfilt_via_fsm``. Filtering uses ``clamp=False``
    throughout: intermediate EQ stages legitimately exceed ``[-1, 1]``.

    The recursion always runs in float64, mirroring the plugin's double-state
    biquads: float32 DF-I state is too imprecise for low-frequency, high-Q
    sections (pole radius 0.999+, e.g. the 95 Hz Q5.9 low shelf), diverging up
    to 5e-3 abs from the C++ reference on fuzzed chains, while float64 matches
    to ~1e-6. The result is cast back to the input dtype; the cast is
    differentiable, so gradients still flow to float32 parameters.
    """
    import torchaudio.functional as _filters

    if sos.ndim != 3 or sos.shape[-1] != 6:
        raise ValueError(
            "sos must have shape (batch, n_sections, 6), got %s" % (tuple(sos.shape),)
        )
    if sos.size(0) != audio.size(0):
        raise ValueError(
            "sos batch %d does not match audio batch %d" % (sos.size(0), audio.size(0))
        )
    batch = audio.size(0)
    stream = audio.to(dtype=torch.float64)
    coefficients = sos.to(dtype=torch.float64)
    flat = stream.reshape(batch, -1, stream.shape[-1])
    width = flat.shape[1]
    output = flat.reshape(batch * width, stream.shape[-1])
    for section in range(coefficients.shape[1]):
        feedforward = coefficients[:, section, :3]
        feedback = coefficients[:, section, 3:]
        feedforward = feedforward.unsqueeze(1).expand(batch, width, 3).reshape(batch * width, 3)
        feedback = feedback.unsqueeze(1).expand(batch, width, 3).reshape(batch * width, 3)
        # lfilter normalizes by a[0] in place on a flipped view of its input;
        # clone so the SOS tensor (and its autograd graph) is never mutated.
        output = _filters.lfilter(
            output, feedback.clone(), feedforward.clone(), clamp=False, batching=True
        )
    return output.reshape(stream.shape).to(dtype=audio.dtype)
