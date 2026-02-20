import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";
import { dirname, fromFileUrl, resolve } from "https://deno.land/std@0.208.0/path/mod.ts";

const moduleDir = dirname(fromFileUrl(import.meta.url));
const repoRoot = resolve(moduleDir, "../../../");

async function getPoems(): Promise<string[]> {
  try {
    const dir = await Deno.readDir(`${repoRoot}/items/poem/items`);
    const poems: string[] = [];
    for await (const entry of dir) {
      if (entry.isDirectory && !entry.name.startsWith(".")) {
        poems.push(entry.name);
      }
    }
    return poems.sort((a, b) => a.localeCompare(b));
  } catch {
    return [];
  }
}

async function getPoemContent(id: string): Promise<string | null> {
  try {
    const path = `${repoRoot}/items/poem/items/${id}/pt_PT.html`;
    return await Deno.readTextFile(path);
  } catch {
    return null;
  }
}

function PoemList({ user, path, poems }: { user: string | null; path: string; poems: string[] }) {
  const buttons = poems.map((poem) =>
    React.createElement("a", {
      key: poem,
      href: `/poem/${poem}/`,
      className: "btn wsnw h"
    }, poem)
  );

  return React.createElement(Layout, { user, title: "poem", path },
    React.createElement("div", { className: "v f fic" },
      buttons.length > 0 ? buttons : React.createElement("p", null, "No poems yet."),
      React.createElement("a", { className: "btn", href: "/poem/add" }, "Add Poem")
    )
  );
}

function PoemAdd({ user, path }: { user: string | null; path: string }) {
  return React.createElement(Layout, { user, title: "Add Poem", path },
    React.createElement("form", {
      action: "/poem/add",
      method: "POST",
      encType: "multipart/form-data",
      className: "v f fic"
    },
      React.createElement("label", null, "ID: ", React.createElement("input", { required: true, name: "id" })),
      React.createElement("label", null, "File: ", React.createElement("input", { required: true, type: "file", name: "file" })),
      React.createElement("button", null, "Upload")
    )
  );
}

function PoemDetail({ user, path, id, html }: { user: string | null; path: string; id: string; html: string }) {
  return React.createElement(Layout, { user, title: `poem: ${id}`, path },
    React.createElement("div", { className: "v f fic" },
      React.createElement("div", { dangerouslySetInnerHTML: { __html: html } })
    )
  );
}

export const routes = ["/poem", "/poem/add", "/poem/:id"];

export async function render({ user, path, params }: { user: string | null; path: string; params: Record<string, string> }) {
  if (path === "/poem/add") {
    return PoemAdd({ user, path });
  }

  if (path === "/poem" || path === "/poem/") {
    const poems = await getPoems();
    return PoemList({ user, path, poems });
  }

  const id = params.id;
  if (id) {
    const html = await getPoemContent(id);
    if (html) {
      return PoemDetail({ user, path, id, html });
    }
  }

  return null;
}
