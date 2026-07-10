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
