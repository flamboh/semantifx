# SemantiFX

SemantiFX explores a musician-centered way to control familiar audio effects
with natural language. A musician captures a representative excerpt, describes
the change they want, previews the resulting effect settings, and remains free
to edit, compare, refine, or reject them.

This repository is at the **prototype** stage. The current question is:

> Does an asynchronous capture → describe → analyze → compare workflow feel
> compatible with music-making, and which interface makes the musician feel
> most in control?

## Current scope

- Parametric EQ only
- Local inference
- macOS Standalone, Audio Unit, and VST3 targets
- Text input before integrated speech input
- Visible, editable parameters with compare and undo

The system is intentionally split at a narrow seam: JUCE owns deterministic
real-time audio processing; the inference worker accepts an audio excerpt and a
prompt, then returns a versioned set of named parameters. ML work never runs on
the audio thread.

See [Architecture](docs/architecture.md) and [Initial research plan](docs/research-plan.md).

Runnable commands will be filled in as the parallel prototypes land.

