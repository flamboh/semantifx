# SemantiFX inference protocol

The newline-delimited JSON (JSONL) contract between an audio-effect host and
the local inference worker. Each input line is one analysis request and each
output line is exactly one result with the same `request_id`. The worker never
writes logs or progress messages to stdout.

The contract is generated from the effect definitions in
[`effects`](effects): edit those TOML files and run `scripts/gen` to update
the schemas in [`schemas`](schemas), the plugin parameter table, and the
inference chain schema together. `scripts/gen --check` verifies the checked-in
artifacts match the definitions. Request examples live in
[`examples`](examples).

The contract is intentionally strict and describes exactly two effect
instances: `eq-1` and `distortion-1`. The host may submit either order; the
optimizer keeps that topology fixed while searching its parameters.

## Request

An analysis request contains:

- `type`: `"analyze"`
- `request_id`: a caller-generated correlation identifier
- `prompt`: the musician's target description
- `audio.path`: a path to the captured excerpt
- `chain`: `eq-1` and `distortion-1` in the desired order, including every
  current physical parameter value
- `options`: optional backend and optimization settings

`options.random_seed` accepts an unsigned 32-bit integer. Real adapters apply
it after model lookup so cold and warm cached requests begin optimization from
the same random state.

Unknown fields are rejected so host/worker skew fails visibly. Paths are passed
as data and are never interpreted by a shell.

## Result

Every well-framed input produces either an `ok` result containing the submitted
`chain_order` and exactly 22 canonically ordered, named, unit-bearing parameters,
or an `error` result with a stable error code, actionable remediation, and
optional details. Fatal stream errors such as invalid JSON use `request_id:
null` because no correlation id can be trusted.

Each parameter identifies its effect instance and effect type. Partial,
duplicate, reordered, or misidentified snapshots are rejected so a host never
applies a suggestion to the wrong processor.

## Future chain topology

Arbitrary supplied chains need repeated-effect validation, bypass state, and an
explicit way to prove that a result belongs to the exact chain submitted. This
remains chain optimization only; automatically choosing effect instances and
their order is a separate future chain-assembly operation.
