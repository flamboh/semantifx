# Initial fixed-effect plugin interface

## Product intent

This document describes the implemented EQ + distortion slice, not the final
product boundary. SemantiFX is an editable, prompt-driven effect-chain
optimizer. The interface should make a short loop obvious:

1. listen to a representative passage;
2. describe a desired direction;
3. inspect and edit the resulting EQ and distortion settings;
4. compare it with the unprocessed signal.

The model supplies a starting point. The musician retains direct access to
every parameter the model changed.

## Source patterns

- FabFilter Pro-Q makes the interactive EQ display the dominant surface,
  overlays the overall response, and reveals precise controls for the selected
  band instead of showing every parameter at equal weight. It also keeps
  global bypass/output functions at the edge of the workspace.
  [FabFilter Pro-Q 4 overview](https://www.fabfilter.com/help/pro-q/using/overview),
  [display workflow](https://www.fabfilter.com/help/pro-q/using/eqdisplay), and
  [band controls](https://www.fabfilter.com/help/pro-q/using/bandcontrols).
- iZotope Neutron treats assistant output as a personalized starting point and
  then exposes broader macro controls or a detailed view. Its Delta controls
  explicitly support hearing only the processing contribution.
  [Neutron 5 product documentation](https://www.izotope.com/products/neutron)
  and [Assistant View guide](https://www.izotope.com/community/blog/how-to-use-mix-assistant).
- sonible smart:EQ uses a visible learning phase with progress feedback, needs
  incoming audio, and then applies an editable filter result. Its learning
  phase takes about ten seconds, closely matching Semantifx's capture window.
  [smart:EQ 4 manual](https://www.sonible.com/wp-content/uploads/2024/01/manual-smEQ4_EN.pdf)
  and [product page](https://www.sonible.com/smarteq4/).
- JUCE supports resizable editors, custom-painted Components, attached
  parameters, keyboard focus, and accessibility roles/actions. The redesign
  should use those native surfaces rather than embedding a browser.
  [AudioProcessorEditor](https://docs.juce.com/master/classjuce_1_1AudioProcessorEditor.html),
  [Component accessibility](https://docs.juce.com/master/classjuce_1_1Component.html), and
  [AccessibilityHandler](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html).

## Concrete interface

### 1. Header: identity and listening comparison

- Left: `SEMANTIFX` wordmark, subtitle `Prompt-driven effects`, and a small
  `LOCAL · MS-CLAP` engine badge.
- Center: an attached selector for `EQ → DISTORTION` or `DISTORTION → EQ`.
- Right: `Import…`, a prominent `Hear original` toggle attached to the real
  bypass parameter, and no unrelated status text.
- The bypass control must communicate both visually and through its accessible
  title whether processing or the original signal is being heard.

### 2. Semantic command bar

- A compact status eyebrow changes among `READY`, `LISTENING 0–10 s`,
  `CAPTURE READY`, `ANALYZING`, `APPLYING`, `APPLIED`, `CANCELLED`, and `ERROR`.
- `Listen 10s` begins capture and becomes a determinate progress control while
  recording.
- The text field is the visual center and accepts natural language. Placeholder:
  `Describe a direction — “brighter, but keep the body”`.
- `Shape sound` runs the 600-iteration local analysis. Return in the
  text field triggers it when a capture is ready.
- `Cancel` appears while capture or analysis is active. Once optimization has
  produced a valid preview it becomes `Use current`, which stops and applies the
  visible candidate; Escape remains a discard action.
- Error text remains visible until a new action, rather than being encoded by
  color alone.

### 3. EQ canvas: primary workspace

- A large custom-painted response display occupies most of the editor.
- Logarithmic horizontal scale: 20 Hz–20 kHz, with labeled anchors at 20, 50,
  100, 200, 500, 1k, 2k, 5k, 10k, and 20k.
- Vertical scale: ±24 dB with a clear 0 dB line and subdued 6 dB grid lines.
- Draw the combined six-band response using the same `EqMath` functions as the
  processor. Repaint from APVTS values on the existing timer.
- Draw one colored, numbered node per fixed band. The selected node is larger
  and receives a high-contrast focus ring.
- Clicking a node selects its band. Dragging it edits that band's frequency and
  gain with proper parameter gestures and host notification. Keep Q editing in
  the precise controls for this iteration.
- The canvas is not a fake spectrum analyzer. A live analyzer should only be
  added later with an explicit real-time-safe data seam.

### 4. Effect inspector

- A seven-item rail names the fixed topology: `LOW`, `LOW MID`, `MID`,
  `HIGH MID`, `AIR`, `HIGH`, and `DISTORTION`.
- Selecting a rail item or graph node reveals exactly three large attached
  controls: `GAIN`, `FREQUENCY`, and `Q`.
- Selecting `DISTORTION` hides the EQ nodes and reveals `DRIVE`, `CHARACTER`,
  `TONE`, and the distortion's parallel `MIX`.
- Use the existing rotary drag interaction, direct numeric text entry, units,
  tooltips, and accessible names. Hide the other fifteen band controls rather
  than rendering an undifferentiated 18-knob wall.

### 5. Output strip

- Keep `Mix` and `Output` visible at all times in a narrow right-side or bottom
  strip.
- Display the latest prompt as `Last direction: …` after a successful result so
  the curve retains semantic context.
- Do not add A/B snapshots, undo history, meters, confidence, or live spectrum
  until the processor has real state/data support for them.

## Visual system

- Background: near-black blue-gray (`#0b1116`).
- Panels: `#121b22`; borders/grid: `#26343d`.
- Primary action and response curve: warm chartreuse (`#b7ef78`).
- Six band colors progress from teal through cyan, blue, violet, coral, and
  amber. Color is supplementary; every node is numbered and every band named.
- Use rounded 8–12 px panels, restrained shadows, and JUCE system fonts.
- Minimum target size: 32 px. Maintain visible keyboard focus and readable
  contrast.

## Responsive behavior

- Default size: approximately 1100 × 760.
- Resize range: 820 × 620 through 1600 × 1100.
- At narrow widths, stack the semantic bar above the graph and place output
  controls beside the selected-band inspector; never shrink text or targets
  below usable sizes.

## Implementation boundaries

- Keep inference and file/process work off the audio thread.
- Reuse all existing APVTS parameter IDs and attachments; do not change plugin
  state compatibility.
- Use JUCE Components and custom painting only; add no dependency.
- Keep import, cancellation, progress, resizing, and accessibility functional.
- Add tests for any extracted curve-coordinate or response-composition helpers.

## Acceptance checks

1. Project builds and all existing checks pass.
2. Every EQ and distortion parameter, including effect order, remains editable
   and automatable.
3. Entering a prompt, capturing audio, and analyzing visibly updates the curve
   and attached parameter controls.
4. Clicking/dragging a band node updates the corresponding host parameter.
5. `Hear original` audibly bypasses processing without losing the suggested
   settings.
6. The editor works at minimum and default sizes without overlap or clipped
   primary actions.
7. Prompt, capture, analyze, cancel, bypass, band selection, and numeric values
   are keyboard reachable and have meaningful accessibility names.
