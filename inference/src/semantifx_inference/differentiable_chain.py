"""Differentiable implementation of the fixed EQ -> distortion chain."""

from typing import Dict, List

import dasp_pytorch
import torch

from .chain_schema import (
    CHAIN,
    DISTORTION_INSTANCE_ID,
    DISTORTION_THEN_EQ,
    EQ_INSTANCE_ID,
    chain_order,
    parameter_entries,
)
from .distortion import process as process_distortion


class EqDistortionChain(torch.nn.Module):
    """Render and serialize the first SemantiFX-owned fixed chain."""

    def __init__(self, sample_rate: float, chain: List[dict]) -> None:
        super().__init__()
        self.sample_rate = sample_rate
        self.eq = dasp_pytorch.ParametricEQ(sample_rate=sample_rate)
        self.distortion_first = chain_order(chain) == DISTORTION_THEN_EQ
        self.num_params = sum(len(specs) for _instance_id, _effect_type, specs in CHAIN)

    @staticmethod
    def normalized_configuration(chain: List[dict], device=None) -> torch.Tensor:
        configuration = {item["instance_id"]: item["parameters"] for item in chain}
        values = []
        for instance_id, _effect_type, specs in CHAIN:
            for spec in specs:
                value = float(configuration[instance_id][spec.name])
                values.append((value - spec.minimum) / (spec.maximum - spec.minimum))
        return torch.tensor([values], dtype=torch.float32, device=device)

    @staticmethod
    def _physical(normalized: torch.Tensor, offset: int, specs) -> Dict[str, torch.Tensor]:
        return {
            spec.name: spec.minimum + normalized[:, offset + index] * (spec.maximum - spec.minimum)
            for index, spec in enumerate(specs)
        }

    def forward(self, signal, normalized: torch.Tensor):
        normalized = normalized.clamp(0.0, 1.0)
        output = signal.clone()
        eq_specs = CHAIN[0][2]
        distortion_specs = CHAIN[1][2]
        distortion_values = self._physical(normalized, len(eq_specs), distortion_specs)
        def apply_eq(audio):
            return self.eq.process_normalized(audio, normalized[:, :len(eq_specs)])

        def apply_distortion(audio):
            return process_distortion(
                audio,
                self.sample_rate,
                drive_db=distortion_values["drive_db"],
                character=distortion_values["character"],
                tone_db=distortion_values["tone_db"],
                mix=distortion_values["mix"],
            )

        if self.distortion_first:
            output.audio_data = apply_eq(apply_distortion(output.audio_data))
        else:
            output.audio_data = apply_distortion(apply_eq(output.audio_data))
        return output

    def snapshot(self, normalized: torch.Tensor) -> List[dict]:
        normalized = normalized.detach().cpu().clamp(0.0, 1.0)
        eq_specs = CHAIN[0][2]
        distortion_specs = CHAIN[1][2]
        values = {
            EQ_INSTANCE_ID: {
                spec.name: float(
                    spec.minimum + normalized[0, index].item() * (spec.maximum - spec.minimum)
                )
                for index, spec in enumerate(eq_specs)
            },
            DISTORTION_INSTANCE_ID: {
                spec.name: float(
                    spec.minimum
                    + normalized[0, len(eq_specs) + index].item() * (spec.maximum - spec.minimum)
                )
                for index, spec in enumerate(distortion_specs)
            },
        }
        return parameter_entries(values)
