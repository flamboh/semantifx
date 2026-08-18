# Initial research plan

## Product question

Given an existing effect chain, can CLAP-guided optimization produce a useful,
editable configuration that moves source audio toward a natural-language target?

Chain assembly is not part of this study. Holding the effect instances fixed
and treating order as explicit input lets us evaluate parameter optimization
before adding a second, combinatorial search problem.

## First technical study

Start with the current reorderable six-band EQ + distortion chain, then add a small
number of fixed-chain fixtures only after this slice is measured. Across a
fixed set of source excerpts and musician-written target prompts, compare:

- effect chains and effect order;
- 3, 5, and 10 second excerpts;
- neutral, current-state, and random parameter initialization;
- bounded optimization budgets and early stopping;
- elapsed time and run-to-run variation;
- parameter extremity, loudness change, and unintended artifacts;
- distortion alias energy, crest-factor change, and parameter-bound hits;
- prompt alignment and preference against the unprocessed and initial-chain audio;
- musician-rated usefulness, predictability, and editability.

Embedding objective values are diagnostic measurements, not user-facing
confidence scores.

## Initial success criteria

- The optimized chain is preferred to its initial configuration for the target prompt.
- A musician can reach a useful starting point faster than manual exploration.
- The result is understandable and every changed parameter remains editable.
- A bad suggestion is cheap to reject or stop early.
- Analysis latency does not break the creative flow.
- Results remain useful across more than one effect type and chain topology.
- Equivalent differentiable and JUCE configurations produce perceptually matched audio.

Before recording participants or treating interviews as publishable human-
subjects research, obtain the appropriate institutional determination.

## First measured baseline

The initial Apple Silicon CPU measurements and reproducible benchmark commands
for the EQ slice are recorded in [benchmark-baseline.md](benchmark-baseline.md).
The strongest early engineering result is that retaining CLAP in a persistent
worker removes most repeated-request startup latency.
