const repoRoot = Deno.env.get("REPO_ROOT") || "/home/quirinpa/site";

export const routes = ["/sb", "/sb/new", "/sb/:id", "/sb/:id/edit"];

// Read songbook index database
async function getSongbooks() {
  const indexPath = `${repoRoot}/items/sb/items/index.db`;
  const songbooks: Array<{ id: string; title: string }> = [];
  
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
        songbooks.push({ id, title });
      }
    }
  } catch {
    // Database doesn't exist yet
  }
  
  return songbooks;
}

// Read songbook details
async function getSongbook(id: string) {
  const sbPath = `${repoRoot}/items/sb/items/${id}`;
  
  try {
    const title = await Deno.readTextFile(`${sbPath}/title`);
    let owner = "";
    try {
      owner = (await Deno.readTextFile(`${sbPath}/.owner`)).trim();
    } catch {
      // No owner file
    }
    
    let choir = "";
    try {
      choir = (await Deno.readTextFile(`${sbPath}/choir`)).trim();
    } catch {
      // No choir file
    }
    
    const dataText = await Deno.readTextFile(`${sbPath}/data.txt`);
    const songs = dataText.trim().split("\n").map(line => {
      const parts = line.split(":");
      return {
        chordId: parts[0] || "",
        transpose: parseInt(parts[1] || "0", 10),
        format: parts[2] || "any"
      };
    });
    
    return { id, title: title.trim(), owner, choir, songs };
  } catch {
    return null;
  }
}

// Read choirs for dropdown
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
    // No choirs
  }
  
  return choirs;
}

// Get all chords for dropdown
async function getAllChords() {
  const chordsPath = `${repoRoot}/items/chords/items`;
  const chords: Array<{ id: string; title: string; type: string }> = [];
  
  try {
    for await (const entry of Deno.readDir(chordsPath)) {
      if (!entry.isDirectory || entry.name.startsWith(".")) continue;
      
      const chordPath = `${chordsPath}/${entry.name}`;
      let title = entry.name;
      try {
        title = (await Deno.readTextFile(`${chordPath}/title`)).trim();
      } catch {
        // No title file
      }
      
      let type = "";
      try {
        type = (await Deno.readTextFile(`${chordPath}/type`)).trim();
      } catch {
        // No type file
      }
      
      chords.push({ id: entry.name, title, type });
    }
  } catch {
    // No chords directory
  }
  
  return chords.sort((a, b) => a.title.localeCompare(b.title));
}

// Transpose a chord file
async function transposeChord(chordId: string, transpose: number): Promise<string> {
  if (!chordId || transpose === 0) {
    // Just read the file
    try {
      return await Deno.readTextFile(`${repoRoot}/items/chords/items/${chordId}/data.txt`);
    } catch {
      return "";
    }
  }
  
  // Use HTTP to call the transpose API
  try {
    const response = await fetch(`http://127.0.0.1:8080/api/chords/transpose?id=${chordId}&t=${transpose}`);
    if (response.ok) {
      const text = await response.text();
      // Remove the title line (first line)
      const lines = text.split("\n");
      return lines.slice(2).join("\n"); // Skip title and empty line
    }
  } catch (e) {
    console.error(`Failed to transpose ${chordId}:`, e);
  }
  
  // Fallback: return original
  try {
    return await Deno.readTextFile(`${repoRoot}/items/chords/items/${chordId}/data.txt`);
  } catch {
    return "";
  }
}

export async function render({ user, path, params }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
}) {
  // List all songbooks
  if (path === "/sb") {
    const songbooks = await getSongbooks();
    
    return (
      <div style={{ padding: "2rem" }}>
        <h1>Songbooks</h1>
        {user ? (
          <div style={{ marginBottom: "1rem" }}>
            <a
              href="/sb/new"
              style={{
                padding: "0.5rem 1rem",
                backgroundColor: "#007bff",
                color: "white",
                textDecoration: "none",
                borderRadius: "4px",
                display: "inline-block"
              }}
            >
              Create New Songbook
            </a>
          </div>
        ) : null}
        <div style={{ display: "flex", flexDirection: "column", gap: "1rem", marginTop: "1rem" }}>
          {songbooks.length === 0 ? (
            <p>No songbooks yet.</p>
          ) : (
            songbooks.map(sb => (
              <div
                key={sb.id}
                style={{
                  border: "1px solid #ccc",
                  padding: "1rem",
                  borderRadius: "4px"
                }}
              >
                <h3>
                  <a href={`/sb/${sb.id}`}>{sb.title}</a>
                </h3>
              </div>
            ))
          )}
        </div>
      </div>
    );
  }
  
  // Create new songbook form
  if (path === "/sb/new") {
    if (!user) {
      return (
        <div style={{ padding: "2rem" }}>
          <h1>Login Required</h1>
          <p>You must be logged in to create a songbook.</p>
        </div>
      );
    }
    
    const choirs = await getChoirs();
    const userChoirs = choirs; // TODO: filter by ownership when we have that info
    
    if (userChoirs.length === 0) {
      return (
        <div style={{ padding: "2rem" }}>
          <h1>No Choirs</h1>
          <p>You need to create a choir first.</p>
          <a href="/choir/new">Create Choir</a>
        </div>
      );
    }
    
    return (
      <div style={{ padding: "2rem" }}>
        <h1>Create New Songbook</h1>
        <form
          method="POST"
          action="/api/sb/create"
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "1rem", maxWidth: "500px" }}
        >
          <label>
            Songbook ID (no spaces):
            <input
              type="text"
              name="id"
              required
              pattern="[a-z0-9_-]+"
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          <label>
            Songbook Name:
            <input
              type="text"
              name="title"
              required
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            />
          </label>
          <label>
            Choir:
            <select
              name="choir"
              required
              style={{ display: "block", marginTop: "0.25rem", width: "100%", padding: "0.5rem" }}
            >
              {userChoirs.map(choir => (
                <option key={choir.id} value={choir.id}>{choir.title}</option>
              ))}
            </select>
          </label>
          <button
            type="submit"
            style={{ padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
          >
            Create Songbook
          </button>
        </form>
      </div>
    );
  }
  
  // View songbook
  if (params.id && !path.endsWith("/edit")) {
    const sb = await getSongbook(params.id);
    
    if (!sb) {
      return (
        <div style={{ padding: "2rem" }}>
          <h1>Songbook Not Found</h1>
        </div>
      );
    }
    
    const isOwner = user === sb.owner;
    
    // Render each song with transposed chords
    const songElements = [];
    for (let i = 0; i < sb.songs.length; i++) {
      const song = sb.songs[i];
      
      if (!song.chordId) {
        // Empty slot - show format header only
        songElements.push(
          <div
            key={i}
            id={`${i}`}
            style={{ marginTop: "2rem", padding: "1rem", backgroundColor: "#f5f5f5", borderRadius: "4px" }}
          >
            <h3>{song.format.toUpperCase()}</h3>
          </div>
        );
        continue;
      }
      
      // Get chord title
      let chordTitle = song.chordId;
      try {
        chordTitle = (await Deno.readTextFile(`${repoRoot}/items/chords/items/${song.chordId}/title`)).trim();
      } catch {
        // Use ID as title
      }
      
      // Get transposed chord data
      const transposedData = await transposeChord(song.chordId, song.transpose);
      
      songElements.push(
        <div
          key={i}
          id={`${i}`}
          style={{ marginTop: "2rem", borderTop: "2px solid #ddd", paddingTop: "1rem" }}
        >
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <div>
              <h4 style={{ margin: 0 }}>
                <a href={`/chords/${song.chordId}`} target="_blank">{chordTitle}</a>
              </h4>
              <p style={{ margin: "0.25rem 0", fontSize: "0.9rem", color: "#666" }}>
                {song.format} • Transpose: {song.transpose > 0 ? '+' : ''}{song.transpose}
              </p>
            </div>
            {isOwner ? (
              <div style={{ display: "flex", gap: "0.5rem" }}>
                <form
                  method="POST"
                  action={`/api/sb/${sb.id}/transpose`}
                  encType="multipart/form-data"
                  style={{ display: "inline" }}
                >
                  <input type="hidden" name="n" value={`${i}`} />
                  <select
                    name="t"
                    onChange="this.form.submit()"
                    defaultValue={`${song.transpose}`}
                    style={{ padding: "0.25rem" }}
                  >
                    {[-11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11].map(t => (
                      <option key={t} value={`${t}`}>{t > 0 ? '+' : ''}{t}</option>
                    ))}
                  </select>
                </form>
                <form
                  method="POST"
                  action={`/api/sb/${sb.id}/randomize`}
                  encType="multipart/form-data"
                  style={{ display: "inline" }}
                >
                  <input type="hidden" name="n" value={`${i}`} />
                  <button type="submit" style={{ padding: "0.25rem 0.5rem" }}>🎲</button>
                </form>
              </div>
            ) : null}
          </div>
          <pre
            style={{
              fontFamily: "monospace",
              fontSize: "0.9rem",
              whiteSpace: "pre-wrap",
              backgroundColor: "#f9f9f9",
              padding: "1rem",
              borderRadius: "4px",
              marginTop: "0.5rem"
            }}
          >
            {transposedData || "(No chord data)"}
          </pre>
        </div>
      );
    }
    
    return (
      <div style={{ padding: "2rem", maxWidth: "900px" }}>
        <h1>{sb.title}</h1>
        <p style={{ color: "#666" }}>Choir: {sb.choir}</p>
        
        {isOwner ? (
          <div style={{ marginTop: "1rem", marginBottom: "2rem" }}>
            <a
              href={`/sb/${sb.id}/edit`}
              style={{
                padding: "0.5rem 1rem",
                backgroundColor: "#ffc107",
                color: "black",
                textDecoration: "none",
                borderRadius: "4px",
                marginRight: "0.5rem"
              }}
            >
              Edit Songbook
            </a>
          </div>
        ) : null}
        
        {songElements}
        
        <div style={{ marginTop: "2rem" }}>
          <a href="/sb">← Back to Songbooks</a>
        </div>
      </div>
    );
  }
  
  // Edit songbook
  if (params.id && path.endsWith("/edit")) {
    const sb = await getSongbook(params.id);
    
    if (!sb) {
      return (
        <div style={{ padding: "2rem" }}>
          <h1>Songbook Not Found</h1>
        </div>
      );
    }
    
    if (user !== sb.owner) {
      return (
        <div style={{ padding: "2rem" }}>
          <h1>Permission Denied</h1>
        </div>
      );
    }
    
    const allChords = await getAllChords();
    
    const formRows = sb.songs.map((song, i) => (
      <div
        key={i}
        style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}
      >
        <label style={{ flex: "0 0 150px" }}>
          {i + 1}. Format:
          <input
            type="text"
            name={`fmt_${i}`}
            defaultValue={song.format}
            style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
          />
        </label>
        <label style={{ flex: 1 }}>
          Song:
          <select
            name={`song_${i}`}
            defaultValue={song.chordId}
            style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
          >
            <option value="">(Empty)</option>
            {allChords.map(chord => (
              <option key={chord.id} value={chord.id}>
                {chord.title}{chord.type ? ` [${chord.type}]` : ''}
              </option>
            ))}
          </select>
        </label>
        <label style={{ flex: "0 0 80px" }}>
          Transpose:
          <select
            name={`t_${i}`}
            defaultValue={`${song.transpose}`}
            style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
          >
            {[-11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11].map(t => (
              <option key={t} value={`${t}`}>{t > 0 ? '+' : ''}{t}</option>
            ))}
          </select>
        </label>
      </div>
    ));
    
    return (
      <div style={{ padding: "2rem" }}>
        <h1>Edit {sb.title}</h1>
        <form
          method="POST"
          action={`/api/sb/${sb.id}/edit`}
          encType="multipart/form-data"
          style={{ display: "flex", flexDirection: "column", gap: "0.5rem" }}
        >
          {formRows}
          <input
            type="hidden"
            name="amount"
            value={`${sb.songs.length}`}
          />
          <div style={{ marginTop: "1rem", display: "flex", gap: "0.5rem" }}>
            <button
              type="submit"
              style={{ padding: "0.5rem 1rem", backgroundColor: "#28a745", color: "white", border: "none", borderRadius: "4px", cursor: "pointer" }}
            >
              Save Changes
            </button>
            <a
              href={`/sb/${sb.id}`}
              style={{ padding: "0.5rem 1rem", backgroundColor: "#6c757d", color: "white", textDecoration: "none", borderRadius: "4px", display: "inline-block" }}
            >
              Cancel
            </a>
          </div>
        </form>
      </div>
    );
  }
  
  return null;
}
