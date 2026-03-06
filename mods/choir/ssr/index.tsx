import React from "https://esm.sh/react@18";

const repoRoot = Deno.env.get("REPO_ROOT") || "/home/quirinpa/site";

export const routes = ["/choir", "/choir/new", "/choir/:id", "/choir/:id/edit"];

// Read choir index database
async function getChoirs() {
  const indexPath = `${repoRoot}/items/choir/items/index.db`;
  const choirs: Array<{ id: string; title: string }> = [];
  
  try {
    const proc = new Deno.Command("qmap", {
      args: ["-l", indexPath],
      stdout: "piped",
    });
    const output = await proc.output();
    const text = new TextDecoder().decode(output.stdout);
    
    for (const line of text.trim().split("\n")) {
      if (!line) continue;
      const parts = line.split(" ");
      if (parts.length >= 2) {
        const id = parts[0];
        const title = parts.slice(1).join(" ");
        choirs.push({ id, title });
      }
    }
  } catch {
    // Database doesn't exist yet
  }
  
  return choirs;
}

// Read choir details
async function getChoir(id: string) {
  const choirPath = `${repoRoot}/items/choir/items/${id}`;
  
  try {
    const title = await Deno.readTextFile(`${choirPath}/title`);
    let owner = "";
    try {
      owner = (await Deno.readTextFile(`${choirPath}/.owner`)).trim();
    } catch {
      // No owner file
    }
    
    let counter = "0";
    try {
      counter = (await Deno.readTextFile(`${choirPath}/counter`)).trim();
    } catch {
      // No counter file
    }
    
    let formats: string[] = [];
    try {
      const formatText = await Deno.readTextFile(`${choirPath}/format`);
      formats = formatText.trim().split("\n").filter(f => f.length > 0);
    } catch {
      // Use default formats
      formats = [
        "entrada",
        "aleluia",
        "ofertorio",
        "santo",
        "comunhao",
        "acao_de_gracas",
        "saida",
        "any"
      ];
    }
    
    return { id, title: title.trim(), owner, counter, formats };
  } catch {
    return null;
  }
}

export async function render({ user, path, params }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
}) {
  // List all choirs
  if (path === "/choir") {
    const choirs = await getChoirs();
    
    return React.createElement("div", { style: { padding: "2rem" } },
      React.createElement("h1", null, "Choirs"),
      user ? React.createElement("div", { style: { marginBottom: "1rem" } },
        React.createElement("a", {
          href: "/choir/new",
          style: {
            padding: "0.5rem 1rem",
            backgroundColor: "#007bff",
            color: "white",
            textDecoration: "none",
            borderRadius: "4px",
            display: "inline-block"
          }
        }, "Create New Choir")
      ) : null,
      React.createElement("div", { style: { display: "flex", flexDirection: "column", gap: "1rem", marginTop: "1rem" } },
        choirs.length === 0
          ? React.createElement("p", null, "No choirs yet.")
          : choirs.map(choir =>
              React.createElement("div", {
                key: choir.id,
                style: {
                  border: "1px solid #ccc",
                  padding: "1rem",
                  borderRadius: "4px"
                }
              },
                React.createElement("h3", null,
                  React.createElement("a", { href: `/choir/${choir.id}` }, choir.title)
                ),
                React.createElement("p", { style: { fontSize: "0.9rem", color: "#666" } },
                  `ID: ${choir.id}`
                )
              )
            )
      )
    );
  }
  
  // Create new choir form
  if (path === "/choir/new") {
    if (!user) {
      return React.createElement("div", { style: { padding: "2rem" } },
        React.createElement("h1", null, "Login Required"),
        React.createElement("p", null, "You must be logged in to create a choir."),
        React.createElement("a", { href: "/auth/login" }, "Login")
      );
    }
    
    return React.createElement("div", { style: { padding: "2rem" } },
      React.createElement("h1", null, "Create New Choir"),
      React.createElement("form", {
        method: "POST",
        action: "/api/choir/create",
        encType: "multipart/form-data",
        style: { display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "500px" }
      },
        React.createElement("label", null,
          "Choir ID (no spaces):",
          React.createElement("input", {
            type: "text",
            name: "id",
            required: true,
            pattern: "[a-z0-9_-]+",
            style: { display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }
          })
        ),
        React.createElement("label", null,
          "Choir Name:",
          React.createElement("input", {
            type: "text",
            name: "title",
            required: true,
            style: { display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }
          })
        ),
        React.createElement("button", {
          type: "submit",
          style: { padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }
        }, "Create Choir")
      )
    );
  }
  
  // View choir
  if (params.id && !path.endsWith("/edit")) {
    const choir = await getChoir(params.id);
    
    if (!choir) {
      return React.createElement("div", { style: { padding: "2rem" } },
        React.createElement("h1", null, "Choir Not Found")
      );
    }
    
    const isOwner = user === choir.owner;
    
    return React.createElement("div", { style: { padding: "2rem" } },
      React.createElement("h1", null, choir.title),
      React.createElement("p", { style: { color: "#666" } }, `Choir ID: ${choir.id}`),
      React.createElement("p", { style: { color: "#666" } }, `Owner: ${choir.owner || "Unknown"}`),
      React.createElement("p", { style: { color: "#666" } }, `Songbooks: ${choir.counter}`),
      
      isOwner ? React.createElement("div", { style: { marginTop: "1rem", marginBottom: "1rem", display: "flex", gap: "0.5rem" } },
        React.createElement("a", {
          href: `/choir/${choir.id}/edit`,
          style: {
            padding: "0.5rem 1rem",
            backgroundColor: "#ffc107",
            color: "black",
            textDecoration: "none",
            borderRadius: "4px"
          }
        }, "Edit Choir"),
        React.createElement("a", {
          href: `/sb/new?choir=${choir.id}`,
          style: {
            padding: "0.5rem 1rem",
            backgroundColor: "#007bff",
            color: "white",
            textDecoration: "none",
            borderRadius: "4px"
          }
        }, "Create Songbook")
      ) : null,
      
      React.createElement("h3", { style: { marginTop: "2rem" } }, "Song Formats"),
      React.createElement("pre", { style: { backgroundColor: "#f5f5f5", padding: "1rem", borderRadius: "4px" } },
        choir.formats.join("\n")
      ),
      
      React.createElement("div", { style: { marginTop: "2rem" } },
        React.createElement("a", { href: "/choir" }, "← Back to Choirs")
      )
    );
  }
  
  // Edit choir
  if (params.id && path.endsWith("/edit")) {
    const choir = await getChoir(params.id);
    
    if (!choir) {
      return React.createElement("div", { style: { padding: "2rem" } },
        React.createElement("h1", null, "Choir Not Found")
      );
    }
    
    if (user !== choir.owner) {
      return React.createElement("div", { style: { padding: "2rem" } },
        React.createElement("h1", null, "Permission Denied"),
        React.createElement("p", null, "You don't own this choir.")
      );
    }
    
    return React.createElement("div", { style: { padding: "2rem" } },
      React.createElement("h1", null, `Edit ${choir.title}`),
      React.createElement("form", {
        method: "POST",
        action: `/api/choir/${choir.id}/edit`,
        encType: "multipart/form-data",
        style: { display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "500px" }
      },
        React.createElement("label", null,
          "Choir Name:",
          React.createElement("input", {
            type: "text",
            name: "title",
            defaultValue: choir.title,
            required: true,
            style: { display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }
          })
        ),
        React.createElement("label", null,
          "Song Formats (one per line):",
          React.createElement("textarea", {
            name: "format",
            rows: 10,
            defaultValue: choir.formats.join("\n"),
            style: { display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem", fontFamily: "monospace" }
          })
        ),
        React.createElement("div", { style: { display: "flex", gap: "0.5rem" } },
          React.createElement("button", {
            type: "submit",
            style: { padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }
          }, "Save Changes"),
          React.createElement("a", {
            href: `/choir/${choir.id}`,
            style: { padding: "0.5rem 1rem", backgroundColor: "#6c757d", color: "white", textDecoration: "none", borderRadius: "4px", display: "inline-block" }
          }, "Cancel")
        )
      )
    );
  }
  
  return null;
}
