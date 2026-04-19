import { useState } from "preact/hooks";

interface Chord {
  id: string;
  title: string;
  type: string;
}

interface Props {
  index: number;
  chordId: string;
  transpose: number;
  format: string;
  originalKey: number;
  allChords: Chord[];
  allTypes: string[];
}

const KEY_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

export default function SongbookEditRow(props: Props) {
  const { index: i, allChords, allTypes } = props;
  const [selectedFormat, setSelectedFormat] = useState(props.format);

  const filteredChords = selectedFormat && allChords.some((c) => c.type === selectedFormat)
    ? allChords.filter((c) => c.type === selectedFormat)
    : allChords;

  const match = allChords.find((c) => c.id === props.chordId);
  const displayVal = match
    ? `${match.title} [${props.chordId}]`
    : props.chordId
    ? `${props.chordId} [${props.chordId}]`
    : "";

  const targetKey = (props.originalKey + props.transpose) % 12;

  return (
    <div style={{ display: "flex", gap: "0.5rem", alignItems: "center" }}>
      <datalist id={`types-${i}`}>
        {allTypes.map((t) => <option key={t} value={t} />)}
      </datalist>
      <datalist id={`songs-${i}`} key={`songs-${i}-${selectedFormat}`}>
        {filteredChords.map((c) => (
          <option key={c.id} value={`${c.title} [${c.id}]`} />
        ))}
      </datalist>

      <label style={{ flex: "0 0 150px" }}>
        {i + 1}. Format:
        <input
          list={`types-${i}`}
          name={`fmt_${i}`}
          value={selectedFormat}
          onInput={(e) => setSelectedFormat((e.target as HTMLInputElement).value)}
          style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
        />
      </label>

      <label style={{ flex: 1 }}>
        Song:
        <input
          list={`songs-${i}`}
          name={`song_${i}`}
          defaultValue={displayVal}
          style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
        />
      </label>

      <label style={{ flex: "0 0 80px" }}>
        Key:
        <select
          name={`key_${i}`}
          defaultValue={`${targetKey}`}
          style={{ width: "100%", padding: "0.25rem", marginTop: "0.25rem" }}
        >
          {KEY_NAMES.map((name, idx) => (
            <option key={idx} value={`${idx}`}>{name}</option>
          ))}
        </select>
      </label>
      <input type="hidden" name={`orig_${i}`} value={`${props.originalKey}`} />
    </div>
  );
}
