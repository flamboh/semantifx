# SemantiFX inference protocol

Protocol version `1` is a newline-delimited JSON (JSONL) contract between an
audio-effect host and a local inference worker. Each input line is one analysis
request and each output line is exactly one result with the same `request_id`.
The worker never writes logs or progress messages to stdout.

The schemas in [`schemas/v1`](schemas/v1) are the source of truth. Examples are
available in [`examples`](examples).

## Request

An analysis request contains:

- `protocol_version`: currently `"1"`
- `type`: `"analyze"`
- `request_id`: a caller-generated correlation identifier
- `prompt`: the musician's description
- `audio.path`: an absolute or worker-relative path to the already-captured excerpt
- `effects`: an ordered list of effect identifiers; version 1 supports `eq`
- `options`: optional backend and Text2FX optimization settings

Unknown fields are rejected so that host/worker skew fails visibly. The host is
responsible for excerpt selection in version 1. Paths are passed as data and
are never interpreted by a shell.

## Result

Every well-framed input line produces either an `ok` result containing named,
unit-bearing parameters or an `error` result with a stable error code,
actionable remediation, and optional details. Fatal stream errors such as
invalid JSON use `request_id: null` because no correlation id can be trusted.

Parameter names match `dasp-pytorch`'s `ParametricEQ` controls. This keeps mock
and Text2FX results interchangeable while the JUCE DSP implementation is being
validated for parity.
