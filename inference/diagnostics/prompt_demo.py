#!/usr/bin/env python3
"""Prompt-to-chain demo: assemble with Jev, tune with CLAP, render with the plugin DSP.

For each prompt this runs the same two requests the plugin sends
(``assemble`` then ``analyze``), renders the tuned chain through the
JUCE-free C++ ``semantifx_render`` tool, and writes:

- ``<out>/<slug>/tuned.wav`` -- the chain the plugin would play
- ``<out>/<slug>/tuned_matched.wav`` -- same, RMS-matched to the dry input
- ``<out>/dry.wav`` -- the untouched input
- ``<out>/summary.json`` -- chain, settings and CLAP loss per prompt

Run with the inference venv. ``--regularization`` pulls settings toward the
assembled defaults so results stay closer to minimal edits.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import render_gallery  # noqa: E402

PYTHON = REPO / "inference" / ".venv" / "bin" / "python"
DEFAULT_AUDIO = REPO / "web" / "public" / "original-guitar.wav"
DEFAULT_PROMPTS = [
    "warm and roomy, like a small jazz club",
    "an old telephone",
    "crunchy and aggressive",
    "bright and airy",
    "lo-fi, like a worn cassette tape",
    "huge and spacious, like a cathedral",
]


def slugify(prompt: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", prompt.lower()).strip("-")[:48]


def cli(*args: str, threads: int) -> dict:
    env = dict(os.environ, PYTHONPATH=str(REPO / "inference" / "src"))
    env["OMP_NUM_THREADS"] = str(threads)
    env["MKL_NUM_THREADS"] = str(threads)
    completed = subprocess.run(
        [str(PYTHON), "-m", "semantifx_inference.cli", *args],
        cwd=REPO, env=env, capture_output=True, text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"cli {args[0]} failed:\n{completed.stderr[-2000:]}")
    return json.loads(completed.stdout)


def rms(audio: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(audio, dtype=np.float64))))


def run_prompt(prompt: str, audio: Path, out_dir: Path, args: argparse.Namespace) -> dict:
    slug = slugify(prompt)
    prompt_dir = out_dir / slug
    prompt_dir.mkdir(parents=True, exist_ok=True)

    assembly = cli("assemble", "--backend", args.planner, "--prompt", prompt, threads=args.threads)
    chain_file = prompt_dir / "assembled_chain.json"
    chain_file.write_text(json.dumps(assembly["chain"], indent=2))

    analysis = cli(
        "analyze", "--backend", "clap", "--audio", str(audio), "--prompt", prompt,
        "--chain-file", str(chain_file), "--iterations", str(args.iterations),
        "--random-seed", str(args.seed), "--parameter-initialization", "current",
        "--parameter-regularization", str(args.regularization),
        *(["--match-loudness"] if args.match_loudness else []),
        threads=args.threads,
    )
    (prompt_dir / "analysis.json").write_text(json.dumps(analysis, indent=2))

    tuned: dict[str, dict[str, float]] = {}
    effect_types: dict[str, str] = {}
    for parameter in analysis["parameters"]:
        instance = parameter["effect_instance_id"]
        effect_types[instance] = parameter["effect"]
        tuned.setdefault(instance, {})[parameter["name"]] = parameter["value"]
    chain = [(effect_types[instance], tuned[instance]) for instance in analysis["chain_order"]]

    destination = prompt_dir / "tuned.wav"
    render_gallery.render(audio, chain, destination)
    dry, rate = sf.read(str(audio), dtype="float32", always_2d=True)
    wet, _ = sf.read(str(destination), dtype="float32", always_2d=True)
    gain = rms(dry) / max(rms(wet), 1e-9)
    matched = wet * gain
    peak = float(np.max(np.abs(matched)))
    if peak > 0.99:
        matched *= 0.99 / peak
    sf.write(str(prompt_dir / "tuned_matched.wav"), matched, rate, subtype="FLOAT")

    return {
        "prompt": prompt,
        "slug": slug,
        "chain": [{"effect": effect, "parameters": params} for effect, params in chain],
        "assembly_ms": assembly.get("elapsed_ms"),
        "analysis_ms": analysis.get("elapsed_ms"),
        "initial_loss": analysis["metrics"]["initial_loss"],
        "final_loss": analysis["metrics"]["final_loss"],
        "tuned_rms_dbfs": 20 * np.log10(max(rms(wet), 1e-12)),
        "tuned_peak_dbfs": 20 * np.log10(max(float(np.max(np.abs(wet))), 1e-12)),
        "match_gain_db": 20 * np.log10(gain),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--audio", type=Path, default=DEFAULT_AUDIO)
    parser.add_argument("--out", type=Path, default=Path("/tmp/sfx/demo"))
    parser.add_argument("--prompt", action="append", dest="prompts")
    parser.add_argument("--planner", choices=["jev", "mock"], default="jev")
    parser.add_argument("--iterations", type=int, default=300)
    parser.add_argument("--regularization", type=float, default=0.0)
    parser.add_argument("--match-loudness", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=14)
    args = parser.parse_args()

    prompts = args.prompts or DEFAULT_PROMPTS
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        render_gallery.ensure_tool()
    shutil.copyfile(args.audio, args.out / "dry.wav")

    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_prompt, p, args.audio, args.out, args): p for p in prompts}
        for future in concurrent.futures.as_completed(futures):
            prompt = futures[future]
            result = future.result()
            results[prompt] = result
            print(
                f"{prompt!r}: {' -> '.join(e['effect'] for e in result['chain'])} "
                f"loss {result['initial_loss']:.3f} -> {result['final_loss']:.3f} "
                f"peak {result['tuned_peak_dbfs']:.1f} dBFS",
                flush=True,
            )

    summary = {
        "audio": str(args.audio),
        "planner": args.planner,
        "iterations": args.iterations,
        "regularization": args.regularization,
        "match_loudness": args.match_loudness,
        "seed": args.seed,
        "results": [results[p] for p in prompts],
    }
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2))
    print(f"wrote {args.out / 'summary.json'}")


if __name__ == "__main__":
    main()
