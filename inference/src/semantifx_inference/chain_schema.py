"""Dependency-free schema for SemantiFX's first fixed effect chain.

Parameter tables come from the generated module; edit protocol/effects/*.toml
and run scripts/gen to change them.
"""

from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple

from ._generated_effects import EFFECTS


@dataclass(frozen=True)
class ParameterSpec:
    name: str
    unit: str
    minimum: float
    maximum: float
    default: float


CHAIN: Tuple[Tuple[str, str, Sequence[ParameterSpec]], ...] = tuple(
    (instance_id, effect_type, tuple(ParameterSpec(*entry) for entry in parameters))
    for instance_id, effect_type, parameters in EFFECTS
)

EQ_INSTANCE_ID, DISTORTION_INSTANCE_ID = CHAIN[0][0], CHAIN[1][0]
EQ_PARAMETERS, DISTORTION_PARAMETERS = CHAIN[0][2], CHAIN[1][2]
EQ_THEN_DISTORTION = (EQ_INSTANCE_ID, DISTORTION_INSTANCE_ID)
DISTORTION_THEN_EQ = (DISTORTION_INSTANCE_ID, EQ_INSTANCE_ID)
CHAIN_ORDERS = (EQ_THEN_DISTORTION, DISTORTION_THEN_EQ)
_CHAIN_BY_ID = {instance_id: (effect_type, specs) for instance_id, effect_type, specs in CHAIN}


def default_chain(order: Sequence[str] = EQ_THEN_DISTORTION) -> List[dict]:
    normalized_order = tuple(order)
    if normalized_order not in CHAIN_ORDERS:
        raise ValueError("Effect order must contain eq-1 and distortion-1 exactly once.")
    return [
        {
            "instance_id": instance_id,
            "effect_type": effect_type,
            "parameters": {spec.name: spec.default for spec in specs},
        }
        for instance_id in normalized_order
        for effect_type, specs in [_CHAIN_BY_ID[instance_id]]
    ]


def instance_schema(instance_id: str) -> Tuple[str, Sequence[ParameterSpec]]:
    return _CHAIN_BY_ID[instance_id]


def chain_order(chain: Sequence[dict]) -> Tuple[str, ...]:
    return tuple(item["instance_id"] for item in chain)


def parameter_entries(values: Dict[str, Dict[str, float]]) -> List[dict]:
    entries = []
    for instance_id, effect_type, specs in CHAIN:
        effect_values = values[instance_id]
        entries.extend(
            {
                "effect_instance_id": instance_id,
                "effect": effect_type,
                "name": spec.name,
                "value": float(effect_values[spec.name]),
                "unit": spec.unit,
            }
            for spec in specs
        )
    return entries
