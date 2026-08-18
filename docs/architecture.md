# Architecture

## Product boundary

SemantiFX optimizes a supplied effect chain; it does not initially assemble one.
The host or user chooses the effect instances and their order. SemantiFX searches
only their parameter space for a configuration that moves captured audio toward
a natural-language target.

```text
                       target prompt
                            │
captured excerpt ────────────┼──> chain optimizer
                            │          │
supplied effect chain ───────┘          │ parameter suggestion
                                       ▼
DAW audio ───────────────> JUCE effect chain ───────────────> audio output
```

This is an asynchronous suggestion workflow, not a continuously running ML
audio processor. CLAP evaluates rendered candidates during optimization; it
does not run on the real-time audio path.

## Modules and seams

### Audio effect host

The host-facing JUCE processor owns the instantiated effect chain, real-time
DSP, parameter automation, state persistence, and smoothing. Its
inference-facing seam accepts a complete parameter suggestion for that exact
chain. Applying a result must not allocate, block, launch a process, load a
model, or touch the filesystem on the audio thread.

### Chain optimizer

The optimizer has one external operation:

```text
optimize(audio, targetPrompt, effectChain) -> suggestion | error
```

The chain description must preserve order and give every effect instance a
stable identity, type, parameter schema, and current configuration. Stable
instance identity matters when a chain contains the same effect type more than
once.

The result contains a complete, instance-addressed parameter suggestion plus
algorithm metadata. Schema versions, names, ranges, units, cancellation
semantics, and structured errors cross the boundary. PyTorch tensors, CLAP
embeddings, gradients, and optimizer state do not.

The two adapters are a deterministic mock and the real CLAP adapter. The CLAP
adapter uses Text2FX's model and runtime dependencies but owns its
differentiable EQ + distortion chain inside SemantiFX, matching the selected
JUCE topology and parameter schema.

### Interaction surface

The editor owns capture state, the target prompt, optimization progress,
suggestion preview, comparison, and rejection. It presents the supplied chain
and the parameters changed by optimization. It never treats an embedding
objective as calibrated confidence, and the user retains manual control.

## Protocol evolution

Protocol v2 is the only supported protocol. It carries stable instance
identities, current physical parameter values, and exactly 22 named parameters
for the two-instance `eq-1` + `distortion-1` chain. Either order is supported
and echoed in results. Supporting arbitrary supplied chains still requires
effect-schema negotiation, repeated effect types, bypass state, and validation
of a result against the exact chain that produced it.

Chain assembly is a separate future operation. It may eventually select effect
types, counts, and order, but it must not be hidden inside chain optimization.

## DSP parity requirement

The CLAP worker optimizes through differentiable processors while playback uses
JUCE DSP. A parameter is portable only if both implementations agree on its range,
unit, topology, and audible response. Every supported effect type therefore
needs parity fixtures covering impulse or frequency responses where applicable
and rendered audio for identical configurations.

## Current constraints

- The shipped implementation is one EQ and one distortion in either order.
- Distortion is currently base-rate; oversampling and its latency contract are
  deferred, so high-drive aliasing remains a known limitation.
- Order changes switch topology at a block boundary without a dual-path
  crossfade.
- Inference runs outside the plugin process where practical.
- Temporary WAV/JSON, JSONL, and atomic spool files are acceptable during
  development.
- No inference operation may run on the audio callback.
- The optimizer may change parameters, but not effect instances or their order.
