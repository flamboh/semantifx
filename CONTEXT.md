# SemantiFX

SemantiFX turns a natural-language description of a desired sound into an
editable configuration for a supplied audio-effect chain.

## Language

**Target prompt**:
A natural-language description of how the processed audio should sound.
_Avoid_: Command, instruction

**Effect type**:
A kind of audio processor, such as an equalizer, distortion, or reverb.
_Avoid_: Effect when the distinction between a kind and an occurrence matters

**Effect instance**:
One identifiable occurrence of an effect type in a chain. Multiple instances
may have the same effect type but retain separate identities and parameters.

**Effect chain**:
An ordered sequence of effect instances through which audio is processed.
_Avoid_: Model, preset

**Chain configuration**:
The complete parameter state of an effect chain at a point in time.
_Avoid_: Preset when the configuration has not been intentionally saved for reuse

**Chain optimization**:
The search for a chain configuration whose processed audio aligns with a target
prompt while the chain's effect instances and order remain fixed.
_Avoid_: Chain generation, chain assembly

**Chain assembly**:
The selection, number, and ordering of effect instances in an effect chain.
It is distinct from optimizing the parameters of an existing chain.

**Suggestion**:
An optimized chain configuration offered as an editable starting point rather
than an authoritative result.
_Avoid_: Answer, final result
