import argparse
import json
from pathlib import Path
import sys
import uuid

from .catalogue import default_chain
from .progress import ProgressFileWriter
from .service import analyze


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="SemantiFX local inference commands.",
        epilog="Analyze accepts --chain TYPES or --chain-file PATH.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    analyze_parser = subparsers.add_parser(
        "analyze", help="Analyze one audio file and print a versioned chain result."
    )
    analyze_parser.add_argument("--backend", choices=("mock", "clap"), default="clap")
    analyze_parser.add_argument("--audio", required=True)
    analyze_parser.add_argument("--prompt", required=True)
    analyze_parser.add_argument("--request-id", default=None)
    analyze_parser.add_argument("--iterations", type=int, default=100)
    analyze_parser.add_argument("--model", choices=("ms_clap", "laion_clap"), default="ms_clap")
    analyze_parser.add_argument("--criterion", choices=("cosine-sim", "directional_loss", "standard"), default="cosine-sim")
    analyze_parser.add_argument("--learning-rate", type=float, default=0.01)
    analyze_parser.add_argument(
        "--parameter-initialization",
        choices=("current", "random"),
        default="current",
    )
    analyze_parser.add_argument("--parameter-regularization", type=float, default=0.0)
    analyze_parser.add_argument("--roll-samples", type=int, default=None)
    analyze_parser.add_argument("--random-seed", type=int, default=0)
    chain_group = analyze_parser.add_mutually_exclusive_group()
    chain_group.add_argument(
        "--chain",
        default="eq,distortion",
        help="Comma-separated effect types in chain order.",
    )
    chain_group.add_argument(
        "--chain-file",
        type=Path,
        help="JSON file containing a chain list.",
    )
    analyze_parser.add_argument("--progress-file", default=None)
    analyze_parser.add_argument("--diagnostics", action="store_true")
    analyze_parser.add_argument(
        "--deterministic-metadata",
        action="store_true",
        help="Write zero elapsed time so seeded result files can be compared byte-for-byte.",
    )
    return parser


def _chain_from_args(args) -> list[dict]:
    if args.chain_file is not None:
        try:
            value = json.loads(args.chain_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError("Could not read --chain-file: %s" % exc) from exc
        if not isinstance(value, list):
            raise ValueError("--chain-file must contain a JSON chain list.")
        return value
    types = [item.strip() for item in args.chain.split(",") if item.strip()]
    return default_chain(types)


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        chain = _chain_from_args(args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    options = {
        "backend": args.backend,
        "iterations": args.iterations,
        "model": args.model,
        "criterion": args.criterion,
        "learning_rate": args.learning_rate,
        "parameter_initialization": args.parameter_initialization,
        "parameter_regularization": args.parameter_regularization,
    }
    if args.roll_samples is not None:
        options["roll_samples"] = args.roll_samples
    if args.random_seed is not None:
        options["random_seed"] = args.random_seed
    request = {
        "type": "analyze",
        "request_id": args.request_id or str(uuid.uuid4()),
        "prompt": args.prompt,
        "audio": {"path": args.audio},
        "chain": chain,
        "options": options,
    }
    progress = (
        ProgressFileWriter(
            Path(args.progress_file),
            request["request_id"],
            [item["instance_id"] for item in request["chain"]],
        )
        if args.progress_file
        else None
    )
    if progress is not None:
        progress("preparing", 0, args.iterations)
    result, diagnostics = analyze(request, args.backend, progress)
    if args.deterministic_metadata:
        result["elapsed_ms"] = 0.0
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if args.diagnostics and diagnostics:
        print(diagnostics, file=sys.stderr)
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
