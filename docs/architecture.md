# Architecture

## Decision

SemantiFX begins as an asynchronous parameter-suggestion instrument. It is not
a continuously running ML audio processor.

```text
DAW audio ──> JUCE real-time DSP ──> audio output
    │                  ▲
    │ captured excerpt │ smoothed parameter snapshot
    ▼                  │
inference worker ──────┘
    ▲
    │ prompt
plugin UI
```

## Modules and seams

### Audio effect module

The host-facing JUCE processor owns real-time DSP, parameter automation, state
persistence, and smoothing. Its inference-facing seam accepts a complete named
parameter snapshot. Applying a result must not allocate, block, launch a
process, load a model, or touch the filesystem on the audio thread.

### Inference module

The inference module has one external operation:

```text
analyze(AnalysisRequest) -> AnalysisResult | AnalysisError
```

Its interface includes schema versioning, parameter names and units, algorithm
metadata, cancellation semantics, and structured errors. PyTorch tensors,
CLAP embeddings, optimizer state, and Python class names are implementation
details and must not cross the seam.

The initial adapters are a deterministic mock and a local Text2FX Python
implementation. A second real adapter would make this seam durable; until then,
we keep it deliberately small rather than designing a general ML platform.

### Interaction module

The editor owns capture state, prompt state, analysis progress, result preview,
A/B comparison, refinement, undo, and rejection. It never treats an embedding
objective as calibrated confidence. The musician sees concrete effect changes
and retains manual control.

## DSP parity requirement

Text2FX optimizes through `dasp-pytorch`, while playback uses JUCE DSP. A named
parameter is only portable if both implementations agree on its range, unit,
topology, and audible response. Before treating inference output as valid, we
will compare impulse responses, frequency responses, and rendered fixtures for
identical settings.

## Prototype constraints

- Inference runs outside the plugin process where practical.
- Temporary WAV/JSON or JSONL process communication is acceptable initially.
- No inference operation may run on the audio callback.
- The UI prototype is throwaway and must be absorbed or deleted after its
  interaction question is answered.

