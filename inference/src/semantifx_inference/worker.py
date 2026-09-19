import argparse
import json
import sys
from typing import Iterable, TextIO

from .errors import SemantiFXError
from .protocol import error_result
from .service import handle


def run_stream(
    source: Iterable[str],
    destination: TextIO,
    default_backend: str = "mock",
    diagnostics_stream: TextIO = None,
) -> int:
    count = 0
    for line_number, line in enumerate(source, 1):
        if not line.strip():
            continue
        count += 1
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            error = SemantiFXError(
                "invalid_json",
                "Input line %d is not valid JSON: %s" % (line_number, exc.msg),
                ["Send one complete JSON object per line."],
                {"line": line_number, "column": exc.colno},
            )
            result = error_result(error, None, None, 0.0)
            diagnostics = ""
        else:
            result, diagnostics = handle(value, default_backend)
        destination.write(json.dumps(result, separators=(",", ":"), ensure_ascii=False) + "\n")
        destination.flush()
        if diagnostics_stream is not None and diagnostics:
            diagnostics_stream.write("[request %s]\n%s\n" % (result.get("request_id"), diagnostics.rstrip()))
            diagnostics_stream.flush()
    return count


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the SemantiFX JSONL inference worker.")
    parser.add_argument("--default-backend", choices=("mock", "clap"), default="mock")
    parser.add_argument("--diagnostics", action="store_true", help="Copy captured backend diagnostics to stderr.")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    diagnostics_stream = sys.stderr if args.diagnostics else None
    run_stream(sys.stdin, sys.stdout, args.default_backend, diagnostics_stream)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
