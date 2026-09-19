"""Persistent single-request worker using an atomic file spool."""

import argparse
import json
import os
from pathlib import Path
import time
from typing import Callable, Optional, Tuple

from .progress import AnalysisCancelled, ProgressFileWriter
from .service import handle


Analyzer = Callable[[object, str, Optional[Callable]], Tuple[dict, str]]


def _write_json(destination: Path, value: object) -> None:
    temporary = destination.with_name(destination.name + ".tmp")
    temporary.write_text(json.dumps(value, separators=(",", ":")), encoding="utf-8")
    os.replace(temporary, destination)


def _stop_requested(directory: Path, request_id: str) -> bool:
    marker = directory / "stop.json"
    try:
        value = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return isinstance(value, dict) and value.get("request_id") == request_id


def _remove_matching_stop(directory: Path, request_id: str) -> None:
    marker = directory / "stop.json"
    if not _stop_requested(directory, request_id):
        return
    try:
        marker.unlink()
    except OSError:
        pass


class SpoolProgress:
    """Publishes progress and turns a matching stop marker into cancellation."""

    def __init__(self, directory: Path, request_id: str, chain_order) -> None:
        self.directory = directory
        self.request_id = request_id
        self.writer = ProgressFileWriter(directory / "progress.json", request_id, chain_order)

    def __call__(self, phase, step, total_steps, parameters=None) -> None:
        self.writer(phase, step, total_steps, parameters)
        if phase != "finalizing":
            if _stop_requested(self.directory, self.request_id):
                raise AnalysisCancelled()
            if (self.directory / "request.json").exists():
                raise AnalysisCancelled()


def process_request(
    request: object,
    directory: Path,
    default_backend: str = "clap",
    analyzer: Analyzer = handle,
) -> None:
    request_id = request.get("request_id") if isinstance(request, dict) else None
    chain = request.get("chain", []) if isinstance(request, dict) else []
    chain_order = [item.get("instance_id") for item in chain if isinstance(item, dict)]
    progress = SpoolProgress(directory, request_id, chain_order) if isinstance(request_id, str) else None
    try:
        if progress is not None:
            options = request.get("options", {})
            total_steps = options.get("iterations", 100) if isinstance(options, dict) else 100
            progress("preparing", 0, total_steps)
        result, _diagnostics = analyzer(request, default_backend, progress)
        if isinstance(request_id, str) and _stop_requested(directory, request_id):
            raise AnalysisCancelled()
    except AnalysisCancelled:
        result = {
            "type": "analysis_cancelled",
            "request_id": request_id,
        }
    if isinstance(request_id, str):
        _remove_matching_stop(directory, request_id)
    _write_json(directory / "result.json", result)


def run_spool(
    directory: Path,
    default_backend: str = "clap",
    poll_seconds: float = 0.05,
    analyzer: Analyzer = handle,
) -> int:
    directory.mkdir(parents=True, exist_ok=True)
    processed = 0
    request_file = directory / "request.json"
    while not (directory / "shutdown").exists():
        try:
            request = json.loads(request_file.read_text(encoding="utf-8"))
        except FileNotFoundError:
            time.sleep(poll_seconds)
            continue
        except (OSError, ValueError):
            time.sleep(poll_seconds)
            continue

        try:
            request_file.unlink()
        except OSError:
            pass
        process_request(request, directory, default_backend, analyzer)
        processed += 1
    return processed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the SemantiFX development spool worker.")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--default-backend", choices=("mock", "clap"), default="clap")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    run_spool(args.directory, args.default_backend)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
