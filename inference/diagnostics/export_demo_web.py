#!/usr/bin/env python3
"""Copy a prompt_demo.py run into the landing page as MP3s plus web/src/demo.json."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import soundfile as sf

REPO = Path(__file__).resolve().parents[2]
AUDIO_DIR = REPO / "web" / "public" / "demo"
DATA_FILE = REPO / "web" / "src" / "demo.json"


def to_mp3(source: Path, destination: Path) -> None:
    audio, rate = sf.read(str(source), dtype="float32", always_2d=True)
    sf.write(str(destination), audio, rate, format="MP3")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path, help="prompt_demo.py output directory")
    parser.add_argument("--only", action="append", help="slugs to include, in order")
    args = parser.parse_args()

    summary = json.loads((args.run / "summary.json").read_text())
    results = {result["slug"]: result for result in summary["results"]}
    slugs = args.only or list(results)

    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    to_mp3(args.run / "dry.wav", AUDIO_DIR / "dry.mp3")
    entries = []
    for slug in slugs:
        result = results[slug]
        to_mp3(args.run / slug / "tuned_matched.wav", AUDIO_DIR / f"{slug}.mp3")
        entries.append({
            "prompt": result["prompt"],
            "audio": f"/demo/{slug}.mp3",
            "chain": result["chain"],
            "initialLoss": round(result["initial_loss"], 3),
            "finalLoss": round(result["final_loss"], 3),
        })

    DATA_FILE.write_text(json.dumps({"dry": "/demo/dry.mp3", "prompts": entries}, indent=2) + "\n")
    print(f"wrote {len(entries)} prompts to {DATA_FILE}")


if __name__ == "__main__":
    main()
