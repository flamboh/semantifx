# Issue tracker

Issues and specs live in [GitHub Issues](https://github.com/flamboh/semantifx/issues). Use `gh` for issue operations and `gh issue view <number> --comments` to read a ticket with its discussion. Use `--body-file` for multiline issue text.

**PRs as a request surface: no.** Issues and PRs share a number space; resolve the object type before operating on it.

## Wayfinding conventions

- A map is one issue labeled `wayfinder:map`, with Notes, Decisions-so-far, and Fog in its body.
- Link child tickets as GitHub sub-issues. If unavailable, use a task list in the map and `Part of #<map>` at the top of each child. Use `wayfinder:research`, `wayfinder:prototype`, `wayfinder:grilling`, or `wayfinder:task` for the ticket type.
- Represent blockers with native issue dependencies. The `blocked_by` endpoint takes the blocker's database `id`, not its issue number or `node_id`. If dependencies are unavailable, use `Blocked by: #<n>, #<n>` at the top of the ticket.
- The frontier is the first open child in map order with no assignee and no open blockers. Native `issue_dependencies_summary.blocked_by` counts open blockers; check linked blockers when using the fallback.
- Claim a ticket by assigning it to the driving developer as the wayfinding session's first write.
- On resolution, comment with the answer, close the child, and add a context pointer to Decisions-so-far in the map.
