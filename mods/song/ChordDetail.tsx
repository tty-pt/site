import { Layout } from "@/ssr/ui.tsx";

export default function ChordDetail({ 
  user, 
  path, 
  id, 
  data, 
  title,
  yt,
  audio,
  pdf,
  transpose = 0,
  useBemol = false,
  useLatin = false,
  showMedia = false,
  transposeForm,
}: { 
  user: string | null; 
  path: string; 
  id: string; 
  data: string;
  title: string | null;
  yt?: string | null;
  audio?: string | null;
  pdf?: string | null;
  transpose?: number;
  useBemol?: boolean;
  useLatin?: boolean;
  showMedia?: boolean;
  transposeForm?: any;
}) {
  const displayTitle = title || id;
  
  return (
    <Layout user={user} title={`song: ${displayTitle}`} path={path} menuItems={transposeForm}>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: "1rem" }}>
        {/* Media container - shown based on showMedia prop */}
        {(yt || audio || pdf) && (
          <div 
            id="media-container"
            class={showMedia ? '' : 'hidden'}
            style={{ width: "100%", maxWidth: "600px" }}
          >
            {/* YouTube Video */}
            {yt && (
              <div style={{ marginBottom: "1rem" }}>
                <iframe 
                  src={`https://www.youtube.com/embed/${yt}`}
                  style={{ width: "100%", aspectRatio: "16/9", border: "none" }}
                  allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture"
                  allowFullScreen
                />
              </div>
            )}
            
            {/* Audio Player */}
            {audio && (
              <div style={{ marginBottom: "1rem" }}>
                <audio controls style={{ width: "100%" }}>
                  <source src={audio} type="audio/mpeg" />
                  Your browser does not support audio.
                </audio>
              </div>
            )}
            
            {/* PDF Link */}
            {pdf && (
              <div style={{ marginBottom: "1rem" }}>
                <a href={pdf} target="_blank" rel="noopener" style={{ color: "#007bff" }}>
                  📄 View PDF
                </a>
              </div>
            )}
          </div>
        )}
        
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
