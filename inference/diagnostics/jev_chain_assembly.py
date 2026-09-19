#!/usr/bin/env python3
"""Run the Jev chain-assembly diagnostic through the worker planner."""

import argparse
from dataclasses import asdict
import json
import os
import sys

from semantifx_inference.assembly import (
    CATALOGUE_DESCRIPTIONS,
    assemble,
    build_state,
    order_questions,
    selection_questions,
)
from semantifx_inference.env import load_env
from semantifx_inference.planner import JevPlanner


EVAL_PROMPTS = [
    "warm and lightly saturated",
    "brighter with more presence",
    "heavy fuzz",
    "tame the boominess but keep it clean",
    "sounds good as it is",
    "aggressive, gritty, then cut the harsh top end",
]


def _assemble_prompt(planner: JevPlanner, prompt: str) -> dict:
    chain, judgments, warnings = assemble(prompt, planner)
    return {
        "chain": chain,
        "judgments": asdict(judgments),
        "warnings": warnings,
    }


def _dry_run(prompt: str) -> None:
    state = build_state(prompt, CATALOGUE_DESCRIPTIONS)
    print(
        json.dumps(
            {
                "selection": {
                    "state": state,
                    "questions": selection_questions(CATALOGUE_DESCRIPTIONS),
                },
                "order": {
                    "state": state,
                    "questions": order_questions(list(CATALOGUE_DESCRIPTIONS)),
                },
            },
            indent=2,
        )
    )


def _print_eval(planner: JevPlanner) -> None:
    print("prompt | selection | no_change | resulting chain ids")
    for prompt in EVAL_PROMPTS:
        result = _assemble_prompt(planner, prompt)
        judgments = result["judgments"]
        selection = ", ".join(
            f"{effect_type}={probability:.2f}"
            for effect_type, probability in judgments["selection"].items()
        )
        chain_ids = ",".join(item["instance_id"] for item in result["chain"])
        print(
            f"{prompt} | {selection} | {judgments['no_change']:.2f} | {chain_ids}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("prompt", nargs="?")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--eval", action="store_true", dest="run_eval")
    args = parser.parse_args()

    load_env()
    if args.run_eval and args.dry_run:
        parser.error("--dry-run cannot be combined with --eval")
    if args.run_eval:
        prompt = None
    elif args.prompt:
        prompt = args.prompt
    else:
        parser.error("PROMPT is required unless --eval is used")

    if args.dry_run:
        _dry_run(prompt)
        return 0

    api_key = os.environ.get("TYPESAFE_API_KEY")
    if not api_key:
        print("TYPESAFE_API_KEY is required for live Jev chain assembly.", file=sys.stderr)
        return 2

    planner = JevPlanner(api_key)
    if args.run_eval:
        _print_eval(planner)
    else:
        print(json.dumps(_assemble_prompt(planner, prompt), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
