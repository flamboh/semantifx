"""Assembly planners for local mock and TypeSafe Jev backends."""

from collections.abc import Mapping
from dataclasses import dataclass
import math
from typing import Any, Protocol

from .assembly import (
    CATALOGUE_DESCRIPTIONS,
    build_state,
    order_questions,
    selected_effects,
    selection_questions,
)
from .errors import SemantiFXError


_REMEDIATION = [
    "Set TYPESAFE_API_KEY in the repository .env or worker environment.",
    "Retry later if the TypeSafe service is unavailable.",
]


@dataclass(frozen=True)
class Judgments:
    selection: dict[str, float]
    no_change: float
    order: dict[str, float]


class Planner(Protocol):
    def assemble(self, prompt: str) -> Judgments:
        """Return selection, no-change, and pairwise order probabilities."""


_SELECTION_WORDS = {
    "eq": {
        "bright": 0.8,
        "brighter": 0.8,
        "presence": 0.75,
        "present": 0.75,
        "warm": 0.7,
        "muddy": 0.8,
        "mud": 0.8,
        "clarity": 0.7,
        "clear": 0.7,
        "harsh": 0.6,
        "thin": 0.65,
        "top end": 0.7,
    },
    "distortion": {
        "gritty": 0.9,
        "grit": 0.9,
        "fuzz": 0.95,
        "fuzzy": 0.9,
        "saturated": 0.8,
        "saturation": 0.8,
        "aggressive": 0.85,
        "distorted": 0.85,
        "crunchy": 0.8,
        "warm": 0.3,
    },
    "compressor": {
        "punchy": 0.85,
        "punch": 0.85,
        "tight": 0.85,
        "glued": 0.9,
        "glue": 0.9,
        "compressed": 0.8,
        "compression": 0.8,
        "sustain": 0.7,
        "controlled": 0.8,
        "dynamics": 0.7,
    },
    "reverb": {
        "spacious": 0.9,
        "space": 0.9,
        "hall": 0.9,
        "room": 0.85,
        "ambient": 0.85,
        "ambience": 0.85,
        "distant": 0.85,
        "depth": 0.7,
        "wash": 0.8,
        "dreamy": 0.8,
        "reverb": 0.9,
    },
}

_ORDER_PREFERENCES = {
    ("eq", "distortion"): 0.7,
    ("eq", "compressor"): 0.4,
    ("eq", "reverb"): 0.8,
    ("distortion", "compressor"): 0.5,
    ("distortion", "reverb"): 0.8,
    ("compressor", "reverb"): 0.8,
}


class MockPlanner:
    """Deterministic keyword planner used by tests and local development."""

    def assemble(self, prompt: str) -> Judgments:
        lowered = prompt.casefold()
        selection = {
            effect_type: max(
                (
                    probability
                    for word, probability in _SELECTION_WORDS.get(effect_type, {}).items()
                    if word in lowered
                ),
                default=0.1,
            )
            for effect_type in CATALOGUE_DESCRIPTIONS
        }
        no_change = (
            0.9
            if any(phrase in lowered for phrase in ("as it is", "no change", "leave it"))
            else 0.1
        )
        selected = selected_effects(selection, no_change)
        order = {
            f"{first}|{second}": _ORDER_PREFERENCES.get((first, second), 0.5)
            for index, first in enumerate(selected)
            for second in selected[index + 1 :]
        }
        return Judgments(selection, no_change, order)


class JevPlanner:
    """Two-round TypeSafe Jev planner with one retry for transient failures."""

    def __init__(self, api_key: str | None, timeout_s: float = 10.0) -> None:
        self.api_key = api_key
        self.timeout_s = timeout_s

    def assemble(self, prompt: str) -> Judgments:
        if not self.api_key or not self.api_key.strip():
            raise self._unavailable(
                "TypeSafe Jev assembly needs TYPESAFE_API_KEY.",
                {"reason": "missing_api_key"},
            )

        try:
            from typesafe_sdk import (
                RetryPolicy,
                TypeSafeAPIError,
                TypeSafeAPITimeoutError,
                TypeSafeClient,
                TypeSafeRateLimitError,
            )
        except ImportError as exc:
            raise self._unavailable(
                "The TypeSafe SDK is not installed.",
                {"exception_type": type(exc).__name__},
                ["Install the optional assembly dependency with inference's assembly extra."],
            ) from exc

        try:
            with TypeSafeClient(
                api_key=self.api_key,
                timeout=self.timeout_s,
                retry=RetryPolicy(max_retries=0),
            ) as client:
                selection_answers = self._request(
                    client,
                    prompt,
                    selection_questions(CATALOGUE_DESCRIPTIONS),
                    TypeSafeAPIError,
                    TypeSafeAPITimeoutError,
                    TypeSafeRateLimitError,
                )
                selection = {
                    question_id.removeprefix("use_"): probability
                    for question_id, probability in selection_answers.items()
                    if question_id.startswith("use_")
                }
                no_change = selection_answers["no_change"]
                selected = selected_effects(selection, no_change)
                order = {}
                if len(selected) >= 2:
                    order_answers = self._request(
                        client,
                        prompt,
                        order_questions(selected),
                        TypeSafeAPIError,
                        TypeSafeAPITimeoutError,
                        TypeSafeRateLimitError,
                    )
                    order = {
                        question_id.removeprefix("before_").replace("_", "|", 1): probability
                        for question_id, probability in order_answers.items()
                    }
        except SemantiFXError:
            raise
        except (TypeSafeAPITimeoutError, TypeSafeRateLimitError) as exc:
            raise self._unavailable(
                "TypeSafe Jev assembly failed after one retry.",
                {"exception_type": type(exc).__name__},
            ) from exc
        except TypeSafeAPIError as exc:
            if getattr(exc, "status", None) == 529:
                raise self._unavailable(
                    "TypeSafe Jev assembly failed after one retry.",
                    {"status": exc.status},
                ) from exc
            raise self._unavailable(
                "TypeSafe Jev assembly request failed.",
                {"status": getattr(exc, "status", None)},
            ) from exc
        except Exception as exc:
            raise self._unavailable(
                "TypeSafe Jev assembly returned an unusable response.",
                {"exception_type": type(exc).__name__},
            ) from exc

        return Judgments(selection, no_change, order)

    def _request(
        self,
        client: Any,
        prompt: str,
        questions: dict,
        api_error_type: type[BaseException],
        timeout_error_type: type[BaseException],
        rate_limit_error_type: type[BaseException],
    ) -> dict[str, float]:
        last_error = None
        for attempt in range(2):
            try:
                response = client.system_one(
                    state=build_state(prompt, CATALOGUE_DESCRIPTIONS),
                    questions=questions,
                )
                return self._probabilities(response, questions)
            except (timeout_error_type, rate_limit_error_type) as exc:
                last_error = exc
            except api_error_type as exc:
                if getattr(exc, "status", None) != 529:
                    raise
                last_error = exc
            if attempt == 0:
                continue
        raise self._unavailable(
            "TypeSafe Jev assembly failed after one retry.",
            {"exception_type": type(last_error).__name__ if last_error else "unknown"},
        )

    @staticmethod
    def _probabilities(response: Any, questions: Mapping[str, Any]) -> dict[str, float]:
        raw_answers = getattr(response, "nouls", None)
        if raw_answers is None and isinstance(response, Mapping):
            raw_answers = response.get("nouls", response.get("answers", response))
        if not isinstance(raw_answers, Mapping):
            raise JevPlanner._unavailable_static(
                "TypeSafe Jev returned no noul answers.",
                {"missing_question_ids": sorted(questions)},
            )

        missing = sorted(set(questions) - set(raw_answers))
        if missing:
            raise JevPlanner._unavailable_static(
                "TypeSafe Jev returned an incomplete answer set.",
                {"missing_question_ids": missing},
            )
        probabilities = {}
        for question_id in questions:
            answer = raw_answers[question_id]
            value = getattr(answer, "noul", None)
            if value is None and isinstance(answer, Mapping):
                value = answer.get("noul")
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise JevPlanner._unavailable_static(
                    "TypeSafe Jev returned a malformed noul answer.",
                    {"question_id": question_id},
                )
            value = float(value)
            if not math.isfinite(value) or not 0.0 <= value <= 1.0:
                raise JevPlanner._unavailable_static(
                    "TypeSafe Jev returned an out-of-range noul answer.",
                    {"question_id": question_id},
                )
            probabilities[question_id] = value
        return probabilities

    def _unavailable(self, message: str, details: dict, extra: list[str] | None = None) -> SemantiFXError:
        remediation = list(_REMEDIATION)
        if extra:
            remediation[0:0] = extra
        return SemantiFXError("assembly_unavailable", message, remediation, details)

    @staticmethod
    def _unavailable_static(message: str, details: dict) -> SemantiFXError:
        return SemantiFXError("assembly_unavailable", message, _REMEDIATION, details)
