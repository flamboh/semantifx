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

## Try it

Explore three interaction directions in a browser:

```sh
bun prototype/server.ts
```

Then open <http://localhost:4173/?variant=A>. The bottom switcher moves among
the guided studio, conversation, and workbench variants without losing the
simulated session state.

Run a real Text2FX analysis using the sibling checkout's existing environment:

```sh
scripts/analyze ../oss/text2fx/assets/guitar.wav "warm like a hug" 50
```

Build and open the JUCE standalone app:

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --config Debug --parallel 4
open plugin/build/Semantifx_artefacts/Debug/Standalone/Semantifx.app
```

Use **Import Analysis…** in the app to load `/tmp/semantifx-analysis.json`.
The same build also produces AU and VST3 artifacts. Run all automated checks
with `scripts/check`.

## Current integration edge

The real inference worker and real-time plugin now share a strict, versioned
18-parameter contract. Importing a result is intentionally manual in this
slice. Audio capture, automatic worker process management, cancellation, and
progress transport are the next vertical integration step.
