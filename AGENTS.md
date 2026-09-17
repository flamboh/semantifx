# SemantiFX

Text prompt in, editable effect-chain configuration out. A JUCE plugin
(`plugin/`, built and iterated on macOS) talks to a Python inference worker
(`inference/`, developed on Linux with CUDA) over a JSONL protocol. Chain
optimization tunes a fixed chain's parameters with a CLAP loss. Chain assembly
picks and orders the effects with TypeSafe Jev.

Read `CONTEXT.md` for the vocabulary before naming anything.

## Working here

- Solo repo. Work directly on `main`; no pull requests.
- Python: `cd inference && .venv/bin/python -m unittest discover -s tests`.
  Set `SEMANTIFX_RUN_MODEL_TESTS=1` to include the golden CLAP test.
- API keys live in `.env` at the repo root (gitignored). Never print them.
- Experiment write-ups go in `docs/experiments/`.

## Agent skills

### Issue tracker

GitHub Issues on `flamboh/semantifx`, driven with `gh`. See
`docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`,
`ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` at the root, ADRs under `docs/adr/`. See
`docs/agents/domain.md`.
