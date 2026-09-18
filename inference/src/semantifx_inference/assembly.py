"""Catalogue-driven chain assembly decisions."""

from itertools import combinations

from .catalogue import CATALOGUE, default_chain

ADD_THRESHOLD = 0.4
NO_CHANGE_THRESHOLD = 0.5

CATALOGUE_DESCRIPTIONS = {
    effect_type: definition.description
    for effect_type, definition in CATALOGUE.items()
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
    probabilities = {
        effect_type: float(answers.get(f"use_{effect_type}", {}).get("noul", 0.0))
        for effect_type in catalogue
    }
    selected = [
        effect_type
        for effect_type, probability in probabilities.items()
        if probability >= ADD_THRESHOLD
    ]
    if not selected and probabilities:
        selected = [max(catalogue, key=lambda effect_type: probabilities[effect_type])]
    return selected


def order_questions(selected: list[str]) -> dict:
    return {
        f"before_{first}_{second}": {
            "type": "noul",
            "instructions": f"To achieve `target_prompt` on `source`, should `effects.{first}` be placed before `effects.{second}` in the signal chain?",
            "criteria": {
                "true": f"Place `effects.{first}` before `effects.{second}`.",
                "false": f"Place `effects.{second}` before `effects.{first}`.",
            },
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
    catalogue_order = {effect_type: index for index, effect_type in enumerate(CATALOGUE)}
    next_order = len(catalogue_order)
    for effect_type in selected:
        if effect_type not in catalogue_order:
            catalogue_order[effect_type] = next_order
            next_order += 1
    return sorted(selected, key=lambda effect_type: (-scores[effect_type], catalogue_order[effect_type]))


def to_chain(ordered_types: list[str]) -> list[dict]:
    return default_chain(ordered_types)
