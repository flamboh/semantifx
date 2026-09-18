import math
from typing import Any, Dict, Mapping, Optional, Sequence

from .catalogue import CATALOGUE
from .errors import SemantiFXError


_REQUEST_FIELDS = {"type", "request_id", "prompt", "audio", "chain", "options"}
_CHAIN_FIELDS = {"instance_id", "effect_type", "parameters"}
_AUDIO_FIELDS = {"path"}
_OPTION_FIELDS = {
    "backend",
    "model",
    "criterion",
    "iterations",
    "learning_rate",
    "parameter_initialization",
    "parameter_regularization",
    "roll_samples",
    "random_seed",
}


def _fail(message: str, field: Optional[str] = None) -> None:
    details = {"field": field} if field else {}
    raise SemantiFXError(
        "invalid_request",
        message,
        ["Send a request matching the protocol request schema."],
        details,
    )


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _check_unknown_fields(value: Mapping[str, Any], allowed: Sequence[str], location: str) -> None:
    unknown = sorted(set(value) - set(allowed))
    if unknown:
        _fail("Unknown field(s) at %s: %s" % (location, ", ".join(unknown)), location)


def _validate_options(options: Any) -> None:
    if not isinstance(options, dict):
        _fail("options must be an object.", "options")
    _check_unknown_fields(options, _OPTION_FIELDS, "options")
    enums = {
        "backend": {"mock", "clap"},
        "model": {"ms_clap", "laion_clap"},
        "criterion": {"cosine-sim", "directional_loss", "standard"},
        "parameter_initialization": {"current", "random"},
    }
    for field, allowed in enums.items():
        if field in options and options[field] not in allowed:
            _fail(
                "options.%s must be one of: %s." % (field, ", ".join(sorted(allowed))),
                "options.%s" % field,
            )
    if "iterations" in options and (
        not isinstance(options["iterations"], int)
        or isinstance(options["iterations"], bool)
        or not 1 <= options["iterations"] <= 10000
    ):
        _fail("options.iterations must be an integer from 1 to 10000.", "options.iterations")
    if "learning_rate" in options and (
        not _is_number(options["learning_rate"]) or options["learning_rate"] <= 0
    ):
        _fail("options.learning_rate must be a finite number greater than zero.", "options.learning_rate")
    if "parameter_regularization" in options and (
        not _is_number(options["parameter_regularization"])
        or options["parameter_regularization"] < 0
    ):
        _fail(
            "options.parameter_regularization must be a finite number greater than or equal to zero.",
            "options.parameter_regularization",
        )
    if "roll_samples" in options and (
        not isinstance(options["roll_samples"], int)
        or isinstance(options["roll_samples"], bool)
        or options["roll_samples"] < 0
    ):
        _fail("options.roll_samples must be an integer greater than or equal to zero.", "options.roll_samples")
    if "random_seed" in options and (
        not isinstance(options["random_seed"], int)
        or isinstance(options["random_seed"], bool)
        or not 0 <= options["random_seed"] <= 4294967295
    ):
        _fail("options.random_seed must be an integer from 0 to 4294967295.", "options.random_seed")


def _validate_chain(chain: Any) -> list[dict]:
    if not isinstance(chain, list) or not 0 <= len(chain) <= 8:
        _fail("chain must contain between 0 and 8 effect instances.", "chain")
    seen = set()
    for index, item in enumerate(chain):
        location = "chain[%d]" % index
        if not isinstance(item, dict):
            _fail("%s must be an object." % location, location)
        _check_unknown_fields(item, _CHAIN_FIELDS, location)
        instance_id = item.get("instance_id")
        if not isinstance(instance_id, str) or not instance_id or len(instance_id) > 64:
            _fail(
                "%s.instance_id must be a non-empty string of at most 64 characters."
                % location,
                location + ".instance_id",
            )
        if instance_id in seen:
            _fail("chain contains duplicate instance_id %r." % instance_id, "chain")
        seen.add(instance_id)

        effect_type = item.get("effect_type")
        if not isinstance(effect_type, str) or effect_type not in CATALOGUE:
            _fail(
                "%s.effect_type must name an effect type in the catalogue." % location,
                location + ".effect_type",
            )
        definition = CATALOGUE[effect_type]
        parameters = item.get("parameters")
        if not isinstance(parameters, dict):
            _fail("%s.parameters must be an object." % location, location + ".parameters")
        expected_names = {spec.name for spec in definition.specs}
        if set(parameters) != expected_names:
            _fail(
                "%s.parameters must contain the complete parameter set for %s."
                % (location, effect_type),
                location + ".parameters",
            )
        for spec in definition.specs:
            parameter = parameters[spec.name]
            if not _is_number(parameter) or not spec.minimum <= parameter <= spec.maximum:
                _fail(
                    "%s.parameters.%s must be between %s and %s."
                    % (location, spec.name, spec.minimum, spec.maximum),
                    "%s.parameters.%s" % (location, spec.name),
                )
    return chain


def validate_request(value: Any) -> Dict[str, Any]:
    """Validate an analyze request without mutating it."""
    if not isinstance(value, dict):
        _fail("Request must be a JSON object.")
    _check_unknown_fields(value, _REQUEST_FIELDS, "request")
    for field in ("type", "request_id", "prompt", "audio", "chain"):
        if field not in value:
            _fail("Missing required field: %s" % field, field)
    if value["type"] != "analyze":
        _fail("type must be 'analyze'.", "type")
    for field, maximum in (("request_id", 200), ("prompt", 4000)):
        item = value[field]
        if not isinstance(item, str) or not item.strip() or len(item) > maximum:
            _fail(
                "%s must be a non-empty string of at most %d characters." % (field, maximum),
                field,
            )

    audio = value["audio"]
    if not isinstance(audio, dict):
        _fail("audio must be an object.", "audio")
    _check_unknown_fields(audio, _AUDIO_FIELDS, "audio")
    if not isinstance(audio.get("path"), str) or not audio["path"].strip():
        _fail("audio.path must be a non-empty string.", "audio.path")

    _validate_chain(value["chain"])
    _validate_options(value.get("options", {}))
    return value


def ok_result(
    request_id: str,
    backend: str,
    elapsed_ms: float,
    chain_order: Sequence[str],
    parameters: list,
    metrics: Optional[dict] = None,
    warnings: Optional[list] = None,
) -> dict:
    return {
        "type": "analysis_result",
        "request_id": request_id,
        "status": "ok",
        "backend": backend,
        "elapsed_ms": round(elapsed_ms, 3),
        "chain_order": list(chain_order),
        "parameters": parameters,
        "metrics": metrics or {},
        "warnings": warnings or [],
    }


def error_result(
    error: SemantiFXError,
    request_id: Optional[str],
    backend: Optional[str],
    elapsed_ms: float,
) -> dict:
    return {
        "type": "analysis_result",
        "request_id": request_id,
        "status": "error",
        "backend": backend,
        "elapsed_ms": round(elapsed_ms, 3),
        "error": {
            "code": error.code,
            "message": error.message,
            "remediation": error.remediation,
            "details": error.details,
        },
    }
