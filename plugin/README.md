# Semantifx JUCE plugin

This directory contains the first runnable EQ-only plugin slice. It builds a
Standalone application plus AU and VST3 plug-ins on macOS. JUCE is fetched at
the pinned `8.0.8` tag during configuration.

## Build and run

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --target Semantifx_Standalone -j 4
open plugin/build/Semantifx_artefacts/Debug/Standalone/Semantifx.app
```

Build all plug-in formats and run the lightweight DSP-math checks with:

```sh
cmake --build plugin/build -j 4
ctest --test-dir plugin/build --output-on-failure
```

Release artifacts are produced by configuring a separate build directory with
`-DCMAKE_BUILD_TYPE=Release`. Plugin copying is deliberately disabled so local
Audio Unit and VST3 directories are not mutated by an ordinary build.

## Parameters

- Low and high shelf gain, frequency, and Q
- Four bell bands, each with gain, frequency, and Q
- Output trim and wet/dry mix
- Smooth bypass
- Protocol-v1 analysis-result JSON import

The 18 EQ parameters and their ranges mirror Text2FX/dasp-pytorch's
`ParametricEQ`, allowing optimized values to transfer without a mapping layer.
All controls are host-automatable and serialized in plugin state. Filter
coefficients are recalculated at a fixed control interval, while parameters,
output gain, mix, and bypass transitions are smoothed. The audio callback does
not allocate or lock.

The editor's **Import Analysis…** button accepts a complete successful
protocol-v1 result, validates all 18 named EQ values and units, clamps values
to the plugin's published parameter ranges, and notifies the host of each
change. Import and parsing run on the UI thread, never the audio thread.

## Development-only capture and analysis

The editor also contains a deliberately prominent **DEVELOPMENT ONLY** panel
for testing the complete local interaction:

1. Enter a prompt and click **Capture 10s** while audio is passing through.
2. After capture reaches 100%, click **Analyze**.
3. The plugin writes a temporary WAV, submits a 600-iteration request to a
   persistent local worker, validates its protocol-v1 JSON, and applies the
   result.
4. Before the optimizer has a valid candidate, **Cancel** discards the request.
   After the first candidate, it becomes **Use current** and applies the latest
   visible curve after interrupting the optimizer. Escape always cancels and
   discards.

While inference runs, the status strip reports preparation, model loading,
optimizer steps, elapsed time, and an estimated time remaining. A progress
sidecar publishes intermediate parameter snapshots atomically; the graph draws
them as a preview without changing audible or host-automated parameters until
the final result has been validated.

Capture storage is allocated by `prepareToPlay`; the audio callback only makes
bounded copies into that storage and publishes atomic state/progress. WAV I/O,
worker launch, waiting, and JSON file reads happen on a dedicated background
thread. Result application is posted back to JUCE's message thread. The worker
stays alive until the plugin instance is destroyed, so later requests reuse its
loaded CLAP model. Interruption is cooperative at optimizer progress callbacks.
If model loading or another non-cooperative phase does not stop within three
seconds, the plugin terminates that worker and lazily starts a new one for the
next request; this intentionally sacrifices the cache to keep the UI responsive.

This is prototype infrastructure, not a distribution design. CMake embeds the
local repository root as `SEMANTIFX_DEVELOPMENT_REPO_PATH`, so the resulting
binary expects this checkout's `scripts/worker` to remain at that path. The
script in turn expects the sibling Text2FX environment described in the root
project documentation. Each plugin instance currently owns one worker; a release
plugin must replace this checkout-specific arrangement with a packaged shared
worker/service.

On macOS, the standalone target declares why it needs microphone access. The
first launch should display a system permission prompt; choose **Allow**, then
use **Options** to select the intended input and output devices. AU and VST3
instances receive audio from their host and do not request microphone access
themselves.
