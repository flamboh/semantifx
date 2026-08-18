"""Dependency-free schema for SemantiFX's first fixed effect chain."""

from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple


@dataclass(frozen=True)
class ParameterSpec:
    name: str
    unit: str
    minimum: float
    maximum: float
    default: float


EQ_INSTANCE_ID = "eq-1"
DISTORTION_INSTANCE_ID = "distortion-1"

EQ_PARAMETERS: Tuple[ParameterSpec, ...] = (
    ParameterSpec("low_shelf_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("low_shelf_cutoff_freq", "Hz", 20.0, 2000.0, 200.0),
    ParameterSpec("low_shelf_q_factor", "ratio", 0.1, 6.0, 0.7071),
    ParameterSpec("band0_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("band0_cutoff_freq", "Hz", 80.0, 2000.0, 500.0),
    ParameterSpec("band0_q_factor", "ratio", 0.1, 6.0, 0.7071),
    ParameterSpec("band1_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("band1_cutoff_freq", "Hz", 2000.0, 8000.0, 3500.0),
    ParameterSpec("band1_q_factor", "ratio", 0.1, 6.0, 0.7071),
    ParameterSpec("band2_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("band2_cutoff_freq", "Hz", 8000.0, 12000.0, 9000.0),
    ParameterSpec("band2_q_factor", "ratio", 0.1, 6.0, 0.7071),
    ParameterSpec("band3_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("band3_cutoff_freq", "Hz", 12000.0, 21050.0, 14000.0),
    ParameterSpec("band3_q_factor", "ratio", 0.1, 6.0, 0.7071),
    ParameterSpec("high_shelf_gain_db", "dB", -20.0, 20.0, 0.0),
    ParameterSpec("high_shelf_cutoff_freq", "Hz", 4000.0, 21050.0, 12000.0),
    ParameterSpec("high_shelf_q_factor", "ratio", 0.1, 6.0, 0.7071),
)

DISTORTION_PARAMETERS: Tuple[ParameterSpec, ...] = (
    ParameterSpec("drive_db", "dB", 0.0, 48.0, 0.0),
    ParameterSpec("character", "ratio", 0.0, 1.0, 0.5),
    ParameterSpec("tone_db", "dB", -12.0, 12.0, 0.0),
    ParameterSpec("mix", "ratio", 0.0, 1.0, 0.25),
)

CHAIN: Tuple[Tuple[str, str, Sequence[ParameterSpec]], ...] = (
    (EQ_INSTANCE_ID, "eq", EQ_PARAMETERS),
    (DISTORTION_INSTANCE_ID, "distortion", DISTORTION_PARAMETERS),
)
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
