#!/usr/bin/env python3
"""Run the two-layer Jev chain-assembly prototype."""

import argparse
import json
import os
import sys
from pathlib import Path

from semantifx_inference.assembly import (
    CATALOGUE_DESCRIPTIONS,
    build_state,
    order_effects,
    order_questions,
    select_effects,
    selection_questions,
    to_chain,
)

DEFAULT_SOURCE = "a clean electric guitar recording"
EVAL_PROMPTS = [
    "warm and lightly saturated",
    "brighter with more presence",
    "heavy fuzz",
    "tame the boominess but keep it clean",
    "sounds good as it is",
    "aggressive, gritty, then cut the harsh top end",
]


def _load_env() -> None:
    """Load root then inference .env files without overriding the environment."""
    inference_dir = Path(__file__).resolve().parents[1]
    repo_dir = inference_dir.parent
    for path in (repo_dir / ".env", inference_dir / ".env"):
        if not path.is_file():
            continue
        for raw_line in path.read_text().splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip()
            if value[:1] in {"'", '"'} and value[-1:] == value[:1]:
                value = value[1:-1]
            os.environ.setdefault(key, value)


def _answer_dict(response) -> dict:
    return {
        question_id: {"noul": answer.noul}
        for question_id, answer in response.nouls.items()
    }


def _print_judgments(judgments: dict) -> None:
    for layer in ("selection", "order"):
        for question_id, answer in judgments.get(layer, {}).items():
            print(f"{layer}.{question_id}: probability={answer['noul']:.3f}", file=sys.stderr)


def _request(client, state: dict, questions: dict) -> dict:
    return _answer_dict(client.system_one(state=state, questions=questions))


def _assemble_live(client, prompt: str, source: str) -> dict:
    state = build_state(prompt, source, CATALOGUE_DESCRIPTIONS)
    selection_answers = _request(client, state, selection_questions(CATALOGUE_DESCRIPTIONS))
    selected = select_effects(selection_answers, CATALOGUE_DESCRIPTIONS)

    order_answers = {}
    ordered = list(selected)
    if len(selected) >= 2:
        order_answers = _request(client, state, order_questions(selected))
        ordered = order_effects(selected, order_answers)

    chain = to_chain(ordered)
    judgments = {"selection": selection_answers, "order": order_answers}
    _print_judgments(judgments)
    return {
        "selected": selected,
        "order": ordered,
        "chain": chain,
        "judgments": judgments,
    }


def _dry_run(prompt: str, source: str) -> None:
    state = build_state(prompt, source, CATALOGUE_DESCRIPTIONS)
    print(json.dumps({
        "selection": {"state": state, "questions": selection_questions(CATALOGUE_DESCRIPTIONS)},
        "order": {"state": state, "questions": order_questions(list(CATALOGUE_DESCRIPTIONS))},
    }, indent=2))


def _print_eval(client, source: str) -> None:
    print("prompt | p(eq) | p(dist) | p(no_change) | p(eq before dist) | resulting chain ids")
    for prompt in EVAL_PROMPTS:
        result = _assemble_live(client, prompt, source)
        selection = result["judgments"]["selection"]
        order = result["judgments"]["order"]
        order_probability = "-"
        if "before_eq_distortion" in order:
            order_probability = f"{order['before_eq_distortion']['noul']:.2f}"
        print(
            f"{prompt} | {selection.get('use_eq', {}).get('noul', 0.0):.2f} | "
            f"{selection.get('use_distortion', {}).get('noul', 0.0):.2f} | "
            f"{selection.get('no_change', {}).get('noul', 0.0):.2f} | "
            f"{order_probability} | {','.join(item['instance_id'] for item in result['chain'])}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("prompt", nargs="?")
    parser.add_argument("--source", default=DEFAULT_SOURCE)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--eval", action="store_true", dest="run_eval")
    args = parser.parse_args()

    _load_env()
    if args.run_eval and args.dry_run:
        parser.error("--dry-run cannot be combined with --eval")
    if args.run_eval:
        prompt = None
    elif args.prompt:
        prompt = args.prompt
    else:
        parser.error("PROMPT is required unless --eval is used")

    if args.dry_run:
        _dry_run(prompt, args.source)
        return 0

    if not os.environ.get("TYPESAFE_API_KEY"):
        print("TYPESAFE_API_KEY is required for live Jev chain assembly.", file=sys.stderr)
        return 2

    from typesafe_sdk import TypeSafeClient

    with TypeSafeClient() as client:
        if args.run_eval:
            _print_eval(client, args.source)
        else:
            print(json.dumps(_assemble_live(client, prompt, args.source), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
