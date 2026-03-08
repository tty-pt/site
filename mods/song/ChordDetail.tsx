import { Layout } from "../ssr/ui.tsx";

export default function ChordDetail({ 
  user, 
  path, 
  id, 
  data, 
  title,
  transpose = 0,
  useBemol = false,
  useLatin = false,
  transposeForm,
  customTransposeControls
}: { 
  user: string | null; 
  path: string; 
  id: string; 
  data: string;
  title: string | null;
  transpose?: number;
  useBemol?: boolean;
  useLatin?: boolean;
  transposeForm?: any;
  customTransposeControls?: any;
}) {
  const displayTitle = title || id;
  
  return (
    <Layout user={user} title={`song: ${displayTitle}`} path={path} menuItems={transposeForm}>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: "1rem" }}>
        {/* Show island controls in main content if provided */}
        {customTransposeControls && <div>{customTransposeControls}</div>}
        
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
