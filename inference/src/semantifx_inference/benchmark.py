import argparse
import csv
import hashlib
import itertools
import json
from pathlib import Path
import sys
import time
from typing import Any, Dict, Iterable, List, Optional

from .chain_schema import DISTORTION_THEN_EQ, EQ_THEN_DISTORTION, default_chain
from .errors import SemantiFXError
from .protocol import validate_request
from .service import analyze


BENCHMARK_VERSION = "1"
_MATRIX_KEYS = (
    "backend",
    "effect_order",
    "model",
    "criterion",
    "iterations",
    "learning_rate",
    "parameter_initialization",
    "parameter_regularization",
    "roll_samples",
)
_CSV_FIELDS = (
    "benchmark_version",
    "manifest_name",
    "sequence",
    "request_id",
    "audio_id",
    "audio_path",
    "prompt_id",
    "prompt",
    "repetition",
    "random_seed",
    "backend",
    "chain_order_json",
    "status",
    "elapsed_ms",
    "wall_elapsed_ms",
    "parameter_count",
    "config_json",
    "parameters_json",
    "metrics_json",
    "warnings_json",
    "error_code",
    "error_message",
    "diagnostics_characters",
)


class BenchmarkError(ValueError):
    pass


def _require_nonempty_list(manifest: dict, name: str) -> list:
    value = manifest.get(name)
    if not isinstance(value, list) or not value:
        raise BenchmarkError("%s must be a non-empty array." % name)
    return value


def load_manifest(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as source:
            manifest = json.load(source)
    except (OSError, json.JSONDecodeError) as exc:
        raise BenchmarkError("Could not read benchmark manifest %s: %s" % (path, exc)) from exc
    if not isinstance(manifest, dict):
        raise BenchmarkError("Benchmark manifest must be a JSON object.")
    allowed_fields = {"manifest_version", "name", "repetitions", "seed", "audio", "prompts", "matrix"}
    unknown_fields = sorted(set(manifest) - allowed_fields)
    if unknown_fields:
        raise BenchmarkError("Unknown manifest field(s): %s" % ", ".join(unknown_fields))
    if manifest.get("manifest_version") != "1":
        raise BenchmarkError("manifest_version must be '1'.")
    if not isinstance(manifest.get("name"), str) or not manifest["name"]:
        raise BenchmarkError("name must be a non-empty string.")

    audio_items = _require_nonempty_list(manifest, "audio")
    prompt_items = _require_nonempty_list(manifest, "prompts")
    for index, audio in enumerate(audio_items):
        if not isinstance(audio, dict) or set(audio) != {"id", "path"}:
            raise BenchmarkError("audio[%d] must contain exactly id and path." % index)
        if not all(isinstance(audio[field], str) and audio[field] for field in ("id", "path")):
            raise BenchmarkError("audio[%d] id and path must be non-empty strings." % index)
    for index, prompt in enumerate(prompt_items):
        if not isinstance(prompt, dict) or set(prompt) != {"id", "text"}:
            raise BenchmarkError("prompts[%d] must contain exactly id and text." % index)
        if not all(isinstance(prompt[field], str) and prompt[field] for field in ("id", "text")):
            raise BenchmarkError("prompts[%d] id and text must be non-empty strings." % index)
    if len({audio["id"] for audio in audio_items}) != len(audio_items):
        raise BenchmarkError("audio ids must be unique.")
    if len({prompt["id"] for prompt in prompt_items}) != len(prompt_items):
        raise BenchmarkError("prompt ids must be unique.")

    matrix = manifest.get("matrix")
    if not isinstance(matrix, dict) or not matrix:
        raise BenchmarkError("matrix must be a non-empty object of option arrays.")
    unknown = sorted(set(matrix) - set(_MATRIX_KEYS))
    if unknown:
        raise BenchmarkError("Unknown matrix option(s): %s" % ", ".join(unknown))
    if "backend" not in matrix:
        raise BenchmarkError("matrix.backend is required.")
    for key, values in matrix.items():
        if not isinstance(values, list) or not values:
            raise BenchmarkError("matrix.%s must be a non-empty array." % key)
    for config in expand_matrix(matrix):
        order = DISTORTION_THEN_EQ if config.get("effect_order") == "distortion-eq" else EQ_THEN_DISTORTION
        validation_request = {
            "type": "analyze",
            "request_id": "benchmark-manifest-validation",
            "prompt": "validation",
            "audio": {"path": "validation.wav"},
            "chain": default_chain(order),
            "options": {key: item for key, item in config.items() if key != "effect_order"},
        }
        try:
            validate_request(validation_request)
        except SemantiFXError as exc:
            raise BenchmarkError("Invalid matrix configuration: %s" % exc.message) from exc

    repetitions = manifest.get("repetitions", 1)
    if not isinstance(repetitions, int) or isinstance(repetitions, bool) or repetitions < 1:
        raise BenchmarkError("repetitions must be a positive integer.")
    seed = manifest.get("seed", 0)
    if not isinstance(seed, int) or isinstance(seed, bool) or not 0 <= seed <= 4294967295:
        raise BenchmarkError("seed must be an integer from 0 to 4294967295.")
    return manifest


def expand_matrix(matrix: dict) -> List[dict]:
    keys = [key for key in _MATRIX_KEYS if key in matrix]
    return [dict(zip(keys, values)) for values in itertools.product(*(matrix[key] for key in keys))]


def _case_seed(base_seed: int, audio_id: str, prompt_id: str, config: dict) -> int:
    identity = json.dumps(
        {"audio_id": audio_id, "prompt_id": prompt_id, "config": config},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    offset = int.from_bytes(hashlib.sha256(identity).digest()[:4], "big")
    return (base_seed + offset) % 4294967296


def _records(
    manifest: dict,
    manifest_path: Path,
    backend_filter: Optional[str],
    repetitions_override: Optional[int],
    limit: Optional[int],
) -> Iterable[dict]:
    configurations = expand_matrix(manifest["matrix"])
    if backend_filter is not None:
        configurations = [config for config in configurations if config.get("backend") == backend_filter]
    if not configurations:
        raise BenchmarkError("No matrix configurations match the selected backend.")

    repetitions = repetitions_override or manifest.get("repetitions", 1)
    sequence = 0
    manifest_dir = manifest_path.resolve().parent
    for audio, prompt, config, repetition in itertools.product(
        manifest["audio"], manifest["prompts"], configurations, range(1, repetitions + 1)
    ):
        if limit is not None and sequence >= limit:
            return
        sequence += 1
        audio_path = Path(audio["path"]).expanduser()
        if not audio_path.is_absolute():
            audio_path = (manifest_dir / audio_path).resolve()
        request_id = "benchmark-%04d-%s-%s-r%d" % (sequence, audio["id"], prompt["id"], repetition)
        order = DISTORTION_THEN_EQ if config.get("effect_order") == "distortion-eq" else EQ_THEN_DISTORTION
        options = {key: item for key, item in config.items() if key != "effect_order"}
        request = {
            "type": "analyze",
            "request_id": request_id,
            "prompt": prompt["text"],
            "audio": {"path": str(audio_path)},
            "chain": default_chain(order),
            "options": options,
        }
        random_seed = _case_seed(manifest.get("seed", 0), audio["id"], prompt["id"], config)
        request["options"]["random_seed"] = random_seed
        started = time.perf_counter()
        result, diagnostics = analyze(request, config["backend"])
        wall_elapsed_ms = (time.perf_counter() - started) * 1000.0
        yield {
            "benchmark_version": BENCHMARK_VERSION,
            "manifest_name": manifest.get("name", manifest_path.stem),
            "sequence": sequence,
            "request_id": request_id,
            "audio": {"id": audio["id"], "path": str(audio_path)},
            "prompt": {"id": prompt["id"], "text": prompt["text"]},
            "repetition": repetition,
            "random_seed": random_seed,
            "config": {**config, "random_seed": random_seed},
            "status": result["status"],
            "backend": result["backend"],
            "chain_order": result.get("chain_order", []),
            "elapsed_ms": result["elapsed_ms"],
            "wall_elapsed_ms": round(wall_elapsed_ms, 3),
            "parameters": result.get("parameters", []),
            "metrics": result.get("metrics", {}),
            "warnings": result.get("warnings", []),
            "error": result.get("error"),
            "diagnostics_characters": len(diagnostics),
        }


def _csv_row(record: dict) -> Dict[str, Any]:
    error = record.get("error") or {}
    return {
        "benchmark_version": record["benchmark_version"],
        "manifest_name": record["manifest_name"],
        "sequence": record["sequence"],
        "request_id": record["request_id"],
        "audio_id": record["audio"]["id"],
        "audio_path": record["audio"]["path"],
        "prompt_id": record["prompt"]["id"],
        "prompt": record["prompt"]["text"],
        "repetition": record["repetition"],
        "random_seed": record["random_seed"],
        "backend": record["backend"],
        "chain_order_json": json.dumps(record["chain_order"], separators=(",", ":")),
        "status": record["status"],
        "elapsed_ms": record["elapsed_ms"],
        "wall_elapsed_ms": record["wall_elapsed_ms"],
        "parameter_count": len(record["parameters"]),
        "config_json": json.dumps(record["config"], sort_keys=True, separators=(",", ":")),
        "parameters_json": json.dumps(record["parameters"], sort_keys=True, separators=(",", ":")),
        "metrics_json": json.dumps(record["metrics"], sort_keys=True, separators=(",", ":")),
        "warnings_json": json.dumps(record["warnings"], separators=(",", ":")),
        "error_code": error.get("code", ""),
        "error_message": error.get("message", ""),
        "diagnostics_characters": record["diagnostics_characters"],
    }


def run_benchmark(
    manifest_path: Path,
    jsonl_path: Path,
    csv_path: Path,
    backend_filter: Optional[str] = None,
    repetitions_override: Optional[int] = None,
    limit: Optional[int] = None,
) -> int:
    manifest_path = Path(manifest_path)
    manifest = load_manifest(manifest_path)
    jsonl_path = Path(jsonl_path)
    csv_path = Path(csv_path)
    jsonl_path.parent.mkdir(parents=True, exist_ok=True)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with jsonl_path.open("w", encoding="utf-8") as jsonl_output, csv_path.open(
        "w", encoding="utf-8", newline=""
    ) as csv_output:
        writer = csv.DictWriter(csv_output, fieldnames=_CSV_FIELDS)
        writer.writeheader()
        for record in _records(manifest, manifest_path, backend_filter, repetitions_override, limit):
            jsonl_output.write(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")
            writer.writerow(_csv_row(record))
            count += 1
    return count


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run a reproducible SemantiFX inference benchmark matrix.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--jsonl", type=Path, required=True, help="Output path for detailed JSONL records.")
    parser.add_argument("--csv", type=Path, required=True, help="Output path for the flat CSV summary.")
    parser.add_argument("--backend", choices=("mock", "clap"), default=None, help="Run only matching matrix rows.")
    parser.add_argument("--repetitions", type=int, default=None, help="Override manifest repetitions.")
    parser.add_argument("--limit", type=int, default=None, help="Run at most this many expanded cases.")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.repetitions is not None and args.repetitions < 1:
        print("--repetitions must be positive.", file=sys.stderr)
        return 2
    if args.limit is not None and args.limit < 1:
        print("--limit must be positive.", file=sys.stderr)
        return 2
    try:
        count = run_benchmark(
            args.manifest,
            args.jsonl,
            args.csv,
            args.backend,
            args.repetitions,
            args.limit,
        )
    except BenchmarkError as exc:
        print("Benchmark error: %s" % exc, file=sys.stderr)
        return 2
    print("Wrote %d benchmark record(s) to %s and %s." % (count, args.jsonl, args.csv), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
