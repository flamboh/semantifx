# SemantiFX inference worker

This directory contains the local JSONL chain-optimization worker described by
[`../protocol`](../protocol). The current protocol is a six-band EQ and
SemantiFX's distortion in either order. The real `clap` backend uses vendored
Text2FX runtime paths for MS-CLAP and audio loading while keeping the effect
chain itself in SemantiFX.

## Set up the environment

From the repository root, create the one inference environment and install the
compatible-release dependency pins:

```sh
python3.12 -m venv inference/.venv
inference/.venv/bin/python -m pip install -e inference
```

The first MS-CLAP model construction downloads its weights into the user's
Hugging Face cache. The environment and weights do not need an external source
checkout.

Run the deterministic mock backend from `inference/`:

```sh
.venv/bin/semantifx-inference analyze \
  --backend mock \
  --audio /tmp/capture.wav \
  --prompt "warm, but keep it clear"
```

Run a persistent worker and send one JSON request per line:

```sh
.venv/bin/semantifx-worker --default-backend mock \
  < ../protocol/examples/analyze-request.jsonl
```

Stdout is reserved for JSONL responses. Model/runtime diagnostics and progress
bars are captured so they cannot corrupt the stream. Use `--diagnostics` to
copy captured diagnostics to stderr after each response. Pass
`--progress-file /tmp/semantifx-progress.json` to publish the latest optimizer
step and intermediate parameters without changing the final stdout result.

## Run the real CLAP optimizer

From the repository root:

```sh
scripts/analyze /path/to/guitar.wav "warm and lightly saturated" 10
```

`scripts/analyze` writes `/tmp/semantifx-analysis.json` by default and gives each
run a UTC timestamped request ID. Set `SEMANTIFX_PYTHON` to override the
interpreter.

The CLAP adapter builds SemantiFX's differentiable EQ + distortion chain in the
submitted order and optimizes its normalized physical parameters directly. It
does not use Text2FX's effect-chain implementation or its peak normalization.
The capture's PCM level is preserved through mono conversion and resampling
because hidden normalization would erase the level-dependent behavior that
makes distortion useful.

A persistent worker reuses each loaded CLAP model within its process. Result
metrics report `model_cache_hit` and `model_cache_miss`; terminating the worker
releases the model. Optional `options.random_seed` makes cold and warm
optimization requests start from the same random state. Metrics also report
`initial_loss` and `final_loss`, the CLAP criterion evaluated on the unrolled
capture before and after optimization, so a run can be judged without listening.

The development plugin uses a private atomic-file adapter around that same
long-lived process:

```sh
scripts/worker /tmp/semantifx-worker-spool
```

It consumes one `request.json` at a time and publishes request-correlated
`progress.json` and `result.json` snapshots. A matching `stop.json` interrupts
at the next optimizer progress callback without unloading the model. This spool
interface is intentionally not part of the public protocol; it exists because
JUCE's child-process interface cannot write to a worker's stdin.

The protocol accepts exactly `eq-1` and `distortion-1` in either order, including
the current value of every parameter, and returns a complete instance-addressed
suggestion with the submitted `chain_order`. It is the only supported protocol.
Arbitrary effect sets are not implemented yet.

The distortion has four controls: `drive_db` (0–48 dB), `character` (a
continuous arctangent-to-tanh curve), `tone_db` (a fixed 3 kHz high shelf), and
`mix`. Both curve endpoints are peak normalized so drive is not a hidden output
gain. Processing currently runs at the session sample rate without
oversampling; high-drive aliasing is a known limitation and is reported in
result warnings.

## Benchmark

Manifest-driven mock and CLAP benchmark examples, including a two-request
model-cache smoke, are documented in [`benchmarks/README.md`](benchmarks/README.md).
The runner writes JSONL and CSV summaries and never copies audio into results.

## Chain assembly

Assembly uses two catalogue-driven TypeSafe Jev requests. The first selects
useful effects or an explicit no-change result. The second asks pairwise
ordering questions only when at least two effects were selected. The worker
returns catalogue-default parameters and the judgments used to assemble the
chain.

The optional Jev dependency is only needed for the `jev` backend. Install it
and set the API key in the environment or in `.env` at the repository root or
under `inference/`:

```sh
.venv/bin/python -m pip install -e ".[assembly]"
export TYPESAFE_API_KEY="your-key"
```

Run the worker-facing CLI with the deterministic mock backend:

```sh
.venv/bin/python -m semantifx_inference.cli assemble \
  --backend mock --prompt "warm and lightly saturated"
```

Inspect both Jev request payloads without a network call:

```sh
.venv/bin/python diagnostics/jev_chain_assembly.py \
  "warm and lightly saturated" --dry-run
```

Run the built-in six-prompt evaluation:

```sh
.venv/bin/python diagnostics/jev_chain_assembly.py x --eval
```

## Test

```sh
.venv/bin/python -m unittest discover -s tests -v
SEMANTIFX_RUN_MODEL_TESTS=1 .venv/bin/python -m unittest \
  discover -s tests -p 'test_golden_reproducibility.py' -v
```

The golden test forces CPU because the CLAP encoder's CUDA bicubic backward is
nondeterministic, and the optimizer amplifies its noise. CPU is bit-exact and
no slower for this fixture.

## Citation

SemantiFX vendors the executed runtime paths from Text2FX by Annie Chu, Patrick
O'Reilly, Julia Barnett, and Bryan Pardo, ICASSP 2025. Upstream:
<https://github.com/anniejchu/text2fx>. Source fork:
<https://github.com/flamboh/text2fx> at commit `7999b59`.

The code is vendored verbatim pending upstream licensing. Unused imports and
the unused `laion_clap` branch are dropped; `get_model` resolves the colocated
MS-CLAP wrapper on its marked `# SemantiFX:` line.

Stage 1 vendors the exact executed paths and locks their output with a golden
fixture. Stage 2 will trim and optimize them against that fixture.
