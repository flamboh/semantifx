# ADR 0002: Reverb uses a frozen noise table

Date: 2026-09-18. Status: accepted.

## Context

dasp-pytorch's noise-shaped reverberation draws new Gaussian noise on every
forward pass. That makes optimization non-reproducible and prevents the later
C++ renderer from matching Python. The optimizer also needs a smaller control
surface than dasp's 25 reverb parameters.

## Decision

Commit a 44.1 kHz, two-channel, twelve-band filtered-noise table under
`protocol/reverb/`. Python loads it lazily and uses the same table for every
render. Three macros, decay, damping, and mix, expand to the twelve band gains
and twelve band decays with the math in the reverb protocol definition.

The renderer supports 44.1 kHz only. Mono input uses table channel 0 and
stays mono. Stereo input uses table channels 0 and 1.

## Consequences

- The repository carries a 6.3 MB binary asset.
- The plugin must resample the table or ship a second table for 48 kHz sessions.
- The maximum reverb tail is 1.49 seconds at 44.1 kHz.
- Python and the later plugin renderer can share the exact filtered noise and
  macro expansion.
