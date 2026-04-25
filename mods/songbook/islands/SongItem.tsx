import { useEffect, useRef, useState } from "preact/hooks";
import type { ComponentChildren } from "preact";
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

function buildTransposeParams(transpose: number): URLSearchParams {
  const params = new URLSearchParams();
  params.set("t", transpose.toString());
  params.set("h", "1");
  return params;
}

function buildTransposeRequest(index: number, transpose: number): FormData {
  const formData = new FormData();
  formData.append("n", index.toString());
  formData.append("t", transpose.toString());
  return formData;
}

function SongItemFormatOnly({ format }: { format: string }) {
  return (
    <div className="p-4 bg-surface rounded">
      <h3>{format.toUpperCase()}</h3>
    </div>
  );
}

function SongItemHeader({
  chordId,
  chordTitle,
  format,
  targetKey,
  controls,
}: {
  chordId: string;
  chordTitle: string;
  format: string;
  targetKey: string;
  controls: ComponentChildren;
}) {
  return (
    <div className="flex justify-between items-center">
      <div className="flex flex-col gap-1">
        <h4 className="m-0">
          <a href={`/song/${chordId}`} target="_blank">{chordTitle}</a>
        </h4>
        <p className="text-sm text-muted">
          {format} • Key: {targetKey}
        </p>
      </div>
      <div className="flex gap-2 items-center">
        {controls}
      </div>
    </div>
  );
}

function SongItemControls({
  keys,
  originalKey,
  transpose,
  index,
  isOwner,
  songbookId,
  onTransposeChange,
}: {
  keys: string[];
  originalKey: number;
  transpose: number;
  index: number;
  isOwner: boolean;
  songbookId: string;
  onTransposeChange: (value: number) => void;
}) {
  if (!isOwner) return null;

  const handleTransposeChange = async (value: number) => {
    onTransposeChange(value);

    try {
      await fetch(`/songbook/${songbookId}/transpose`, {
        method: "POST",
        body: buildTransposeRequest(index, value),
      });
    } catch (err) {
      console.error(err);
    }
  };

  return (
    <>
      <select
        name="t"
        onChange={(e) => handleTransposeChange(parseInt(e.currentTarget.value, 10))}
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

      <form method="POST" action={`/songbook/${songbookId}/randomize`} encType="multipart/form-data">
        <input type="hidden" name="n" value={`${index}`} />
        <button type="submit" className="btn py-1 px-2">🎲</button>
      </form>
    </>
  );
}

export default function SongItem(props: Props) {
  const [transpose, setTranspose] = useState(props.transpose);
  const [chordHtml, setChordHtml] = useState(props.chordData);
  const isMounted = useRef(false);

  const originalKey = props.originalKey || 0;
  const keys = getKeyNames(false, false);
  const targetIndex = (originalKey + transpose + 12) % 12;
  const targetKey = keys[targetIndex];
  const format = props.format || "";

  useEffect(() => {
    if (!IS_BROWSER || !props.chordId) return;
    if (!isMounted.current) {
      isMounted.current = true;
      return;
    }

    const updateChords = async () => {
      try {
        const res = await fetch(`/api/song/${props.chordId}/transpose?${buildTransposeParams(transpose).toString()}`);
        if (!res.ok) return;

        const json = await res.json();
        setChordHtml(json.data);
      } catch (err) {
        console.error("Transposition failed:", err);
      }
    };

    updateChords();
  }, [transpose, props.chordId]);

  if (!props.chordId) {
    return <SongItemFormatOnly format={format} />;
  }

  return (
    <div className="separator flex flex-col gap-2">
      <SongItemHeader
        chordId={props.chordId}
        chordTitle={props.chordTitle || ""}
        format={format}
        targetKey={targetKey}
        controls={
          <SongItemControls
            keys={keys}
            originalKey={originalKey}
            transpose={transpose}
            index={props.index}
            isOwner={props.isOwner}
            songbookId={props.songbookId}
            onTransposeChange={setTranspose}
          />
        }
      />

      <pre
        dangerouslySetInnerHTML={{ __html: chordHtml || "" }}
        className="font-mono text-sm whitespace-pre-wrap bg-surface p-4 rounded"
      />
    </div>
  );
}
