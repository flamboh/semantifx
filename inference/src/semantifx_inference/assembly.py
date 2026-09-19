"""Catalogue-driven chain assembly policy."""

from itertools import combinations
from typing import TYPE_CHECKING, Mapping, Sequence

from .catalogue import CATALOGUE, default_chain

if TYPE_CHECKING:
    from .planner import Judgments, Planner


ADD_THRESHOLD = 0.4
NO_CHANGE_THRESHOLD = 0.5


CATALOGUE_DESCRIPTIONS = {
    effect_type: definition.description
    for effect_type, definition in CATALOGUE.items()
}


def build_state(target_prompt: str, catalogue: Mapping[str, str]) -> dict:
    """Build the state sent to Jev for either assembly round."""
    return {"target_prompt": target_prompt, "effects": dict(catalogue)}


def selection_questions(catalogue: Mapping[str, str]) -> dict:
    questions = {
        f"use_{effect_type}": {
            "type": "noul",
            "instructions": (
                f"Is effects.{effect_type} a useful effect for achieving the sound "
                "described by target_prompt?"
            ),
            "criteria": {
                "true": description,
                "false": f"Do not include effects.{effect_type} in the chain.",
            },
        }
        for effect_type, description in catalogue.items()
    }
    questions["no_change"] = {
        "type": "noul",
        "instructions": "Is the sound already right, or does target_prompt ask for no processing?",
        "criteria": {
            "true": "The sound is already right or the prompt asks for no processing.",
            "false": "The prompt asks for a sound change.",
        },
    }
    return questions


def order_questions(selected: Sequence[str]) -> dict:
    return {
        f"before_{first}_{second}": {
            "type": "noul",
            "instructions": (
                f"Should effects.{first} be placed before effects.{second} "
                "in the signal chain for target_prompt?"
            ),
            "criteria": {
                "true": f"Place effects.{first} before effects.{second}.",
                "false": f"Place effects.{second} before effects.{first}.",
            },
        }
        for first, second in combinations(selected, 2)
    }


def _answer_probability(value: object, default: float = 0.0) -> float:
    if isinstance(value, Mapping):
        value = value.get("noul", default)
    return float(value)


def _selection_values(value: Mapping) -> dict[str, float]:
    """Accept the wire-shaped prototype answers and the planner shape."""
    if value and all(isinstance(answer, Mapping) for answer in value.values()):
        return {
            str(effect_type): _answer_probability(answer)
            for effect_type, answer in value.items()
        }
    return {
        str(effect_type): _answer_probability(answer)
        for effect_type, answer in value.items()
    }


def selected_effects(
    selection: Mapping[str, float],
    no_change: float = 0.0,
    catalogue: Mapping[str, str] = CATALOGUE_DESCRIPTIONS,
) -> list[str]:
    """Apply the no-change gate and selection threshold, including argmax fallback."""
    if no_change >= NO_CHANGE_THRESHOLD:
        return []
    probabilities = {
        effect_type: float(selection.get(effect_type, 0.0))
        for effect_type in catalogue
    }
    selected = [
        effect_type
        for effect_type, probability in probabilities.items()
        if probability >= ADD_THRESHOLD
    ]
    if not selected and probabilities:
        selected = [max(probabilities, key=probabilities.get)]
    return selected


def select_effects(answers: Mapping, catalogue: Mapping[str, str]) -> list[str]:
    """Compatibility helper for the original prototype answer shape."""
    no_change = _answer_probability(answers.get("no_change", 0.0))
    raw_selection = {
        str(effect_type).removeprefix("use_"): answer
        for effect_type, answer in answers.items()
        if str(effect_type).startswith("use_")
    }
    return selected_effects(_selection_values(raw_selection), no_change, catalogue)


def _order_probability(answers: Mapping, first: str, second: str) -> float:
    key = f"{first}|{second}"
    if key in answers:
        return _answer_probability(answers[key], 0.5)
    return _answer_probability(answers.get(f"before_{first}_{second}", 0.5), 0.5)


def order_effects(selected: Sequence[str], answers: Mapping) -> list[str]:
    """Use a deterministic Borda tally that tolerates cyclic preferences."""
    if len(selected) < 2:
        return list(selected)
    scores = {effect_type: 0.0 for effect_type in selected}
    for first, second in combinations(selected, 2):
        probability = _order_probability(answers, first, second)
        scores[first] += probability
        scores[second] += 1.0 - probability
    catalogue_order = {effect_type: index for index, effect_type in enumerate(CATALOGUE)}
    next_order = len(catalogue_order)
    for effect_type in selected:
        if effect_type not in catalogue_order:
            catalogue_order[effect_type] = next_order
            next_order += 1
    return sorted(
        selected,
        key=lambda effect_type: (-scores[effect_type], catalogue_order[effect_type]),
    )


def to_chain(ordered_types: Sequence[str]) -> list[dict]:
    return default_chain(ordered_types)


def assemble(prompt: str, planner: "Planner") -> tuple[list[dict], "Judgments", list[str]]:
    """Apply assembly policy to planner judgments and mint a default chain."""
    judgments = planner.assemble(prompt)
    selected = selected_effects(judgments.selection, judgments.no_change)
    warnings = []
    catalogue_probabilities = [
        judgments.selection.get(effect_type, 0.0)
        for effect_type in CATALOGUE_DESCRIPTIONS
    ]
    if (
        judgments.no_change < NO_CHANGE_THRESHOLD
        and catalogue_probabilities
        and not any(probability >= ADD_THRESHOLD for probability in catalogue_probabilities)
    ):
        warnings.append(
            "No effect cleared the selection threshold; picked %s by argmax."
            % selected[0]
        )
    ordered = order_effects(selected, judgments.order)
    return to_chain(ordered), judgments, warnings
