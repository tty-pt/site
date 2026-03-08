import { renderToString } from "preact-render-to-string";
import { Layout } from "@/ssr/ui.tsx";

/**
 * Module Router Library
 * 
 * Shared utilities for routing requests to legacy NDC modules.
 * Extracted from mods/ssr/server.tsx for reuse in Fresh catch-all routes.
 */

export interface ModuleEntry {
  id: string;
  title: string;
  flags: number;
}

export interface RouteMatch {
  matched: boolean;
  params: Record<string, string>;
}

export interface RenderContext {
  user: string | null;
  path: string;
  params: Record<string, string>;
  searchParams: URLSearchParams;
  body?: string | null;
}

/**
 * Parse X-Modules header from NDC proxy
 */
export function getModules(req: Request): ModuleEntry[] {
  const header = req.headers.get("X-Modules");
  if (!header) {
    return [];
  }
  
  try {
    const json = atob(header);
    const parsed = JSON.parse(json) as ModuleEntry[];
    
    if (!Array.isArray(parsed)) {
      console.error("X-Modules header is not an array");
      return [];
    }
    
    return parsed;
  } catch (e) {
    console.error("Failed to parse X-Modules header:", e);
    return [];
  }
}

/**
 * Match a path against a route pattern (supports :param syntax)
 * 
 * Examples:
 *   matchRoute("/song/123", "/song/:id") → { matched: true, params: { id: "123" } }
 *   matchRoute("/song", "/song") → { matched: true, params: {} }
 *   matchRoute("/poem/foo", "/song/:id") → { matched: false, params: {} }
 */
export function matchRoute(path: string, route: string): RouteMatch {
  const cleanPath = path.replace(/\/$/, "");
  const cleanRoute = route.replace(/\/$/, "");

  if (cleanPath === cleanRoute) {
    return { matched: true, params: {} };
  }

  const p = cleanPath.split("/").filter(Boolean);
  const r = cleanRoute.split("/").filter(Boolean);

  if (p.length !== r.length) {
    return { matched: false, params: {} };
  }

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

/**
 * Render a page using module's render function
 * 
 * Iterates through modules to find one that handles the given path,
 * then calls its render() function with the provided context.
 * 
 * Returns HTML string and status code, or null if no module handled the route.
 */
export async function renderModuleRoute(
  path: string,
  modules: ModuleEntry[],
  context: RenderContext
): Promise<{ html: string; status: number } | null> {
  for (const mod of modules) {
    const modUrl = new URL(`../mods/${mod.id}/index.tsx`, import.meta.url).href;
    let modEntry;

    try {
      modEntry = await import(modUrl);
    } catch (e) {
      // Module doesn't have index.tsx or failed to load
      continue;
    }

    const modRoutes = modEntry?.routes || [];
    
    for (const route of modRoutes) {
      const match = matchRoute(path, route);

      if (match.matched && modEntry?.render) {
        try {
          const rendered = await modEntry.render({
            user: context.user,
            path: context.path,
            params: match.params,
            searchParams: context.searchParams,
            body: context.body,
          });

          if (rendered) {
            // Wrap in full HTML page structure
            const html = renderToString(rendered);
            const fullHtml = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=3.0, user-scalable=yes">
  <title>tty.pt</title>
  <link rel="stylesheet" href="/styles.css">
</head>
<body style="margin: 0">
  ${html}
  <script src="/app.js" defer></script>
</body>
</html>`;

            return { html: fullHtml, status: 200 };
          }
        } catch (e) {
          console.error(`Error rendering module ${mod.id}:`, e);
          // Continue to next module
        }
      }
    }
  }

  return null;
}

/**
 * Render a 404 page with proper layout
 */
export function render404(user: string | null, pathStr: string): string {
  const jsx = (
    <Layout user={user} title="Not Found" path={pathStr}>
      <p>Page not found</p>
    </Layout>
  );
  
  const content = renderToString(jsx);

  return `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=3.0, user-scalable=yes">
  <title>tty.pt - Not Found</title>
  <link rel="stylesheet" href="/styles.css">
</head>
<body style="margin: 0">
  ${content}
  <script src="/app.js" defer></script>
</body>
</html>`;
}
