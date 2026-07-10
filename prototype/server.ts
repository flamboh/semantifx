// THROWAWAY PROTOTYPE SERVER — no dependencies, no persistence.
const root = import.meta.dir;
const types: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".svg": "image/svg+xml",
};

Bun.serve({
  port: Number(process.env.PORT ?? 4173),
  async fetch(request) {
    const url = new URL(request.url);
    const path = url.pathname === "/" ? "/index.html" : url.pathname;
    if (path.includes("..")) return new Response("Not found", { status: 404 });
    const file = Bun.file(root + path);
    if (!(await file.exists())) return new Response("Not found", { status: 404 });
    const extension = path.slice(path.lastIndexOf("."));
    return new Response(file, { headers: { "content-type": types[extension] ?? "application/octet-stream" } });
  },
});

console.log("SemantiFX UI prototype: http://localhost:4173/?variant=A");

