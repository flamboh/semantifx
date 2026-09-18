# ADR 0001: A fixed slot pool holds the plugin's variable effect chain

Date: 2026-09-18. Status: accepted. Decided on the prompt-to-chain map
(GitHub issue #8, "Plugin: representing, rendering, and editing a variable
chain").

## Context

Chain assembly produces an effect chain of any subset, order, and (through
user edits) repeats of the catalogue's effect types. The JUCE plugin must hold
that chain. Hosts read an `AudioProcessor`'s parameter set once, at
construction, and never again: a plugin cannot create or delete host parameters
when the assembler adds or removes an effect instance. Saved host state and
automation are keyed to that fixed set.

## Decision

- The plugin declares a **pool of two effect instances per catalogue type**
  (`<type>-1`, `<type>-2`), each with all of its parameters plus a bypass, for
  every type at construction. With four types that is eight slots.
- The **effect chain is an ordered subset of those slots**, stored in the
  APVTS `ValueTree` as one `chain` property (a comma-separated slot list). It
  is saved and restored with plugin state but is **not a host parameter**, so
  hosts cannot automate the chain's structure.
- Instance ids on the wire are slot names. The assembler mints `<type>-1`, so
  an instance that survives a re-assembly keeps its id and its values with no
  id mapping.
- The audio thread reads the composition from one atomic word packed by the
  message thread and skips inactive slots. Every slot's DSP is prepared up
  front.

## Consequences

- Chain length is bounded by the pool: 0 to 8 instances, at most two of one
  type. The protocol's chain-length limit falls out of this.
- The parameter count is fixed at construction (60 floats plus 8 bypasses),
  which is what hosts require, at the cost of declaring knobs for slots that
  may never be used.
- Adding a catalogue type means adding a slot pair, a C++ class, and a factory
  case; it is a rebuild, never a runtime change.
- Saved states from the fixed-pair plugin do not carry over. Nothing has
  shipped, so no migration is written.
