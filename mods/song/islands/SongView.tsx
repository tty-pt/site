import { useEffect, useState, useRef } from "preact/hooks";
import { IS_BROWSER } from "$fresh/runtime.ts";
import { Layout, ItemMenu } from "@/ssr/ui.tsx";

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
  originalKey: number;
  owner?: boolean;
  categories?: string | null;
  author?: string | null;
}

const KEY_NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const KEY_NAMES_FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
const KEY_NAMES_LATIN_SHARP = ["Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"];
const KEY_NAMES_LATIN_FLAT = ["Do", "Reb", "Re", "Mib", "Mi", "Fa", "Solb", "Sol", "Lab", "La", "Sib", "Si"];

function getKeyNames(useBemol: boolean, useLatin: boolean): string[] {
  if (useLatin) {
    return useBemol ? KEY_NAMES_LATIN_FLAT : KEY_NAMES_LATIN_SHARP;
  }
  return useBemol ? KEY_NAMES_FLAT : KEY_NAMES_SHARP;
}

function getTargetKey(originalKey: number, semitones: number, useBemol: boolean, useLatin: boolean): string {
  const keys = getKeyNames(useBemol, useLatin);
  const targetIndex = (originalKey + semitones + 12) % 12;
  return keys[targetIndex];
}

export default function SongDetailIsland(props: SongDetailProps) {
  // 1. Unified State
  const [transpose, setTranspose] = useState(props.transpose);
  const [useBemol, setUseBemol] = useState(props.useBemol);
  const [useLatin, setUseLatin] = useState(props.useLatin);
  const [showMedia, setShowMedia] = useState(props.showMedia);
  const [chordHtml, setChordHtml] = useState(props.data);

  // Calculate target key from transpose
  const originalKey = props.originalKey || 0;
  const targetKey = getTargetKey(originalKey, transpose, useBemol, useLatin);
  const keys = getKeyNames(useBemol, useLatin);

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
        Key:
        <select
          name="t"
          value={transpose}
          onChange={(e) => {
            setTranspose(parseInt(e.currentTarget.value, 10));
          }}
        >
          {keys.map((key, i) => {
            const semitones = (i - originalKey + 12) % 12;
            return (
              <option key={semitones} value={semitones}>
                {key}{semitones === 0 ? " (Original)" : ""}
              </option>
            );
          })}
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

  const editLink = <ItemMenu module="song" id={props.id} isOwner={!!props.owner} />;

  return (
    <Layout user={props.user} title={`song: ${displayTitle}`} path={props.path} icon="🎸" menuItems={<>{transposeForm}{editLink}</>}>
      <div className="flex flex-col items-center gap-4">
        
        {/* Media container - reacts to showMedia state */}
        {(props.yt || props.audio || props.pdf) && (
          <div
            id="media-container"
            class={showMedia ? 'flex flex-col gap-4 w-full max-w-xl' : 'hidden w-full max-w-xl'}
          >
            {props.yt && (
              <iframe
                src={`https://www.youtube.com/embed/${props.yt}`}
                className="w-full aspect-video border-none"
                allowFullScreen
              />
            )}

            {props.audio && (
              <audio controls className="w-full">
                <source src={props.audio} type="audio/mpeg" />
              </audio>
            )}

            {props.pdf && (
              <a href={props.pdf} target="_blank" rel="noopener" className="text-blue-600">
                📄 View PDF
              </a>
            )}
          </div>
        )}

        {/* Categories + Author row */}
        {(props.categories || props.author) && (
          <div className="flex justify-between items-start w-full max-w-xl text-xs text-muted">
            <div className="italic whitespace-pre-wrap">
              {props.categories || ""}
            </div>
            <div className="text-right">
              {props.author || "N/A"}
            </div>
          </div>
        )}

        {/* The Live Data */}
        <div
          id="chord-data"
          dangerouslySetInnerHTML={{ __html: chordHtml }}
          className="whitespace-pre-wrap font-mono p-4 rounded w-full max-w-xl chord-data"
        />

      </div>
    </Layout>
  );
}
