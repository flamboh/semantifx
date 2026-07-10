# Initial benchmark baseline

Measured July 10, 2026 on the local arm64 Mac using the existing Python 3.9
Text2FX environment, MS-CLAP, its `guitar.wav` asset, parametric EQ, cosine
similarity, and neutral parameter initialization.

## Observations

- A one-shot process takes roughly 5–7 seconds for model startup plus work.
- A 50-iteration one-shot request completed in about 12 seconds end to end.
- The optimization loop sustained roughly 7 iterations per second in that run.
- In a persistent worker, two identical seeded one-iteration requests measured
  approximately 4.57 seconds cold and 0.21 seconds warm.
- A separate seeded benchmark invocation measured 5.32 seconds cold and 0.26
  seconds warm.
- Cold and warm seeded runs returned identical 18-parameter results.
- The worker emitted exactly one JSON response per request; model/progress
  diagnostics did not corrupt its stdout protocol.

These are feasibility measurements, not perceptual-quality claims. Loss varies
across randomly rolled excerpts and does not behave like a calibrated quality
or confidence score. Fifty iterations is therefore a useful quick-interaction
candidate to evaluate with musicians, not a proven optimum.

## Reproduce

From `inference/`, run the fast matrix first:

```sh
PYTHONPATH=src python -m semantifx_inference.benchmark \
  --manifest benchmarks/starter.json \
  --jsonl /tmp/semantifx-starter.jsonl \
  --csv /tmp/semantifx-starter.csv
```

Then exercise cold/warm Text2FX caching with the sibling environment:

```sh
PYTHONPATH=src ../../oss/text2fx/.venv/bin/python -m semantifx_inference.benchmark \
  --manifest benchmarks/text2fx-cache-smoke.json \
  --jsonl /tmp/semantifx-text2fx-cache.jsonl \
  --csv /tmp/semantifx-text2fx-cache.csv
```

The harness records configuration, stable seed, timing, status, metrics,
warnings, errors, and parameter values in JSONL and CSV. It never embeds audio.

