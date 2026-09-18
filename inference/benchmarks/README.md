# Inference benchmarks

The benchmark runner expands a small JSON configuration matrix, invokes the
same in-process service used by the JSONL worker, and writes one detailed JSONL
record plus one CSV row per run. Keeping runs in one process measures CLAP's
cold model load followed by warm-cache requests.

Audio files are referenced by path. Results contain configuration, timing,
status, metrics, errors, and numeric parameters, but never audio content.
Each case gets a stable seed derived from the manifest seed, asset, prompt, and
configuration. Repetitions of the same case reuse that seed.

## Fast mock benchmark

From inference:

    PYTHONPATH=src python -m semantifx_inference.benchmark       --manifest benchmarks/starter.json       --jsonl /tmp/semantifx-starter.jsonl       --csv /tmp/semantifx-starter.csv

## Real two-request cache smoke

Place guitar.wav and speech.wav in inference/benchmarks/audio/ first. The
CLAP smoke manifest repeats the same one-iteration request twice, so
metrics.model_cache_hit should be false then true.

    .venv/bin/python -m semantifx_inference.benchmark       --manifest benchmarks/clap-cache-smoke.json       --jsonl /tmp/semantifx-clap-cache.jsonl       --csv /tmp/semantifx-clap-cache.csv

Use --backend mock to filter mixed matrices, --repetitions N to override a
manifest, or --limit N for a bounded smoke. Paths in manifests are resolved
relative to the manifest file. A matrix chain value is a list of effect types,
for example ["eq", "distortion"] or ["distortion", "eq"].
