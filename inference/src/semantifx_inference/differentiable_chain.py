"""Differentiable rendering of a validated variable effect chain."""

from dataclasses import dataclass
from typing import Any, List, Mapping, Sequence

import torch

from .catalogue import CATALOGUE, EffectDefinition


@dataclass(frozen=True)
class _Slot:
    instance_id: str
    definition: EffectDefinition
    offset: int

    @property
    def width(self) -> int:
        return len(self.definition.specs)


def _layout(chain: Sequence[dict]) -> List[_Slot]:
    slots = []
    offset = 0
    seen = set()
    for index, item in enumerate(chain):
        instance_id = item["instance_id"]
        if instance_id in seen:
            raise ValueError("chain[%d].instance_id is duplicated" % index)
        seen.add(instance_id)
        try:
            definition = CATALOGUE[item["effect_type"]]
        except KeyError as exc:
            raise ValueError("chain[%d].effect_type is unknown" % index) from exc
        slots.append(_Slot(instance_id, definition, offset))
        offset += len(definition.specs)
    return slots


class VariableChain(torch.nn.Module):
    """Render effect instances in submitted order from one flat tensor."""

    def __init__(self, sample_rate: float, chain: List[dict]) -> None:
        super().__init__()
        self.sample_rate = sample_rate
        self.layout = _layout(chain)
        self.num_params = sum(slot.width for slot in self.layout)

    @staticmethod
    def normalized_configuration(chain: List[dict], device=None) -> torch.Tensor:
        values = []
        for item in chain:
            definition = CATALOGUE[item["effect_type"]]
            for spec in definition.specs:
                value = float(item["parameters"][spec.name])
                values.append((value - spec.minimum) / (spec.maximum - spec.minimum))
        return torch.tensor([values], dtype=torch.float32, device=device)

    @staticmethod
    def _physical(normalized: torch.Tensor, slot: _Slot) -> Mapping[str, torch.Tensor]:
        return {
            spec.name: spec.minimum
            + normalized[:, slot.offset + index] * (spec.maximum - spec.minimum)
            for index, spec in enumerate(slot.definition.specs)
        }

    def forward(self, signal: Any, normalized: torch.Tensor) -> Any:
        if not self.layout:
            return signal.clone()
        normalized = normalized.clamp(0.0, 1.0)
        output = signal.clone()
        audio = output.audio_data
        for slot in self.layout:
            audio = slot.definition.render(
                audio, self.sample_rate, self._physical(normalized, slot)
            )
        output.audio_data = audio
        return output

    def snapshot(self, normalized: torch.Tensor) -> List[dict]:
        normalized = normalized.detach().cpu().clamp(0.0, 1.0)
        entries = []
        for slot in self.layout:
            for index, spec in enumerate(slot.definition.specs):
                value = spec.minimum + normalized[0, slot.offset + index].item() * (
                    spec.maximum - spec.minimum
                )
                entries.append(
                    {
                        "effect_instance_id": slot.instance_id,
                        "effect": slot.definition.effect_type,
                        "name": spec.name,
                        "value": float(value),
                        "unit": spec.unit,
                    }
                )
        return entries
