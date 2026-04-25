import { useEffect, useState, useRef } from "preact/hooks";
import { IS_BROWSER } from "$fresh/runtime.ts";
import { Layout, ItemMenu } from "@/ssr/ui.tsx";
import { getKeyNames } from "@/ssr/keys.ts";

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

interface SongFlags {
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia: boolean;
}

function buildSongTransposeParams(flags: SongFlags): URLSearchParams {
  const params = new URLSearchParams();
  params.set("t", flags.transpose.toString());
  params.set("h", "1");
  if (flags.useBemol) params.append("b", "1");
  if (flags.useLatin) params.append("l", "1");
  if (flags.showMedia) params.append("m", "1");
  return params;
}

function SongTransposeMenu({
  path,
  originalKey,
  transpose,
  useBemol,
  useLatin,
  showMedia,
  onTransposeChange,
  onUseBemolChange,
  onUseLatinChange,
  onShowMediaChange,
}: {
  path: string;
  originalKey: number;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia: boolean;
  onTransposeChange: (value: number) => void;
  onUseBemolChange: (value: boolean) => void;
  onUseLatinChange: (value: boolean) => void;
  onShowMediaChange: (value: boolean) => void;
}) {
  const keys = getKeyNames(useBemol, useLatin);

  return (
    <form id="transpose-form" method="get" action={path}>
      <label>
        Key:
        <select
          name="t"
          value={transpose}
          onChange={(e) => onTransposeChange(parseInt(e.currentTarget.value, 10))}
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
          onChange={(e) => onUseBemolChange(e.currentTarget.checked)}
        />
        <span>Flats (♭)</span>
      </label>

      <label>
        <input
          type="checkbox"
          name="l"
          value="1"
          checked={useLatin}
          onChange={(e) => onUseLatinChange(e.currentTarget.checked)}
        />
        <span>Latin</span>
      </label>

      <label>
        <input
          type="checkbox"
          name="m"
          value="1"
          checked={showMedia}
          onChange={(e) => onShowMediaChange(e.currentTarget.checked)}
        />
        <span>▶️ Video</span>
      </label>

      {!IS_BROWSER && <button type="submit" className="btn">Apply</button>}
    </form>
  );
}

function SongMedia({
  yt,
  audio,
  pdf,
  showMedia,
}: {
  yt?: string | null;
  audio?: string | null;
  pdf?: string | null;
  showMedia: boolean;
}) {
  if (!yt && !audio && !pdf) return null;

  return (
    <div
      id="media-container"
      class={showMedia ? "flex flex-col gap-4 w-full max-w-xl" : "hidden w-full max-w-xl"}
    >
      {yt && (
        <iframe
          src={`https://www.youtube.com/embed/${yt}`}
          className="w-full aspect-video border-none"
          allowFullScreen
        />
      )}

      {audio && (
        <audio controls className="w-full">
          <source src={audio} type="audio/mpeg" />
        </audio>
      )}

      {pdf && (
        <a href={pdf} target="_blank" rel="noopener" className="text-blue-600">
          📄 View PDF
        </a>
      )}
    </div>
  );
}

function SongMeta({
  categories,
  author,
}: {
  categories?: string | null;
  author?: string | null;
}) {
  if (!categories && !author) return null;

  return (
    <div className="flex justify-between items-start w-full max-w-xl text-xs text-muted">
      <div className="italic whitespace-pre-wrap">
        {categories || ""}
      </div>
      <div className="text-right">
        {author || "N/A"}
      </div>
    </div>
  );
}

function SongChordData({ chordHtml }: { chordHtml: string }) {
  return (
    <div
      id="chord-data"
      dangerouslySetInnerHTML={{ __html: chordHtml }}
      className="whitespace-pre-wrap font-mono p-4 rounded w-full max-w-xl chord-data"
    />
  );
}

export default function SongDetailIsland(props: SongDetailProps) {
  const [transpose, setTranspose] = useState(props.transpose);
  const [useBemol, setUseBemol] = useState(props.useBemol);
  const [useLatin, setUseLatin] = useState(props.useLatin);
  const [showMedia, setShowMedia] = useState(props.showMedia);
  const [chordHtml, setChordHtml] = useState(props.data);

  const originalKey = props.originalKey || 0;
  const isMounted = useRef(false);
  const displayTitle = props.title || props.id;

  useEffect(() => {
    if (!IS_BROWSER) return;
    if (!isMounted.current) {
      isMounted.current = true;
      return;
    }

    const updateChords = async () => {
      const params = buildSongTransposeParams({
        transpose,
        useBemol,
        useLatin,
        showMedia,
      });

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
  }, [transpose, useBemol, useLatin, showMedia, props.id]);

  const menuItems = (
    <>
      <SongTransposeMenu
        path={props.path}
        originalKey={originalKey}
        transpose={transpose}
        useBemol={useBemol}
        useLatin={useLatin}
        showMedia={showMedia}
        onTransposeChange={setTranspose}
        onUseBemolChange={setUseBemol}
        onUseLatinChange={setUseLatin}
        onShowMediaChange={setShowMedia}
      />
      <ItemMenu module="song" id={props.id} isOwner={!!props.owner} />
    </>
  );

  return (
    <Layout user={props.user} title={`song: ${displayTitle}`} path={props.path} icon="🎸" menuItems={menuItems}>
      <div className="flex flex-col items-center gap-4">
        <SongMedia yt={props.yt} audio={props.audio} pdf={props.pdf} showMedia={showMedia} />
        <SongMeta categories={props.categories} author={props.author} />
        <SongChordData chordHtml={chordHtml} />
      </div>
    </Layout>
  );
}
