import IndexAdd from "@/index/IndexAdd.tsx";
import IndexList from "@/index/IndexList.tsx";

const repoRoot = Deno.env.get("REPO_ROOT") || "/home/quirinpa/site";

export const routes = [
  "/choir",
  "/choir/add",
  "/choir/:id",
  "/choir/:id/edit"
];

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

export async function render({ user, path, params, body }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
  body?: string | null;
}) {
  if (path === "/choir/add")
    return IndexAdd({
      user,
      module: "choir",
    });

  if (path === "/choir")
    return IndexList({
      module: "choir",
      body: body || null,
    });

  // Create new choir form
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
