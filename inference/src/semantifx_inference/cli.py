import argparse
import json
import sys
import uuid

from .service import analyze


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="SemantiFX local inference commands.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    analyze_parser = subparsers.add_parser("analyze", help="Analyze one audio file and print a v1 result.")
    analyze_parser.add_argument("--backend", choices=("mock", "text2fx"), default="mock")
    analyze_parser.add_argument("--audio", required=True)
    analyze_parser.add_argument("--prompt", required=True)
    analyze_parser.add_argument("--request-id", default=None)
    analyze_parser.add_argument("--iterations", type=int, default=100)
    analyze_parser.add_argument("--model", choices=("ms_clap", "laion_clap"), default="ms_clap")
    analyze_parser.add_argument("--criterion", choices=("cosine-sim", "directional_loss", "standard"), default="cosine-sim")
    analyze_parser.add_argument("--learning-rate", type=float, default=0.01)
    analyze_parser.add_argument("--parameter-initialization", choices=("zeros", "random", "super_random"), default="zeros")
    analyze_parser.add_argument("--parameter-regularization", type=float, default=0.0)
    analyze_parser.add_argument("--roll-samples", type=int, default=None)
    analyze_parser.add_argument("--random-seed", type=int, default=0)
    analyze_parser.add_argument("--diagnostics", action="store_true")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
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
        "protocol_version": "1",
        "type": "analyze",
        "request_id": args.request_id or str(uuid.uuid4()),
        "prompt": args.prompt,
        "audio": {"path": args.audio},
        "effects": ["eq"],
        "options": options,
    }
    result, diagnostics = analyze(request, args.backend)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if args.diagnostics and diagnostics:
        print(diagnostics, file=sys.stderr)
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
