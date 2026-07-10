# SemantiFX inference worker

This directory contains the local JSONL worker described by
[`../protocol`](../protocol). It has no runtime dependencies for the protocol,
CLI, or deterministic mock backend. The Text2FX backend loads the sibling
checkout and therefore needs Text2FX's Python 3.9 dependencies.

## Run without installing

From this directory:

```sh
PYTHONPATH=src python -m semantifx_inference.cli analyze \
  --backend mock \
  --audio /tmp/capture.wav \
  --prompt "warm, but keep it clear"
```

Run a persistent worker and send one JSON request per line:

```sh
PYTHONPATH=src python -m semantifx_inference.worker --default-backend mock \
  < ../protocol/examples/analyze-request.v1.jsonl
```

Stdout is reserved for JSONL responses. Diagnostics from Text2FX and progress
bars are captured so they cannot corrupt the stream. Use `--diagnostics` to
copy captured diagnostics to stderr after each response.

## Install the lightweight worker

```sh
uv venv --python 3.9 .venv
uv pip install --python .venv/bin/python -e .
.venv/bin/semantifx-worker --default-backend mock
```

## Use the sibling Text2FX checkout

If Text2FX already has a working environment, invoke this package with that
interpreter while adding this package's source directory:

```sh
PYTHONPATH=src ../../oss/text2fx/.venv/bin/python -m semantifx_inference.cli analyze \
  --backend text2fx \
  --audio ../../oss/text2fx/assets/guitar.wav \
  --prompt "warm" \
  --iterations 10
```

The default sibling checkout is resolved as `../../oss/text2fx` relative to
this repository. Override it with `SEMANTIFX_TEXT2FX_REPO=/absolute/path`.
Model caches remain controlled by Text2FX and its ML libraries; do not place
them in this repository.

## Test

```sh
PYTHONPATH=src python -m unittest discover -s tests -v
```
