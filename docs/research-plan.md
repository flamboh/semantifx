# Initial research plan

## Product question

Can natural-language effect suggestions fit into a musician's workflow without
obscuring the underlying craft or reducing their sense of authorship?

## First technical study

Use a fixed set of source excerpts and musician-written EQ prompts to compare:

- 3, 5, and 10 second excerpts;
- neutral and random parameter initialization;
- 50, 100, 200, and 600 optimization iterations;
- elapsed time and run-to-run variation;
- parameter extremity and loudness change;
- musician-rated usefulness, alignment, and editability.

Embedding objective values are diagnostic measurements, not user-facing
confidence scores.

## Co-design rounds

1. **Discovery:** vocabulary, desired control, representative excerpts, and
   unacceptable automation behavior.
2. **Interaction prototype:** capture, waiting, preview, refinement, rejection,
   undo, and explanation.
3. **Working plugin:** use in real DAW sessions, including keyboard-only and
   reduced-mobility workflows.

## Initial success criteria

- A musician can reach a useful starting point faster than manual exploration.
- The result is understandable and remains manually editable.
- A bad suggestion is cheap to reject or undo.
- Analysis latency does not break the creative flow.
- The musician reports that the tool supports rather than replaces authorship.
- Equivalent Python and JUCE parameter snapshots produce perceptually matched
  results.

Before recording participants or treating interviews as publishable human-
subjects research, obtain the appropriate institutional determination.

## First measured baseline

The initial Apple Silicon CPU measurements and reproducible benchmark commands
are recorded in [benchmark-baseline.md](benchmark-baseline.md). The strongest
early engineering result is that retaining CLAP in a persistent worker removes
most repeated-request startup latency.

