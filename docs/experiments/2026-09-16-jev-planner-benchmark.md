# Jev as the chain-assembly planner: benchmark against a frontier LLM

Date: 2026-09-16. Harness: the InstructFX2FX evaluation code (Yu et al., DAFx 2026,
https://github.com/vaclisinc/InstructFX2FX), run from a local clone at
`~/code/oss/InstructFX2FX` on branch `jev-planner`. Nothing upstream was modified; the Jev
planner is a drop-in with the same interface as their LLM planner.

## Decision under test

Given one descriptor word ("warm", "punch", "church"), choose which of four effects
(compressor, distortion, EQ, reverb) belong in the chain. Ground truth is crowd behaviour
from the SAFE and SocialFX datasets: which effects people reached for when they typed that
word. 68 words, 3 repeats per planner. Ordering, parameters, audio, and multi-turn state are
not part of this test.

Metrics: **GT score** is the upstream metric, the ground-truth mass of the chosen effects (no
penalty for over-selecting; choosing all four scores 0.96). **F1** is against the set of
effects at least 10% of people used, so it penalises over-selection (choosing all four scores
0.63).

## Result

| Planner | Descriptions | F1 | GT score | Effects per chain | Same answer across repeats | Seconds per call |
|---|---|---:|---:|---:|---:|---:|
| Union Alpha (`stealth/union-alpha`, tool calling) | upstream | 0.63 | 0.65 | 1.7 | 46% | 12.4 |
| Union Alpha | rich | 0.66 | 0.66 | 1.7 | 46% | 15.4 |
| Jev, p >= 0.5 | upstream | 0.57 | 0.52 | 1.3 | 85% | 0.14 |
| Jev, p >= 0.5 | rich | 0.65 | | | | |
| Jev, p >= 0.4 | rich | 0.67 | | 2.3 | | |

Jev's probabilities are near-deterministic (standard deviation across repeats under 0.01),
so its residual inconsistency comes from values sitting near the threshold.

**Headline:** with good effect descriptions Jev matches the frontier LLM on accuracy, at
roughly one hundredth of the latency, with stable answers. Thresholds below 0.5 push Jev
ahead on both metrics, but they were chosen on the same 68 words, so treat that as
directional until checked on held-out words.

## What moves Jev's accuracy

Grid over 7 state variants x 7 question wordings, scored by AUC (threshold-free ranking of
correct effects above incorrect ones) with paired bootstrap against the baseline.

- **Persona text does nothing.** "You are an expert audio engineer", in the state or in the
  question, changed AUC by under 0.01 with intervals straddling zero.
- **Question wording is a threshold knob.** Rankings stayed within 0.715-0.730 AUC across
  wordings; the mean probability moved from 0.24 ("is it necessary") to 0.47 ("is it
  useful"). Policy prose in the state ("prefer the smallest chain") behaves the same way.
- **Richer effect descriptions are the one real lever.** Two-line engineer-facing
  descriptions raised AUC by 0.04 (95% CI +0.005 to +0.078). The same descriptions gave the
  LLM +0.027 F1 with an interval straddling zero: it already knows what the effects do, Jev
  judges the text it is given.
- **Criteria** (spelling out what yes and no mean) added about 0.015 F1. Harmless.

## Carried into SemantiFX

Neutral question wording ("is this effect useful for achieving the instruction"), two-line
effect descriptions in the state, inclusion threshold in code around 0.4 (to be tuned on
held-out words), no persona, no policy prose, pairwise order questions with a Borda tally.

Scripts and raw results: `eval/fx_selection/run_planner_benchmark.py`,
`sweep_jev_policy.py`, `probe_jev_prompts.py`, and `eval/results/` on the `jev-planner`
branch of the clone.
