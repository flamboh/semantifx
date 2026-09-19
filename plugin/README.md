# SemantiFX JUCE plugin

The plugin exposes a fixed pool of two slots for each catalogue type: EQ,
distortion, compressor, and reverb. The saved `chain` state property stores an
ordered comma-separated subset of those slots, such as `eq-1,reverb-1`.
Hosts see every slot's parameters at construction time, including its bypass parameter.
Chain structure is saved state, and solo is a transient UI control rather than a host
parameter.

## Build and run on macOS

```sh
cmake -S plugin -B plugin/build -DCMAKE_BUILD_TYPE=Debug
cmake --build plugin/build --target Semantifx_Standalone -j 4
open plugin/build/Semantifx_artefacts/Debug/Standalone/Semantifx.app
```

Build all formats and run the tests with:

```sh
cmake --build plugin/build -j 4
ctest --test-dir plugin/build --output-on-failure
```

JUCE is fetched at tag `8.0.8`. Release artifacts use a separate build
directory with `-DCMAKE_BUILD_TYPE=Release`.

## Slot parameters and chain editing

Each slot has the generated catalogue parameters and a continuous bypass
parameter. IDs are derived from the generated table, for example
`eq1_low_shelf_gain_db`, `distortion2_drive_db`, and `reverb1_mix`. The host
parameters also include global `outputGain`, `mix`, and `bypass`.

The card rack shows the saved effect chain from left to right. Select a card to
edit it, use its bypass or solo toggle for transient auditioning, and remove or
drag cards to change the chain. Left and right arrows reorder the focused card.
Delete or Backspace removes it. Enter or Space selects it. The trailing `+`
card lists catalogue types that still have a free slot.

EQ keeps its interactive response graph. Other effect types show one rotary
control per generated parameter, with its catalogue display name and unit.
An empty chain passes audio through and shows `Press Shape sound, or + to add
an effect`.

## Shape sound and optimize chain

1. Press `Listen 10s` while audio is passing through the plugin.
2. Enter a target prompt.
3. `Shape sound` asks the worker to assemble a chain, applies that chain, and
   then starts optimization from the current slot values. A no-change assembly
   clears the chain and stops with `Prompt asks for no change; chain cleared`.
4. `Optimize chain` runs parameter optimization on the current non-empty chain.

During a run, `Stop` keeps the latest audible values and `Revert` restores the
composition, parameter values, and bypasses captured before the first start.
Escape performs Revert. Every progress snapshot containing parameters is
applied on the message thread, so the current candidate is audible while the
worker runs. A final `analysis_result` is applied as the current configuration.

Changing the chain structure restarts analysis immediately. Releasing a knob
on an active slot schedules one restart after 300 ms, so a group of knob edits
becomes one request. Bypass and solo changes do not restart analysis. A
restart keeps the original pre-run snapshot, and stale worker results are
discarded by request ID.

`Import` accepts a successful `analysis_result` JSON for the current chain and
validates its order, instances, effects, names, units, and finite values before
applying it.

## Linux: DSP-only checks

Linux does not need the JUCE GUI or audio modules for the reusable catalogue
DSP. Run:

```sh
scripts/check-dsp
```

This builds and runs the JUCE-free math, capture, effect parity, and slot-pool
tests. The full plugin and processor tests must be built on macOS, where the
JUCE GUI and audio dependencies are available.

`scripts/check-plugin-syntax` goes one step further on Linux: it clones the
pinned JUCE tag into `plugin/build-juce-src` (or uses `SEMANTIFX_JUCE_DIR`) and
runs a header-only syntax compile of every plugin source and test with warnings
on. It catches type, signature, and include mistakes in the JUCE code without
building JUCE; linking and runtime behaviour still need the macOS build.

The plugin uses JUCE's `dsp::Convolution` for reverb in real time. Generated
impulse responses are built by the message-thread timer, not by the audio
callback. The direct convolver remains available for parity tests and offline
DSP checks.
