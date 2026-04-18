import { useState, useEffect } from "preact/hooks";
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

export default function SongItem(props: Props) {
  const [transpose, setTranspose] = useState(props.transpose);
  const [useBemol, setUseBemol] = useState(false);
  const [useLatin, setUseLatin] = useState(false);
  const [chordHtml, setChordHtml] = useState(props.chordData);

  const originalKey = props.originalKey || 0;
  const keys = getKeyNames(useBemol, useLatin);
  const targetIndex = (originalKey + transpose + 12) % 12;
  const targetKey = keys[targetIndex];

  useEffect(() => {
    if (!IS_BROWSER) return;

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
      <div style={{ padding: "1rem", backgroundColor: "#f5f5f5", borderRadius: "4px" }}>
        <h3>{format.toUpperCase()}</h3>
      </div>
    );
  }

  return (
    <div style={{ marginTop: "2rem", borderTop: "2px solid #ddd", paddingTop: "1rem" }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <div>
          <h4 style={{ margin: 0 }}>
            <a href={`/song/${props.chordId}`} target="_blank">{props.chordTitle || ""}</a>
          </h4>
          <p style={{ margin: "0.25rem 0", fontSize: "0.9rem", color: "#666" }}>
            {format} • Key: {targetKey}
          </p>
        </div>

        <div style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}>
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
                style={{ padding: "0.25rem" }}
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
                <button type="submit" style={{ padding: "0.25rem 0.5rem" }}>🎲</button>
              </form>
            )}
          </div>
      </div>

      <pre
        dangerouslySetInnerHTML={{ __html: chordHtml || "" }}
        style={{
          fontFamily: "monospace",
          fontSize: "0.9rem",
          whiteSpace: "pre-wrap",
          backgroundColor: "#f9f9f9",
          padding: "1rem",
          borderRadius: "4px",
          marginTop: "0.5rem",
        }}
      />
    </div>
  );
}
