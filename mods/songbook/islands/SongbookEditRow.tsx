import { useState } from "preact/hooks";
import { KEY_NAMES_SHARP as KEY_NAMES } from "@/ssr/keys.ts";

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
    <div className="flex gap-2 items-center">
      <datalist id={`types-${i}`}>
        {allTypes.map((t) => <option key={t} value={t} />)}
      </datalist>
      <datalist id={`songs-${i}`} key={`songs-${i}-${selectedFormat}`}>
        {filteredChords.map((c) => (
          <option key={c.id} value={`${c.title} [${c.id}]`} />
        ))}
      </datalist>

      <label className="w-[150px] shrink-0">
        {i + 1}. Format:
        <input
          list={`types-${i}`}
          name={`fmt_${i}`}
          value={selectedFormat}
          onInput={(e) => setSelectedFormat((e.target as HTMLInputElement).value)}
          className="w-full"
        />
      </label>

      <label className="flex-1">
        Song:
        <input
          list={`songs-${i}`}
          name={`song_${i}`}
          defaultValue={displayVal}
          className="w-full"
        />
      </label>

      <label className="w-20 shrink-0">
        Key:
        <select
          name={`key_${i}`}
          defaultValue={`${targetKey}`}
          className="w-full"
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
