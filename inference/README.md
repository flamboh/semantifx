# SemantiFX inference worker

This directory contains the local JSONL chain-optimization worker described by
[`../protocol`](../protocol). The current protocol-v2 slice is a six-band EQ
and SemantiFX's distortion in either order. The worker has no runtime dependencies
for the protocol, CLI, or deterministic mock backend. The real `clap` backend
uses the sibling Text2FX checkout for MS-CLAP, audio loading, and runtime
dependencies while keeping the effect chain itself in SemantiFX.

## Run without installing

From this directory:

```sh
PYTHONPATH=src python -m semantifx_inference.cli analyze \
  --backend mock \
  --audio /tmp/capture.wav \
  --prompt "warm, but keep it clear"
```

Pass `--progress-file /tmp/semantifx-progress.json` to atomically publish the
latest phase, optimizer step, total step count, and intermediate parameters.
Progress is advisory and never replaces the final protocol result on stdout.

Run a persistent worker and send one JSON request per line:

```sh
PYTHONPATH=src python -m semantifx_inference.worker --default-backend mock \
  < ../protocol/examples/analyze-request.jsonl
```

Stdout is reserved for JSONL responses. Model/runtime diagnostics and progress
bars are captured so they cannot corrupt the stream. Use `--diagnostics` to
copy captured diagnostics to stderr after each response.

## Install the lightweight worker

```sh
uv venv --python 3.9 .venv
uv pip install --python .venv/bin/python -e .
.venv/bin/semantifx-worker --default-backend mock
```

## Run the real CLAP optimizer

If Text2FX already has a working environment, invoke this package with that
interpreter while adding this package's source directory:

```sh
PYTHONPATH=src ../../oss/text2fx/.venv/bin/python -m semantifx_inference.cli analyze \
  --backend clap \
  --audio ../../oss/text2fx/assets/guitar.wav \
  --prompt "warm and lightly saturated" \
  --effect-order distortion-eq \
  --iterations 10
```

The default sibling checkout is resolved as `../../oss/text2fx` relative to
this repository. Override it with `SEMANTIFX_TEXT2FX_REPO=/absolute/path`.
Model caches remain controlled by Text2FX and its ML libraries; do not place
them in this repository.

The CLAP adapter builds SemantiFX's differentiable EQ + distortion chain in the
submitted order and optimizes its normalized physical parameters directly. It
does not use
Text2FX's effect-chain implementation or its peak normalization. The capture's
PCM level is preserved through mono conversion and resampling because hidden
normalization would erase the level-dependent behavior that makes distortion
useful.

A persistent worker reuses each loaded CLAP model within its process. Result
metrics report `model_cache_hit` and `model_cache_miss`; terminating the worker
releases the model. Optional `options.random_seed` makes cold and warm
optimization requests start from the same random state.

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

Protocol v2 accepts exactly `eq-1` and `distortion-1` in either order, including
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

## Test

```sh
PYTHONPATH=src python -m unittest discover -s tests -v
```
