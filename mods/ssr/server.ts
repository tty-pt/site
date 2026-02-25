import React from "https://esm.sh/react@18";
import ReactDOMServer from "https://esm.sh/react-dom@18/server";

import { Layout } from "./ui.tsx";

interface ModuleEntry {
  id: string;
  title: string;
  routes: string[];
  ssr: string;
  be?: string;
}

async function loadModulesFromModsLoad(): Promise<ModuleEntry[]> {
  try {
    const modsLoadUrl = new URL("../../mods.load", import.meta.url);
    const contents = await Deno.readTextFile(modsLoadUrl);
    const lines = contents.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);

    const seen = new Set<string>();
    const modules: ModuleEntry[] = [];

    for (const line of lines) {
      const match = line.match(/\/mods\/([^/]+)\//);
      if (!match) continue;
      const id = match[1];
      if (seen.has(id)) continue;
      seen.add(id);

      const modJsonUrl = new URL(`../../mods/${id}/mod.json`, import.meta.url);
      let data: any;
      try {
        data = JSON.parse(await Deno.readTextFile(modJsonUrl));
      } catch {
        continue;
      }

      let routes: string[] = [];
      if (typeof data?.routes === "string") {
        routes = [data.routes];
      } else if (Array.isArray(data?.routes)) {
        routes = data.routes.filter((route: unknown) => typeof route === "string");
      }

      if (routes.length === 0) continue;

      modules.push({
        id: data?.id || id,
        title: data?.title || id,
        routes,
        ssr: data?.ssr || "",
        be: data?.be,
      });
    }

    return modules;
  } catch {
    return [];
  }
}

async function getModules(req: Request): Promise<ModuleEntry[]> {
  const header = req.headers.get("X-Modules");
  if (header) {
    try {
      const json = decodeURIComponent(header);
      const parsed = JSON.parse(json) as ModuleEntry[];
      if (Array.isArray(parsed) && parsed.length > 0) {
        return parsed;
      }
    } catch (e) {
      console.error("Failed to parse modules header:", e);
    }
  }
  return await loadModulesFromModsLoad();
}

function IndexPage({ modules, user, path }: { modules: ModuleEntry[]; user: string | null; path: string }) {
  const buttons = modules.map((item) =>
    React.createElement("a", {
      key: item.id,
      href: `/${item.id}/`,
      className: "btn"
    }, item.title || item.id)
  );

  return React.createElement(Layout, { user, title: "tty.pt", path },
    React.createElement("div", { className: "center" }, buttons)
  );
}

function matchRoute(path: string, route: string): { matched: boolean; params: Record<string, string> } {
  const cleanPath = path.replace(/\/$/, "");
  const cleanRoute = route.replace(/\/$/, "");
  if (cleanPath === cleanRoute) return { matched: true, params: {} };

  const p = cleanPath.split("/").filter(Boolean);
  const r = cleanRoute.split("/").filter(Boolean);
  if (p.length !== r.length) return { matched: false, params: {} };

  const params: Record<string, string> = {};
  for (let i = 0; i < r.length; i++) {
    if (r[i].startsWith(":")) {
      params[r[i].slice(1)] = p[i];
    } else if (r[i] !== p[i]) {
      return { matched: false, params: {} };
    }
  }
  return { matched: true, params };
}

function renderPage(content: React.ReactElement): string {
  const html = ReactDOMServer.renderToString(content);
  return `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=3.0, user-scalable=yes">
  <title>tty.pt</title>
  <link rel="stylesheet" href="/styles.css">
</head>
<body style="margin: 0">
  ${html}
</body>
</html>`;
}

async function handleRequest(req: Request): Promise<Response> {
  const url = new URL(req.url);
  const path = url.pathname;
  const user = req.headers.get("X-Remote-User");
  const modules = await getModules(req);

  let content: React.ReactElement;
  let status = 200;

  if (path === "/" || path === "") {
    content = React.createElement(IndexPage, { modules, user, path });
  } else if (req.method === "GET") {
    let handled = false;
    for (const mod of modules) {
      const modUrl = new URL(`../../mods/${mod.id}/ssr/index.tsx`, import.meta.url).href;
      let modEntry;
      try {
        modEntry = await import(modUrl);
      } catch {
        continue;
      }
      const modRoutes = modEntry?.routes || mod.routes || [];
      for (const route of modRoutes) {
        const match = matchRoute(path, route);
        if (match.matched) {
          if (modEntry?.render) {
            try {
              const rendered = await modEntry.render({ user, path, params: match.params });
              if (rendered) {
                content = rendered;
                handled = true;
                break;
              }
            } catch (e) {
              console.error("Error in render:", e);
            }
          }
        }
      }
      if (handled) break;
    }
    if (!handled) {
      status = 404;
      content = React.createElement(Layout, { user, title: "Not Found", path },
        React.createElement("p", null, "Page not found")
      );
    }
  } else {
    status = 404;
    content = React.createElement(Layout, { user, title: "Not Found", path },
      React.createElement("p", null, "Page not found")
    );
  }

  return new Response(renderPage(content), {
    status,
    headers: { "Content-Type": "text/html" },
  });
}

const listener = Deno.serve({ port: 3000 }, handleRequest);

console.log("Deno SSR server running on http://localhost:3000");
