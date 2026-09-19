import os
import time
from dataclasses import asdict
from typing import Optional, Tuple

from .adapters.mock import MockAdapter
from .adapters.clap import CLAPAdapter
from .assembly import assemble as assemble_chain
from .env import load_env
from .errors import SemantiFXError
from .planner import JevPlanner, MockPlanner
from .progress import AnalysisCancelled, ProgressCallback
from .protocol import (
    error_result,
    ok_result,
    validate_analyze_request,
    validate_assemble_request,
)


def _adapter(name: str, progress: Optional[ProgressCallback] = None):
    if name == "mock":
        return MockAdapter(progress=progress)
    if name == "clap":
        return CLAPAdapter(progress=progress)
    raise SemantiFXError(
        "backend_unavailable",
        "Unknown inference backend: %s." % name,
        ["Use 'mock' or 'clap'."],
    )


def _assembly_unavailable(message: str, details: Optional[dict] = None) -> SemantiFXError:
    return SemantiFXError(
        "assembly_unavailable",
        message,
        [
            "Set TYPESAFE_API_KEY in the repository .env or worker environment.",
            "Retry with options.backend='mock', or retry later if Jev is unavailable.",
        ],
        details,
    )


def _analyze_request(
    value: object,
    default_backend: str,
    progress: Optional[ProgressCallback],
    started: float,
) -> Tuple[dict, str]:
    request = validate_analyze_request(value)
    backend = request.get("options", {}).get("backend", default_backend)
    adapter = _adapter(backend, progress)
    parameters, metrics, warnings, diagnostics = adapter.analyze(request)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    chain_order = [item["instance_id"] for item in request["chain"]]
    return ok_result(
        request["request_id"],
        backend,
        elapsed_ms,
        chain_order,
        parameters,
        metrics,
        warnings,
    ), diagnostics


def _assemble_request(
    value: object,
    progress: Optional[ProgressCallback],
    started: float,
) -> Tuple[dict, str]:
    request = validate_assemble_request(value)
    options = request.get("options", {})
    requested_backend = options.get("backend")
    load_env()
    if requested_backend is None:
        if not os.environ.get("TYPESAFE_API_KEY", "").strip():
            raise _assembly_unavailable(
                "No assembly backend was selected and TYPESAFE_API_KEY is not set.",
                {"field": "options.backend"},
            )
        backend = "jev"
    else:
        backend = requested_backend

    planner = MockPlanner() if backend == "mock" else JevPlanner(
        os.environ.get("TYPESAFE_API_KEY"),
        timeout_s=10.0,
    )
    chain, judgments, warnings = assemble_chain(request["prompt"], planner)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    return (
        {
            "type": "assembly_result",
            "request_id": request["request_id"],
            "status": "ok",
            "backend": backend,
            "elapsed_ms": round(elapsed_ms, 3),
            "chain": chain,
            "judgments": asdict(judgments),
            "warnings": warnings,
        },
        "",
    )


def handle(
    value: object,
    default_backend: str = "mock",
    progress: Optional[ProgressCallback] = None,
) -> Tuple[dict, str]:
    """Dispatch one validated protocol value to analysis or assembly."""
    started = time.perf_counter()
    request_id = (
        value.get("request_id")
        if isinstance(value, dict) and isinstance(value.get("request_id"), str)
        else None
    )
    result_type = (
        "assembly_result"
        if isinstance(value, dict) and value.get("type") == "assemble"
        else "analysis_result"
    )
    options = value.get("options", {}) if isinstance(value, dict) else {}
    backend = (
        options.get("backend")
        if isinstance(options, dict) and isinstance(options.get("backend"), str)
        else None
    )
    if backend is None and result_type == "analysis_result":
        backend = default_backend
    try:
        if isinstance(value, dict) and value.get("type") == "analyze":
            return _analyze_request(value, default_backend, progress, started)
        if isinstance(value, dict) and value.get("type") == "assemble":
            return _assemble_request(value, progress, started)
        raise SemantiFXError(
            "invalid_request",
            "Unknown request type.",
            ["Set type to 'analyze' or 'assemble'."],
            {"field": "type"},
        )
    except AnalysisCancelled:
        raise
    except SemantiFXError as exc:
        return error_result(
            exc,
            request_id,
            backend,
            (time.perf_counter() - started) * 1000.0,
            result_type,
        ), ""
    except Exception as exc:
        error = SemantiFXError(
            "internal_error",
            "Unexpected inference worker failure: %s" % exc,
            ["Restart the worker and retain this response for diagnosis."],
            {"exception_type": type(exc).__name__},
        )
        return error_result(
            error,
            request_id,
            backend,
            (time.perf_counter() - started) * 1000.0,
            result_type,
        ), ""


def analyze(
    value: object,
    default_backend: str = "mock",
    progress: Optional[ProgressCallback] = None,
) -> Tuple[dict, str]:
    """Compatibility wrapper for callers that only submit analyze requests."""
    return handle(value, default_backend, progress)
