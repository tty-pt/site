import { useEffect, useState, useRef } from "preact/hooks";
import { IS_BROWSER } from "$fresh/runtime.ts";

interface TransposeFormProps {
  id: string;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
  showMedia?: boolean;
  yt?: string | null;
  audio?: string | null;
  pdf?: string | null;
}

export default function TransposeForm({
  id,
  transpose: initialTranspose,
  useBemol: initialBemol,
  useLatin: initialLatin,
  showMedia: initialShowMedia = false,
  yt,
  audio,
  pdf
}: TransposeFormProps) {
  const [transpose, setTranspose] = useState(initialTranspose);
  const [useBemol, setUseBemol] = useState(initialBemol);
  const [useLatin, setUseLatin] = useState(initialLatin);
  const [showMedia, setShowMedia] = useState(initialShowMedia);

  const isMounted = useRef(false);

  useEffect(() => {
    if (!IS_BROWSER) return;
    if (!isMounted.current) {
      isMounted.current = true;
      return;
    }

    const submit = async () => {
      const params = new URLSearchParams();
      params.set('t', transpose.toString());
      if (useBemol) params.append('b', '1');
      if (useLatin) params.append('l', '1');
      if (showMedia) params.append('m', '1');

      const res = await fetch(`/api/song/${id}/transpose?${params.toString()}`);
      if (!res.ok) {
        console.error('API failed, not submitting form');
        return;
      }

      const { data, showMedia: showMediaResponse } = await res.json();
      const chordData = document.getElementById('chord-data');
      if (chordData) chordData.textContent = data;

      const mediaContainer = document.getElementById('media-container');
      if (mediaContainer) {
        mediaContainer.classList.toggle('hidden', !showMediaResponse);
      }

      const url = new URL(window.location.href);
      url.search = params.toString();
      window.history.pushState({}, '', url.toString());
    };

    submit();
  }, [transpose, useBemol, useLatin, showMedia]);

  const transposeOptions = [];
  for (let i = -11; i <= 11; i++) {
    transposeOptions.push(
      <option key={i} value={i}>
        {i === 0 ? "Original" : (i > 0 ? `+${i}` : i)}
      </option>
    );
  }

  return (
    <form id="transpose-form" method="get" action={`/song/${id}`}>
      <label>
        Transpose:
        <select
          name="t"
          value={transpose}
          onChange={(e) => setTranspose(parseInt((e.target as HTMLSelectElement).value, 10))}
        >
          {transposeOptions}
        </select>
      </label>

      <label>
        <input
          type="checkbox"
          name="b"
          value="1"
          checked={useBemol}
          onChange={(e) => setUseBemol((e.target as HTMLInputElement).checked)}
        />
        <span>Flats (♭)</span>
      </label>

      <label>
        <input
          type="checkbox"
          name="l"
          value="1"
          checked={useLatin}
          onChange={(e) => setUseLatin((e.target as HTMLInputElement).checked)}
        />
        <span>Latin</span>
      </label>

      {(yt || audio || pdf) && (
        <label>
          <input
            type="checkbox"
            name="m"
            value="1"
            checked={showMedia}
            onChange={(e) => setShowMedia((e.target as HTMLInputElement).checked)}
          />
          <span>▶️ Video</span>
        </label>
      )}

      {!IS_BROWSER && <button type="submit" className="btn">Apply</button>}
    </form>
  );
}
