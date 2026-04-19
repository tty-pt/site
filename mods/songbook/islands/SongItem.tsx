import { useState, useEffect, useRef } from "preact/hooks";
import { getKeyNames } from "@/ssr/keys.ts";
import { IS_BROWSER } from "$fresh/runtime.ts";

interface Props {
  chordId: string;
  transpose: number;
  format: string;
  chordTitle: string;
  chordData: string;
  originalKey: number;
  isOwner: boolean;
  songbookId: string;
  index: number;
}



export default function SongItem(props: Props) {
  const [transpose, setTranspose] = useState(props.transpose);
  const [useBemol, setUseBemol] = useState(false);
  const [useLatin, setUseLatin] = useState(false);
  const [chordHtml, setChordHtml] = useState(props.chordData);

  const originalKey = props.originalKey || 0;
  const keys = getKeyNames(useBemol, useLatin);
  const targetIndex = (originalKey + transpose + 12) % 12;
  const targetKey = keys[targetIndex];

  const isMounted = useRef(false);

  useEffect(() => {
    if (!IS_BROWSER) return;
    if (!isMounted.current) { isMounted.current = true; return; }

    const updateChords = async () => {
      try {
        const params = new URLSearchParams();
        params.set('t', transpose.toString());
        params.set('h', '1');

        const res = await fetch(`/api/song/${props.chordId}/transpose?${params.toString()}`);
        if (!res.ok) return;

        const json = await res.json();
        setChordHtml(json.data);
      } catch (err) {
        console.error("Transposition failed:", err);
      }
    };

    updateChords();
  }, [transpose, props.chordId]);

  const format = props.format || "";

  if (!props.chordId) {
    return (
      <div className="p-4 bg-surface rounded">
        <h3>{format.toUpperCase()}</h3>
      </div>
    );
  }

  return (
    <div className="separator flex flex-col gap-2">
      <div className="flex justify-between items-center">
        <div className="flex flex-col gap-1">
          <h4 className="m-0">
            <a href={`/song/${props.chordId}`} target="_blank">{props.chordTitle || ""}</a>
          </h4>
          <p className="text-sm text-muted">
            {format} • Key: {targetKey}
          </p>
        </div>

        <div className="flex gap-2 items-center">
            {props.isOwner && (
              <select
                name="t"
                onChange={async (e) => {
                  const semitones = parseInt(e.currentTarget.value, 10);
                  setTranspose(semitones);

                  // Fire-and-forget POST to persist the transpose value
                  const formData = new FormData();
                  formData.append('n', props.index.toString());
                  formData.append('t', semitones.toString());
                  fetch(`/songbook/${props.songbookId}/transpose`, {
                    method: "POST",
                    body: formData,
                  }).catch(console.error);
                }}
                value={transpose}
                className="p-1"
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
            )}

            {props.isOwner && (
              <form method="POST" action={`/songbook/${props.songbookId}/randomize`} encType="multipart/form-data">
                <input type="hidden" name="n" value={`${props.index}`} />
                <button type="submit" className="btn py-1 px-2">🎲</button>
              </form>
            )}
          </div>
      </div>

      <pre
        dangerouslySetInnerHTML={{ __html: chordHtml || "" }}
        className="font-mono text-sm whitespace-pre-wrap bg-surface p-4 rounded"
      />
    </div>
  );
}
