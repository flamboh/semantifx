import time
from typing import Optional, Tuple

from .adapters.mock import MockAdapter
from .adapters.text2fx import Text2FXAdapter
from .errors import SemantiFXError
from .protocol import error_result, ok_result, validate_request


def _adapter(name: str):
    if name == "mock":
        return MockAdapter()
    if name == "text2fx":
        return Text2FXAdapter()
    raise SemantiFXError(
        "backend_unavailable",
        "Unknown inference backend: %s." % name,
        ["Use 'mock' or 'text2fx'."],
    )


def analyze(value: object, default_backend: str = "mock") -> Tuple[dict, str]:
    started = time.perf_counter()
    request_id = value.get("request_id") if isinstance(value, dict) and isinstance(value.get("request_id"), str) else None
    backend: Optional[str] = None
    diagnostics = ""
    try:
        request = validate_request(value)
        backend = request.get("options", {}).get("backend", default_backend)
        adapter = _adapter(backend)
        parameters, metrics, warnings, diagnostics = adapter.analyze(request)
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        return ok_result(request["request_id"], backend, elapsed_ms, parameters, metrics, warnings), diagnostics
    except SemantiFXError as exc:
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        return error_result(exc, request_id, backend, elapsed_ms), diagnostics
    except Exception as exc:  # Last-resort containment: never crash a DAW-owned worker.
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        error = SemantiFXError(
            "internal_error",
            "Unexpected inference worker failure: %s" % exc,
            ["Restart the worker and retain this response for diagnosis."],
            {"exception_type": type(exc).__name__},
        )
        return error_result(error, request_id, backend, elapsed_ms), diagnostics

