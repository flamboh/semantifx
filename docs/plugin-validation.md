# Plugin validation

Validation snapshots: July 10, 2026, using the arm64 Debug artifacts in
`plugin/build/Semantifx_artefacts/Debug`. Results for both the initial build and
the post-fix rebuild are retained below.

## Apple Audio Unit validation

Install a build where macOS can discover it, restart the per-user component
registrar, and validate the component identity declared in its `Info.plist`:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
ditto plugin/build/Semantifx_artefacts/Debug/AU/Semantifx.component \
  "$HOME/Library/Audio/Plug-Ins/Components/Semantifx.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aufx Smeq Smfx
```

Initial and post-fix result: `AU VALIDATION SUCCEEDED` (exit 0). The render tests passed for mono and stereo,
block sizes from 64 to 4096 frames, and sample rates from 11.025 to 192 kHz.
The custom Cocoa view, bypass property, parameter scheduling, and state reset
tests passed. `auval` printed non-fatal "Parameter did not retain default value
when set" warnings for the six Q parameters (all report a normalized default of
`0.483433`); the enclosing parameter tests still passed.

The exact warning, repeated for Air, High Mid, Mid, Low Mid, Low Shelf, and High
Shelf Q, was:

```text
WARNING: retrievedValue = 0.483433 (was 0.483433), Parameter did not retain default value when set
```

Because the displayed retrieved and previous normalized values are identical
and the enclosing retention/reset tests pass, this appears to be a precision
artifact in `auval`'s comparison rather than a parameter-mapping failure.

The Debug AU has only a linker-generated ad-hoc signature. This strict check
does not pass:

```sh
codesign --verify --deep --strict --verbose=2 \
  plugin/build/Semantifx_artefacts/Debug/AU/Semantifx.component
```

It reports `code has no resources but signature indicates they must be
present`. Distribution builds need an explicit, resource-sealing signature.

## VST3 pluginval

`pluginval` 1.0.4 was installed with `brew install --cask pluginval`.

The normal compatibility pass succeeded (exit 0):

```sh
mkdir -p /tmp/semantifx-pluginval
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --validate plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  --strictness-level 5 \
  --sample-rates 44100,48000,96000 \
  --block-sizes 64,128,512,1024 \
  --timeout-ms 30000 \
  --output-dir /tmp/semantifx-pluginval \
  --output-filename vst3-strictness-5.txt \
  --verbose
```

Audio processing, automation, editor lifecycle, state, parameter accessors, and
bus-layout tests passed. Opening and automating the editor repeatedly emitted
Debug-build assertions at `juce_String.cpp:327`, but pluginval still completed
successfully and did not crash.

### Initial maximum-strictness result

The initial maximum-strictness randomized pass found one state-restoration failure:

```sh
mkdir -p /tmp/semantifx-pluginval
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --validate plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  --strictness-level 10 \
  --sample-rates 44100,48000,96000,192000 \
  --block-sizes 1,16,64,512,4096 \
  --random-seed 1234567 \
  --repeat 3 \
  --randomise \
  --timeout-ms 30000 \
  --output-dir /tmp/semantifx-pluginval \
  --output-filename vst3-strictness-10.txt
```

Result: `FAILURE` / nonzero exit. Test 21 reported:

```text
Bypass not restored on setStateInformation -- Expected value within 0.1 of:
0.321689, Actual value: 0
```

All stress audio-processing combinations, parameter thread-safety checks,
parameter fuzzing, automation, and bus tests completed without a crash. The
Steinberg VST3 validator was not run because no validator path was configured.

### Post-fix maximum-strictness result

After changing bypass to a continuous normalized parameter and removing
ambiguous non-ASCII UI literals, the same fixed-seed test was rerun against the
rebuilt VST3. The only command change was the output filename:

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --validate plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3 \
  --strictness-level 10 \
  --sample-rates 44100,48000,96000,192000 \
  --block-sizes 1,16,64,512,4096 \
  --random-seed 1234567 \
  --repeat 3 \
  --randomise \
  --timeout-ms 30000 \
  --output-dir /tmp/semantifx-pluginval \
  --output-filename vst3-strictness-10-post-fix.txt
```

Result: `SUCCESS` (exit 0). Plugin state restoration passed in all three
randomized repeats, including the repeat that previously failed. Parameter
thread safety, parameter fuzzing, automation, editor lifecycle, and all audio
processing combinations passed. The post-fix console log contained no
`juce_String.cpp:327` assertions and no other JUCE assertions. The Steinberg
VST3 validator remained skipped because no validator path was configured.

The VST3 bundle itself passes strict on-disk signature verification:

```sh
codesign --verify --deep --strict --verbose=2 \
  plugin/build/Semantifx_artefacts/Debug/VST3/Semantifx.vst3
```

After validation, remove a temporary AU installation and refresh discovery:

```sh
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Semantifx.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
```
