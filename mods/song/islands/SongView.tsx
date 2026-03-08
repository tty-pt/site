import { useEffect, useState, useRef } from "preact/hooks";
import { IS_BROWSER } from "$fresh/runtime.ts";
import { Layout } from "@/ssr/ui.tsx";

interface SongDetailProps {
  user: string | null;
  path: string;
  id: string;
  data: string;
  title: string | null;
  yt?: string | null;
  audio?: string | null;
  pdf?: string | null;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia: boolean;
}

export default function SongDetailIsland(props: SongDetailProps) {
  // 1. Unified State
  const [transpose, setTranspose] = useState(props.transpose);
  const [useBemol, setUseBemol] = useState(props.useBemol);
  const [useLatin, setUseLatin] = useState(props.useLatin);
  const [showMedia, setShowMedia] = useState(props.showMedia);
  const [chordHtml, setChordHtml] = useState(props.data);

  const isMounted = useRef(false);
  const displayTitle = props.title || props.id;

  // 2. Transposition Effect
  useEffect(() => {
    if (!IS_BROWSER) return;
    
    // Skip initial SSR render
    if (!isMounted.current) {
      isMounted.current = true;
      return;
    }

    const updateChords = async () => {
      const params = new URLSearchParams();
      params.set('t', transpose.toString());
      params.set('h', '1'); // Force HTML format from C backend
      if (useBemol) params.append('b', '1');
      if (useLatin) params.append('l', '1');
      if (showMedia) params.append('m', '1');

      try {
        const res = await fetch(`/api/song/${props.id}/transpose?${params.toString()}`);
        if (!res.ok) return;

        const json = await res.json();
        setChordHtml(json.data);

        // Sync URL for refreshes/bookmarks
        const url = new URL(window.location.href);
        url.search = params.toString();
        window.history.pushState({}, '', url.toString());
      } catch (err) {
        console.error("Transposition failed:", err);
      }
    };

    updateChords();
  }, [transpose, useBemol, useLatin, showMedia]);

  // 3. The Form (passed to Layout menuItems)
  // This is SSR-first: it works via GET if JS is disabled
  const transposeForm = (
    <form id="transpose-form" method="get" action={props.path}>
      <label>
        Transpose:
        <select
          name="t"
          value={transpose}
          onChange={(e) => setTranspose(parseInt(e.currentTarget.value, 10))}
        >
          {Array.from({ length: 23 }, (_, i) => i - 11).map(i => (
            <option key={i} value={i}>
              {i === 0 ? "Original" : (i > 0 ? `+${i}` : i)}
            </option>
          ))}
        </select>
      </label>

      <label>
        <input
          type="checkbox"
          name="b"
          value="1"
          checked={useBemol}
          onChange={(e) => setUseBemol(e.currentTarget.checked)}
        />
        <span>Flats (♭)</span>
      </label>

      <label>
        <input
          type="checkbox"
          name="l"
          value="1"
          checked={useLatin}
          onChange={(e) => setUseLatin(e.currentTarget.checked)}
        />
        <span>Latin</span>
      </label>

      <label>
        <input
          type="checkbox"
          name="m"
          value="1"
          checked={showMedia}
          onChange={(e) => setShowMedia(e.currentTarget.checked)}
        />
        <span>▶️ Video</span>
      </label>

      {!IS_BROWSER && <button type="submit" className="btn">Apply</button>}
    </form>
  );

  return (
    <Layout user={props.user} title={`song: ${displayTitle}`} path={props.path} menuItems={transposeForm}>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: "1rem" }}>
        
        {/* Media container - reacts to showMedia state */}
        {(props.yt || props.audio || props.pdf) && (
          <div 
            id="media-container"
            class={showMedia ? '' : 'hidden'}
            style={{ width: "100%", maxWidth: "600px" }}
          >
            {props.yt && (
              <div style={{ marginBottom: "1rem" }}>
                <iframe 
                  src={`https://www.youtube.com/embed/${props.yt}`}
                  style={{ width: "100%", aspectRatio: "16/9", border: "none" }}
                  allowFullScreen
                />
              </div>
            )}
            
            {props.audio && (
              <div style={{ marginBottom: "1rem" }}>
                <audio controls style={{ width: "100%" }}>
                  <source src={props.audio} type="audio/mpeg" />
                </audio>
              </div>
            )}
            
            {props.pdf && (
              <div style={{ marginBottom: "1rem" }}>
                <a href={props.pdf} target="_blank" rel="noopener" style={{ color: "#007bff" }}>
                  📄 View PDF
                </a>
              </div>
            )}
          </div>
        )}

        {/* The Live Data */}
        <div
          id="chord-data"
          dangerouslySetInnerHTML={{ __html: chordHtml }}
          style={{
            whiteSpace: "pre-wrap",
            fontFamily: "monospace",
            padding: "1rem",
            background: "#f5f5f5",
            borderRadius: "4px",
            width: "100%",
            maxWidth: "600px"
          }}
        />

        <a href="/song/" className="btn">
          Back to Chords
        </a>
      </div>
    </Layout>
  );
}
