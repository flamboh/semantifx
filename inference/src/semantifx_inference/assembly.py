"""Catalogue-driven chain assembly decisions."""

from itertools import combinations

from .chain_schema import CHAIN

ADD_THRESHOLD = 0.4
NO_CHANGE_THRESHOLD = 0.5

DEFAULT_CATALOGUE: dict[str, str] = {
    "eq": "Six-band parametric equalizer with a low shelf, four peaking bands, and a high shelf. Boosts or cuts frequency ranges to shape brightness, warmth, body, presence, harshness, mud, thinness, and clarity.",
    "distortion": "Distortion with drive, character, tone, and wet/dry mix. Nonlinear waveshaping that adds harmonics for saturation, grit, warmth from soft clipping, overdrive, fuzz, and aggression.",
}


def build_state(target_prompt: str, source: str, catalogue: dict[str, str]) -> dict:
    return {"target_prompt": target_prompt, "source": source, "effects": catalogue}


def selection_questions(catalogue: dict[str, str]) -> dict:
    questions = {
        f"use_{effect_type}": {
            "type": "noul",
            "instructions": f"Is `effects.{effect_type}` a useful effect for achieving the sound described by `target_prompt` on `source`?",
            "criteria": {"true": description, "false": f"Do not include `effects.{effect_type}` in the chain."},
        }
        for effect_type, description in catalogue.items()
    }
    questions["no_change"] = {
        "type": "noul",
        "instructions": "Is the sound already right, or does `target_prompt` ask for no processing on `source`?",
        "criteria": {"true": "The sound is already right or the prompt asks for no processing.", "false": "The prompt asks for a sound change."},
    }
    return questions


def select_effects(answers: dict, catalogue: dict[str, str]) -> list[str]:
    if answers.get("no_change", {}).get("noul", 0.0) >= NO_CHANGE_THRESHOLD:
        return []
    probabilities = {effect_type: float(answers.get(f"use_{effect_type}", {}).get("noul", 0.0)) for effect_type in catalogue}
    selected = [effect_type for effect_type, probability in probabilities.items() if probability >= ADD_THRESHOLD]
    if not selected and probabilities:
        selected = [max(catalogue, key=lambda effect_type: probabilities[effect_type])]
    return selected


def order_questions(selected: list[str]) -> dict:
    return {
        f"before_{first}_{second}": {
            "type": "noul",
            "instructions": f"To achieve `target_prompt` on `source`, should `effects.{first}` be placed before `effects.{second}` in the signal chain?",
            "criteria": {"true": f"Place `effects.{first}` before `effects.{second}`.", "false": f"Place `effects.{second}` before `effects.{first}`."},
        }
        for first, second in combinations(selected, 2)
    }


def order_effects(selected: list[str], answers: dict) -> list[str]:
    """Use a deterministic Borda tally that tolerates cyclic preferences."""
    if len(selected) < 2:
        return list(selected)
    scores = {effect_type: 0.0 for effect_type in selected}
    for first, second in combinations(selected, 2):
        probability = float(answers.get(f"before_{first}_{second}", {}).get("noul", 0.5))
        scores[first] += probability
        scores[second] += 1.0 - probability
    catalogue_order = {effect_type: index for index, effect_type in enumerate(selected)}
    return sorted(selected, key=lambda effect_type: (-scores[effect_type], catalogue_order[effect_type]))


def to_chain(ordered_types: list[str]) -> list[dict]:
    specs_by_type = {}
    for _instance_id, effect_type, specs in CHAIN:
        specs_by_type.setdefault(effect_type, specs)
    counts: dict[str, int] = {}
    chain = []
    for effect_type in ordered_types:
        if effect_type not in specs_by_type:
            raise ValueError(f"Unknown effect type: {effect_type}")
        counts[effect_type] = counts.get(effect_type, 0) + 1
        chain.append({
            "instance_id": f"{effect_type}-{counts[effect_type]}",
            "effect_type": effect_type,
            "parameters": {spec.name: spec.default for spec in specs_by_type[effect_type]},
        })
    return chain


def optimizer_note(chain: list[dict]) -> str | None:
    supported = {"eq-1", "distortion-1"}
    instance_ids = [item["instance_id"] for item in chain]
    if len(instance_ids) == 2 and set(instance_ids) == supported:
        return None
    return "The current optimizer supports exactly one eq-1 and one distortion-1 instance."
