# SemantiFX inference protocol

The newline-delimited JSON (JSONL) contract between an audio-effect host and
the local inference worker. Each input line is one request and each output
line is one result with the same `request_id`. The worker never writes logs
or progress messages to stdout.

The protocol has two request types:

- `assemble` asks the worker to choose and order effect instances for a
  target prompt.
- `analyze` asks the worker to optimize the parameters of a supplied chain
  against captured audio.

Effect-specific parameter names, units, ranges, and defaults come from
[`effects`](effects). The schemas also define the request and result
envelopes. Run `scripts/gen --check` after changing an effect definition to
check the generated effect artifacts.

## Assemble request

An assembly request contains:

- `type`: `"assemble"`
- `request_id`: a caller-generated correlation identifier
- `prompt`: the target description
- `options.backend`: optional `"mock"` or `"jev"`

An assembly request has no audio path or chain. Assembly is a separate request
from analysis. The worker returns an `assembly_result` containing a chain
whose parameters start at catalogue defaults, selection and ordering
judgments, and warnings.

## Analyze request

An analysis request contains:

- `type`: `"analyze"`
- `request_id`: a caller-generated correlation identifier
- `prompt`: the target description
- `audio.path`: a path to the captured excerpt
- `chain`: an ordered chain of zero to eight effect instances
- `options`: optional backend and optimization settings

Each chain item has an opaque, non-empty `instance_id` of at most 64
characters, a catalogue `effect_type`, and the complete parameter object for
that effect type.
Instance IDs are unique within a chain. An empty chain is valid. The chain
order is fixed for the duration of an analysis request.

Analysis options keep the existing `mock` and `clap` backends and the
existing model and optimization fields. `options.random_seed` accepts an
unsigned 32-bit integer. Real adapters apply it after model lookup so cold
and warm cached requests begin optimization from the same random state.

Unknown fields are rejected so host/worker skew fails visibly. Paths are
passed as data and are never interpreted by a shell.

## Results

Every well-framed request produces an `ok` or `error` result with the
request's correlation ID. Fatal stream errors such as invalid JSON use
`request_id: null` because no correlation ID can be trusted.

An `assembly_result` with status `"ok"` contains:

- `chain`: the assembled chain in the same item shape accepted by
  `analyze`
- `judgments.selection`: selection probabilities by effect type
- `judgments.no_change`: the no-change probability
- `judgments.order`: ordering probabilities keyed by effect-type pair
- `warnings`: a list of non-fatal messages

An `analysis_result` with status `"ok"` contains:

- `chain_order`: the submitted instance IDs, in signal order
- `parameters`: named, unit-bearing parameter entries for the submitted
  instances
- `metrics`: backend metrics
- `warnings`: a list of non-fatal messages

The parameter list is variable length and follows `chain_order`. An empty
chain produces empty `chain_order` and `parameters` arrays. Each entry
identifies its instance and effect type. The worker and host use the submitted
chain to reject incomplete, duplicate, reordered, or misidentified snapshots.

An `error` result uses the same envelope and contains:

- `error.code`: a stable machine-readable code
- `error.message`: a human-readable description
- `error.remediation`: steps the caller can take
- `error.details`: optional structured context

Assembly failures use `assembly_unavailable`; malformed requests use
`invalid_request`.

## Examples

The [assemble request](examples/assemble-request.json) and its one-line
[JSONL form](examples/assemble-request.jsonl) show the separate assembly
request. The [analyze request](examples/analyze-request.json) and its
[JSONL form](examples/analyze-request.jsonl) show a valid non-empty analysis
chain.
