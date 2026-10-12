# semantifx web

The landing page for SemantiFX. Vite+ with React and Tailwind, styled to match
[tagium](https://github.com/flamboh/tagium) with a violet accent instead of crimson.

```sh
bun install
bun run dev
bun run build   # writes dist/
```

`cloudflare.config.ts` serves the build as a Cloudflare Worker with static assets: `bun run deploy` (runs `cf deploy`).
