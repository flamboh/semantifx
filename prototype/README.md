# SemantiFX interaction prototype

> **THROWAWAY UI:** this exists only to answer which capture → describe →
> analyze → compare workflow feels right. Once a direction wins, record the
> verdict in `NOTES.md` and delete or rewrite this prototype.

Three structural variants share the same in-memory simulated state:

- `A — Guided studio`: a linear, low-anxiety sequence.
- `B — Conversation`: prompt refinement as a creative dialogue.
- `C — Workbench`: a dense, parameter-forward professional surface.

Run from the repository root:

```sh
bun prototype/server.ts
```

Then open <http://localhost:4173/?variant=A>. Use the bottom switcher or the
left/right arrow keys when an input is not focused.

