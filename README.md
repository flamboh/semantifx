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
scripts/analyze ../oss/text2fx/assets/guitar.wav "warm like a hug" 600
```

Build and open the JUCE standalone app:

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --config Debug --parallel 4
open plugin/build/Semantifx_artefacts/Debug/Standalone/Semantifx.app
```

To try the complete development flow, pass audio through the app or plugin,
enter a prompt, click **Listen 10s**, and then click **Shape sound**. The plugin
runs Text2FX outside the audio and UI threads and applies the validated result.
**Import Analysis…** can still load `/tmp/semantifx-analysis.json` directly.
The same build produces AU and VST3 artifacts. Run all automated checks with
`scripts/check`.

## Test in FL Studio

Use the VST3 build for FL Studio. This is a development build, so it expects
this repository and the sibling Text2FX environment to stay where they were
when you built it.

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
press Play. FL Studio's [effect-slot
guide](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_plugin.htm)
has more detail about Mixer routing.

### Shape the sound

1. Type what you want the sound to become.
2. Play a representative part of the track.
3. Click **Listen 10s** and keep playback running.
4. When capture finishes, click **Shape sound**.
5. Wait for the result. Once a preview appears, you can click **Use current**
   to stop early and apply the preview you see.

The first run can take longer because the worker has to load the model. Later
runs in the same plugin instance reuse it. Press Escape if you want to stop
without applying the preview.

### Rebuild after a code change

Close FL Studio, then run:

```sh
cmake --build plugin/build --target Semantifx_VST3 --config Debug --parallel 4
ditto plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  "$HOME/Library/Audio/Plug-Ins/VST3/Semantifx.vst3"
```

Open FL Studio again and load Semantifx. If it still shows the old build, scan
again with **Rescan previously verified plugins** turned on.

## Current integration edge

The real inference worker and real-time plugin now share a strict, versioned
18-parameter contract. The development build captures audio, keeps a local
inference worker alive across requests, reports model-loading and optimization
progress, and previews the evolving EQ curve. Once a preview exists, **Use
current** interrupts optimization and applies that visible curve; Escape cancels
and discards it. The worker deliberately uses an atomic file spool and embeds
this checkout's path. Packaging a shared worker and replacing repository-relative
paths remain distribution work.

See [the first measured baseline](docs/benchmark-baseline.md) and
[plugin validation](docs/plugin-validation.md).
