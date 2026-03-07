import { Layout } from "../ssr/ui.tsx";

export default function ChordDetail({ 
  user, 
  path, 
  id, 
  data, 
  title,
  transpose = 0,
  useBemol = false,
  useLatin = false
}: { 
  user: string | null; 
  path: string; 
  id: string; 
  data: string;
  title: string | null;
  transpose?: number;
  useBemol?: boolean;
  useLatin?: boolean;
}) {
  const displayTitle = title || id;
  
  const transposeOptions = [];
  for (let i = -11; i <= 11; i++) {
    transposeOptions.push(
      <option key={i} value={i} selected={i === transpose}>
        {i === 0 ? "Original" : (i > 0 ? `+${i}` : i)}
      </option>
    );
  }
  
  // Transpose controls form for the menu
  const transposeControls = (
    <form id="transpose-form" method="get" action={`/song/${id}`}>
      <label>
        Transpose:
        <select name="t">
          {transposeOptions}
        </select>
      </label>
      
      <label>
        <input type="checkbox" name="b" value="1" checked={useBemol} />
        <span>Flats (♭)</span>
      </label>
      
      <label>
        <input type="checkbox" name="l" value="1" checked={useLatin} />
        <span>Latin</span>
      </label>
      
      <button type="submit" className="btn">Apply</button>
    </form>
  );
  
  return (
    <Layout user={user} title={`song: ${displayTitle}`} path={path} menuItems={transposeControls}>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: "1rem" }}>
        <h2>{displayTitle}</h2>
        
        {/* Chord Chart */}
        <pre id="chord-data" style={{ whiteSpace: "pre-wrap", fontFamily: "monospace", padding: "1rem", background: "#f5f5f5", borderRadius: "4px", width: "100%", maxWidth: "600px" }}>
          {data}
        </pre>
        
        <a href="/song/" className="btn">
          Back to Chords
        </a>
      </div>
    </Layout>
  );
}
