"""The type-keyed effect catalogue shared by inference and chain assembly."""

from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Mapping, Sequence

from ._generated_effects import EFFECTS


@dataclass(frozen=True)
class ParameterSpec:
    name: str
    unit: str
    minimum: float
    maximum: float
    default: float


@dataclass(frozen=True)
class EffectDefinition:
    effect_type: str
    specs: tuple[ParameterSpec, ...]
    description: str
    render: Callable[[Any, float, Mapping[str, Any]], Any]


def _render_eq(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    from .effects.eq import render

    return render(audio, sample_rate, params)


def _render_distortion(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    from .effects.distortion import render

    return render(audio, sample_rate, params)


def _render_compressor(audio: Any, sample_rate: float, params: Mapping[str, Any]) -> Any:
    from .effects.compressor import render

    return render(audio, sample_rate, params)


_RENDERERS = {
    "eq": _render_eq,
    "distortion": _render_distortion,
    "compressor": _render_compressor,
}


def _build_catalogue() -> Dict[str, EffectDefinition]:
    catalogue = {}
    for effect_type, description, parameters in EFFECTS:
        try:
            render = _RENDERERS[effect_type]
        except KeyError as exc:
            raise RuntimeError("No renderer is registered for effect type %r." % effect_type) from exc
        catalogue[effect_type] = EffectDefinition(
            effect_type=effect_type,
            specs=tuple(ParameterSpec(*entry) for entry in parameters),
            description=description,
            render=render,
        )
    return catalogue


CATALOGUE = _build_catalogue()


def default_chain(types: Sequence[str] = ("eq", "distortion")) -> List[dict]:
    """Mint a chain in type order with each type's TOML defaults."""
    counts: Dict[str, int] = {}
    chain = []
    for effect_type in types:
        if effect_type not in CATALOGUE:
            raise ValueError("Unknown effect type: %s" % effect_type)
        counts[effect_type] = counts.get(effect_type, 0) + 1
        definition = CATALOGUE[effect_type]
        chain.append(
            {
                "instance_id": "%s-%d" % (effect_type, counts[effect_type]),
                "effect_type": effect_type,
                "parameters": {spec.name: spec.default for spec in definition.specs},
            }
        )
    return chain


def parameter_entries(chain: Sequence[dict], values: Mapping[str, Mapping[str, float]]) -> List[dict]:
    """Serialize physical parameter values in submitted chain and spec order."""
    entries = []
    for item in chain:
        definition = CATALOGUE[item["effect_type"]]
        effect_values = values[item["instance_id"]]
        entries.extend(
            {
                "effect_instance_id": item["instance_id"],
                "effect": definition.effect_type,
                "name": spec.name,
                "value": float(effect_values[spec.name]),
                "unit": spec.unit,
            }
            for spec in definition.specs
        )
    return entries
