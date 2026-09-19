#!/usr/bin/env python3
"""Generate SemantiFX's deterministic 44.1 kHz reverb noise table."""

import hashlib
import json
from pathlib import Path

import torch
import torch.nn.functional as F

from dasp_pytorch.signal import octave_band_filterbank


SEED = 20_260_918
SAMPLE_RATE = 44_100.0
CHANNELS = 2
BANDS = 12
SAMPLES = 65_536
TAPS = 1023

REPO = Path(__file__).resolve().parents[2]
OUTPUT_DIR = REPO / "protocol" / "reverb"
NOISE_PATH = OUTPUT_DIR / "noise_44100.f32"
METADATA_PATH = OUTPUT_DIR / "noise_44100.json"


def freeze_noise() -> torch.Tensor:
    """Return filtered noise with shape ``(channels, bands, samples)``."""
    torch.manual_seed(SEED)
    white = torch.randn(CHANNELS, BANDS, SAMPLES + TAPS - 1, dtype=torch.float32)
    filters = octave_band_filterbank(TAPS, SAMPLE_RATE)
    return F.conv1d(white, filters, groups=BANDS)


def write_table(table: torch.Tensor) -> str:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    raw = table.detach().cpu().contiguous().numpy().astype("<f4", copy=False).tobytes()
    NOISE_PATH.write_bytes(raw)
    metadata = {
        "sample_rate": int(SAMPLE_RATE),
        "channels": CHANNELS,
        "bands": BANDS,
        "samples": SAMPLES,
        "seed": SEED,
        "taps": TAPS,
        "sha256": hashlib.sha256(raw).hexdigest(),
    }
    METADATA_PATH.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata["sha256"]


if __name__ == "__main__":
    result = freeze_noise()
    if tuple(result.shape) != (CHANNELS, BANDS, SAMPLES):
        raise RuntimeError("Unexpected frozen noise shape: %s" % (tuple(result.shape),))
    print(write_table(result))
