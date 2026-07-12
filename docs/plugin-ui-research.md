# Semantifx plugin UI research

Research date: 2026-07-11. Scope: first-party product documentation and JUCE documentation only.

## Patterns in shipped plug-ins

### Analysis is a short, explicit stateful workflow

sonible smart:EQ 4 asks the user to choose a source profile, start playback, and press a record control. It signals active learning simultaneously with a pulsing record icon, an in-control progress bar, and a circular animation; it can return a result after about 6 seconds and normally applies it automatically after roughly 10 seconds. Longer learning can be configured, paused, and resumed at a more representative song section. [smart:EQ 4 manual, pp. 6–7](https://www.sonible.com/wp-content/uploads/2024/01/manual-smEQ4_EN.pdf), [sonible learning-time support note](https://help.sonible.com/hc/en-us/articles/11847248626972-How-can-I-change-the-learning-time-for-smart-EQ-4)

iZotope treats an assistant result as a starting point rather than a sealed answer. Ozone 12 separates a fast Auto-Master path from a guided Custom path, allows targets to be selected before analysis, and lets users exclude processing modules. Neutron 5 likewise presents Mix Assistant as a way to generate a custom chain that can then be refined with source profiles, references, and macros. [Ozone 12 Master Assistant guide](https://www.izotope.com/community/blog/how-to-use-master-assistant-in-ozone), [Neutron 5 product page](https://www.izotope.com/products/neutron)

**Implication for Semantifx:** make the prompt the target-selection step and make capture a single obvious action. The primary sequence should be `Ready → Listening 0–10 s → Analyzing → Applied`, not separate unexplained Capture and Analyze buttons. Keep `Stop and analyze` available after the model's minimum viable duration, and offer `Cancel` during either long-running state. Never imply that analysis is working when the DAW is stopped or the input is silent.

### The visualization is the main workspace, while detail appears on demand

FabFilter Pro-Q 4 gives most of its area to one interactive EQ/spectrum display. Selecting a band reveals a compact floating controller for exact values; the display itself supports direct frequency/gain movement, Q adjustment, bypass, and typed values. [Pro-Q 4 overview](https://www.fabfilter.com/help/pro-q/using/overview), [display workflow](https://www.fabfilter.com/help/pro-q/using/eqdisplay), [band controls](https://www.fabfilter.com/help/pro-q/using/bandcontrols)

Pro-Q can overlay pre- and post-EQ spectra and the combined response curve, while allowing analyzer range, resolution, speed, and tilt to be configured. It explicitly distinguishes the EQ scale from the spectrum/meter scale. [Pro-Q 4 analyzer](https://www.fabfilter.com/help/pro-q/using/analyzer), [display workflow](https://www.fabfilter.com/help/pro-q/using/eqdisplay)

**Implication for Semantifx:** the center of the editor should be a log-frequency graph showing a subdued input spectrum, a distinct output spectrum, and a high-contrast total EQ curve with six draggable nodes. Selecting one node opens only its gain, frequency, and Q controls. A collapsible `All parameters` inspector can expose all 18 values for learning and precision without making 18 permanent knobs the visual hierarchy.

### Generated settings remain inspectable and reversible

Ozone exposes high-level assistant controls and also a module view where every generated setting can be inspected and edited; modules can be soloed or bypassed to hear what the assistant changed. [iZotope on automated mastering](https://www.izotope.com/en/learn/ai-mastering)

Pro-Q keeps Undo/Redo and A/B state switching in its persistent top area, provides global bypass in its bottom bar, and allows individual EQ bands to be bypassed with a visible dimmed state. [Pro-Q 4 overview](https://www.fabfilter.com/help/pro-q/using/overview), [band controls](https://www.fabfilter.com/help/pro-q/using/bandcontrols)

smart:EQ 4 provides multiple states for A/B/C comparisons and permits one state to be copied to another before editing; its presets include all states. [smart:EQ 4 manual, p. 25](https://www.sonible.com/wp-content/uploads/2024/01/manual-smEQ4_EN.pdf)

**Implication for Semantifx:** preserve the pre-analysis parameter snapshot as `A` and put the generated result in `B`; make A/B, Undo, Redo, and global Bypass persistent header controls. Applying a second prompt should create one new undoable transaction, not 18 unrelated edits. Each band should also have an enabled/bypassed state. Show the last applied descriptor beside `B` so the comparison has semantic context.

### Resizing is part of precision, not decoration

Pro-Q offers several remembered size presets, scaling independent of size, arbitrary VST3 edge resizing, and full screen for precise spectrum/EQ editing. [Pro-Q 4 resizing and scaling](https://www.fabfilter.com/help/pro-q/using/fullscreenandresize)

JUCE's `AudioProcessorEditor` directly supports host/user resizing, minimum and maximum limits, a corner resizer, and host-provided GUI scale factors. [JUCE `AudioProcessorEditor`](https://docs.juce.com/master/classjuce_1_1AudioProcessorEditor.html)

**Implication for Semantifx:** retain a resizable editor with sensible limits and fluid layout. At narrow widths, keep prompt, primary action, status, curve, and A/B/bypass; collapse the full parameter inspector. At larger sizes, increase graph precision and reveal the inspector rather than merely scaling every control.

### Accessibility requires semantic controls and redundant status cues

FabFilter offers an accessible-color option for analyzer collision highlighting, demonstrating that meaning encoded by a difficult color should have an alternative palette. [Pro-Q 4 analyzer](https://www.fabfilter.com/help/pro-q/using/analyzer)

JUCE accessibility handlers expose roles, concise titles, descriptions, help text, current state, actions, value interfaces, and text interfaces to platform accessibility clients. JUCE also warns that an unnamed and undescribed element may be ignored. [JUCE `AccessibilityHandler`](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html), [JUCE accessibility roles](https://docs.juce.com/master/namespacejuce.html#a3a6a7f7ddb6a7de0353a81e8b0f6da3f)

**Implication for Semantifx:** use real `TextEditor`, button, toggle, progress, and slider components wherever possible. Give every graph node a band name and spoken gain/frequency/Q value, establish predictable Tab order, support Return to run and Escape to cancel, and pair every color/state change with text and shape. Status changes such as `Listening`, `Analyzing`, `Applied`, and errors should be available to assistive technology, not painted only into the graph.

## Concrete Semantifx design

### Default editor structure

1. **Header (persistent):** product name; Undo/Redo; `A` / `B`; global Bypass; compact settings menu.
2. **Prompt bar:** label `Describe the change`; single-line text input (example: `bright`); one primary button. It reads `Listen for 10 s` when ready, `Stop and analyze` after the minimum duration, and `Cancel` during inference.
3. **Status strip:** input-presence meter plus an exact state message and progress/time (`Listening · 6.4 / 10.0 s`). Completion reads `Applied “bright”` and offers `Try again`; errors remain visible and actionable.
4. **Main graph:** input/output spectra, total response curve, six nodes, frequency and dB axes. Clicking a node opens an anchored mini-panel for its three values and band bypass.
5. **Inspector (progressive disclosure):** collapsed by default; when expanded, a table of all six bands with gain/frequency/Q, plus output trim and wet/dry. Values remain host-automatable.
6. **Footer:** analyzer display controls and a short inference provenance line (descriptor, capture duration, model/seed in an optional details popover), not development-path or subprocess language.

The proposed layout applies FabFilter's visualization-first/detail-on-selection pattern, sonible's visible learning-state pattern, and iZotope's assistant-as-editable-starting-point pattern; it does not copy any product's visual styling.

### State and behavior contract

| State | Primary control | Status | Graph behavior |
|---|---|---|---|
| Ready | Listen for 10 s | `Play audio, then start listening` | Live input spectrum |
| Listening <6 s | Cancel | elapsed/target time; silence warning if needed | Live input, capture progress ring |
| Listening 6–10 s | Stop and analyze | elapsed/target time | Same |
| Analyzing | Cancel | indeterminate progress plus current phase | Freeze the captured aggregate spectrum |
| Applied | Try again | `Applied “descriptor”`; A/B hint | Animate/smooth from A curve to B curve |
| Error | Retry | plain-language cause and remedy | Keep last valid audio state and parameters |

### JUCE implementation route

- Use `juce::TextEditor` for the descriptor and standard buttons/toggles/sliders so native keyboard and accessibility behavior is retained; custom graph nodes should provide `AccessibilityHandler` role/title/value/actions. [JUCE `AccessibilityHandler`](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html)
- Keep UI controls synchronized with host parameters through `AudioProcessorValueTreeState` attachments; JUCE's `SliderAttachment` maintains the parameter/slider connection for its lifetime. [JUCE `SliderAttachment`](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState_1_1SliderAttachment.html)
- Build the analyzer with JUCE DSP FFT/windowing primitives and move audio-thread samples through a bounded FIFO; JUCE provides an official real-time spectrum-analyzer tutorial using this route. [JUCE spectrum-analyzer tutorial index](https://juce.com/learn/tutorials/)
- Run inference outside the audio/message threads using the existing worker boundary. JUCE `ThreadPool` supports queued background jobs, while `ProgressBar` monitors a 0–1 value and supports an indeterminate animation for values outside that range. [JUCE `ThreadPool`](https://docs.juce.com/master/classjuce_1_1ThreadPool.html), [JUCE `ProgressBar`](https://docs.juce.com/master/classProgressBar.html)
- Implement responsive breakpoints in `resized()` and keep `setResizable`/`setResizeLimits`; JUCE documents both host/user resizing and bounded window sizes. [JUCE `AudioProcessorEditor`](https://docs.juce.com/master/classjuce_1_1AudioProcessorEditor.html)

## Priority

For the next build, prioritize the workflow state machine, prompt/action/status region, visualization-first layout, A/B snapshot, and responsive/accessibility semantics. Spectrum FFT polish, fine analyzer preferences, and the expanded 18-value inspector can follow once the end-to-end DAW interaction is proven.
