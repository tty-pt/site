import React from "https://esm.sh/react@18";
import { Layout } from "../../../ssr/ui.tsx";

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
  
  return (
    <Layout user={user} title={`chords: ${displayTitle}`} path={path}>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: "1rem" }}>
        <h2>{displayTitle}</h2>
        
        {/* Transpose Controls */}
        <form method="get" action={`/chords/${id}`} style={{ display: "flex", gap: "1rem", flexWrap: "wrap", alignItems: "center" }}>
          <label>
            Transpose:
            <select name="t" style={{ marginLeft: "0.5rem" }}>
              {transposeOptions}
            </select>
          </label>
          
          <label style={{ display: "flex", alignItems: "center", gap: "0.3rem" }}>
            <input type="checkbox" name="b" value="1" checked={useBemol} />
            Use Flats (♭)
          </label>
          
          <label style={{ display: "flex", alignItems: "center", gap: "0.3rem" }}>
            <input type="checkbox" name="l" value="1" checked={useLatin} />
            Latin (Do-Ré-Mi)
          </label>
          
          <button type="submit" className="btn">Apply</button>
        </form>
        
        {/* Chord Chart */}
        <pre style={{ whiteSpace: "pre-wrap", fontFamily: "monospace", padding: "1rem", background: "#f5f5f5", borderRadius: "4px", width: "100%", maxWidth: "600px" }}>
          {data}
        </pre>
        
        <a href="/chords/" className="btn">
          Back to Chords
        </a>
      </div>
    </Layout>
  );
}
