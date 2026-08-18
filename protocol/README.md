# SemantiFX inference protocol

Protocol version `2` is the only supported newline-delimited JSON (JSONL)
contract between an audio-effect host and the local inference worker. Each
input line is one analysis request and each output line is exactly one result
with the same `request_id`. The worker never writes logs or progress messages
to stdout.

Version 2 is intentionally strict and describes exactly two effect instances:
`eq-1` and `distortion-1`. The host may submit either order; the optimizer keeps
that topology fixed while searching its parameters. The schemas in
[`schemas/v2`](schemas/v2) are the source of truth, with request examples in
[`examples`](examples).

## Request

An analysis request contains:

- `protocol_version`: `"2"`
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

Arbitrary supplied chains need schema negotiation, repeated-effect validation,
bypass state, and an explicit way to prove that a result belongs to the exact
chain submitted. This remains chain optimization only; automatically choosing
effect instances and their order is a separate future chain-assembly operation.
