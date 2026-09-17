# semantifx web

The landing page for SemantiFX. Vite+ with React and Tailwind, styled to match
[tagium](https://github.com/flamboh/tagium) with a violet accent instead of crimson.

```sh
bun install
bun run dev
bun run build   # writes dist/
```

`wrangler.jsonc` serves `dist/` as Cloudflare static assets: `bunx wrangler deploy`.
