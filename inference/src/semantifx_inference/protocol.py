import math
from typing import Any, Dict, Mapping, Optional, Sequence

from .errors import SemantiFXError


PROTOCOL_VERSION = "1"

_REQUEST_FIELDS = {
    "protocol_version", "type", "request_id", "prompt", "audio", "effects", "options"
}
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
_BACKENDS = {"mock", "text2fx"}
_MODELS = {"ms_clap", "laion_clap"}
_CRITERIA = {"cosine-sim", "directional_loss", "standard"}
_INITIALIZATIONS = {"zeros", "random", "super_random"}


def _fail(message: str, field: Optional[str] = None) -> None:
    details = {"field": field} if field else {}
    raise SemantiFXError(
        "invalid_request",
        message,
        ["Send a request matching protocol/schemas/v1/request.schema.json."],
        details,
    )


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _check_unknown_fields(value: Mapping[str, Any], allowed: Sequence[str], location: str) -> None:
    unknown = sorted(set(value) - set(allowed))
    if unknown:
        _fail("Unknown field(s) at %s: %s" % (location, ", ".join(unknown)), location)


def validate_request(value: Any) -> Dict[str, Any]:
    """Validate and return a v1 request without mutating it.

    The worker deliberately uses a small handwritten validator so its mock path
    stays dependency-free. The JSON Schema remains the language-neutral source
    of truth.
    """
    if not isinstance(value, dict):
        _fail("Request must be a JSON object.")
    _check_unknown_fields(value, _REQUEST_FIELDS, "request")
    for field in ("protocol_version", "type", "request_id", "prompt", "audio", "effects"):
        if field not in value:
            _fail("Missing required field: %s" % field, field)
    if value["protocol_version"] != PROTOCOL_VERSION:
        raise SemantiFXError(
            "unsupported_protocol_version",
            "Protocol version %r is not supported." % value["protocol_version"],
            ["Use protocol_version \"%s\"." % PROTOCOL_VERSION],
            {"supported_versions": [PROTOCOL_VERSION]},
        )
    if value["type"] != "analyze":
        _fail("type must be 'analyze'.", "type")
    for field, maximum in (("request_id", 200), ("prompt", 4000)):
        item = value[field]
        if not isinstance(item, str) or not item.strip() or len(item) > maximum:
            _fail("%s must be a non-empty string of at most %d characters." % (field, maximum), field)

    audio = value["audio"]
    if not isinstance(audio, dict):
        _fail("audio must be an object.", "audio")
    _check_unknown_fields(audio, _AUDIO_FIELDS, "audio")
    if not isinstance(audio.get("path"), str) or not audio["path"].strip():
        _fail("audio.path must be a non-empty string.", "audio.path")
    effects = value["effects"]
    if not isinstance(effects, list) or len(effects) != 1:
        _fail("effects must contain exactly one effect in protocol v1.", "effects")
    if any(effect != "eq" for effect in effects):
        _fail("Protocol v1 supports only the 'eq' effect.", "effects")

    options = value.get("options", {})
    if not isinstance(options, dict):
        _fail("options must be an object.", "options")
    _check_unknown_fields(options, _OPTION_FIELDS, "options")
    enums = {
        "backend": _BACKENDS,
        "model": _MODELS,
        "criterion": _CRITERIA,
        "parameter_initialization": _INITIALIZATIONS,
    }
    for field, allowed in enums.items():
        if field in options and options[field] not in allowed:
            _fail("options.%s must be one of: %s." % (field, ", ".join(sorted(allowed))), "options.%s" % field)
    if "iterations" in options and (
        not isinstance(options["iterations"], int)
        or isinstance(options["iterations"], bool)
        or not 1 <= options["iterations"] <= 10000
    ):
        _fail("options.iterations must be an integer from 1 to 10000.", "options.iterations")
    if "learning_rate" in options and (not _is_number(options["learning_rate"]) or options["learning_rate"] <= 0):
        _fail("options.learning_rate must be a finite number greater than zero.", "options.learning_rate")
    if "parameter_regularization" in options and (
        not _is_number(options["parameter_regularization"])
        or options["parameter_regularization"] < 0
    ):
        _fail("options.parameter_regularization must be a finite number greater than or equal to zero.", "options.parameter_regularization")
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
    return value


def ok_result(
    request_id: str,
    backend: str,
    elapsed_ms: float,
    parameters: list,
    metrics: Optional[dict] = None,
    warnings: Optional[list] = None,
) -> dict:
    return {
        "protocol_version": PROTOCOL_VERSION,
        "type": "analysis_result",
        "request_id": request_id,
        "status": "ok",
        "backend": backend,
        "elapsed_ms": round(elapsed_ms, 3),
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
        "protocol_version": PROTOCOL_VERSION,
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
