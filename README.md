# SemantiFX

SemantiFX is a prompt-driven interface for general audio effects. Given a
representative audio excerpt, a natural-language target, and an existing effect
chain, it uses CLAP to optimize the chain's parameters toward the target. The
result remains an ordinary, visible, editable effect configuration.

Think of it as an Ozone-style modular workspace for general effects, with a
prompt describing the desired sound. SemantiFX does not initially decide which
effects to use or how to order them. The first question is narrower:

> Given a fixed effect chain, can CLAP find useful, editable parameter settings
> that move the input audio toward a natural-language target?

## Product direction

- General, ordered effect chains
- Chain supplied by the host or user
- CLAP-guided parameter optimization
- Local inference
- Inspectable, editable, automatable results
- Chain assembly deliberately deferred

## Current implementation

The working slice contains a six-band EQ and a SemantiFX-owned distortion in a
user-selectable order. CLAP optimizes all 22 effect parameters through the
selected differentiable topology, then the JUCE processor applies the same
configuration in real time. The
distortion exposes drive, character, tone, and parallel mix rather than a
single generic drive knob.

It builds as macOS Standalone, Audio Unit, and VST3 targets and supports
capture, text prompting, progress previews, interruption, manual editing,
bypass comparison, and state persistence. The protocol identifies each effect
instance and carries its current configuration; it is generated from the
effect definitions in `protocol/effects` by `scripts/gen`.

The system is split at a narrow seam: JUCE owns deterministic real-time audio
processing; the inference worker accepts an audio excerpt, target prompt, and
effect-chain description, then returns a versioned parameter suggestion. ML
work never runs on the audio thread.

See [Architecture](docs/architecture.md), [Domain language](CONTEXT.md), and
[Initial research plan](docs/research-plan.md).

## Try it

Create the inference environment, install its pinned dependencies, and run a
real CLAP optimization on a WAV file:

```sh
python3.12 -m venv inference/.venv
inference/.venv/bin/python -m pip install -e inference
scripts/analyze /path/to/guitar.wav "warm like a hug" 600
```

The first run downloads the MS-CLAP weights into the user's model cache.

Build and open the JUCE standalone app:

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --config Debug --parallel 4
open plugin/build/Semantifx_artefacts/Debug/Standalone/Semantifx.app
```

To try the complete development flow, pass audio through the app or plugin,
enter a prompt, click **Listen 10s**, and then click **Shape sound**. The plugin
runs the CLAP optimizer outside the audio and UI threads and applies the
validated result.
**Import Analysis…** can still load `/tmp/semantifx-analysis.json` directly.
The same build produces AU and VST3 artifacts. Run all automated checks with
`scripts/check`.

## Test in FL Studio

Use the VST3 build for FL Studio. This is a development build, so it expects
this repository and `inference/.venv` to stay where they were when you built
it.

### Build and install it

Close FL Studio first. From the root of this repository, run:

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --target Semantifx_VST3 --config Debug --parallel 4
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
ditto plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  "$HOME/Library/Audio/Plug-Ins/VST3/Semantifx.vst3"
```

The last command copies the new build into the normal VST3 folder on your Mac.

### Let FL Studio find it

1. Open FL Studio.
2. Go to **Options → Manage plugins**.
3. Turn on **Verify plugins**.
4. Click **Find installed plugins**.
5. Search for `Semantifx` when the scan finishes.

If you already scanned an older build, turn on **Rescan previously verified
plugins** before scanning. FL Studio also has a [plugin installation
guide](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)
if Semantifx does not appear.

### Put it on some audio

1. Open the Mixer and choose the insert used by your instrument or audio clip.
2. Click an empty effect slot.
3. Choose **More plugins**, find `Semantifx`, and open it.
4. Leave the FL Studio effect-slot mix at 100%. Use the **Mix** control inside
   Semantifx instead.

Semantifx is an effect, so audio must be moving through that Mixer insert. If
its meter is not moving, route an instrument or audio clip to the insert and
press Play.

### Rebuild after a code change

Close FL Studio, then run:

```sh
cmake --build plugin/build --target Semantifx_VST3 --config Debug --parallel 4
ditto plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  "$HOME/Library/Audio/Plug-Ins/VST3/Semantifx.vst3"
```

Open FL Studio again and load Semantifx. If it still shows the old build, scan
again with **Rescan previously verified plugins** turned on.
