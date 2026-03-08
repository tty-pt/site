import { IS_BROWSER } from "$fresh/runtime.ts";

interface TransposeControlsProps {
  initialTranspose: number;
  useBemol: boolean;
  useLatin: boolean;
}

/**
 * Quick transpose controls - +1/-1 semitone buttons
 * 
 * Updates URL with query params and reloads page.
 * Server-side transposition is handled by C backend (libtransp.so).
 * 
 * Query params:
 *   - t=N  : transpose value
 *   - b=1  : flat notation
 *   - l=1  : latin notation
 */
export default function TransposeControls({ 
  initialTranspose,
  useBemol,
  useLatin
}: TransposeControlsProps) {
  const updateTranspose = (delta: number) => {
    if (!IS_BROWSER) return;
    
    const newValue = initialTranspose + delta;
    
    // Build URL with all params (t, b, l)
    const url = new URL(window.location.href);
    
    if (newValue === 0) {
      url.searchParams.delete("t");
    } else {
      url.searchParams.set("t", newValue.toString());
    }
    
    if (useBemol) {
      url.searchParams.set("b", "1");
    } else {
      url.searchParams.delete("b");
    }
    
    if (useLatin) {
      url.searchParams.set("l", "1");
    } else {
      url.searchParams.delete("l");
    }
    
    // Reload page - C backend will transpose server-side
    window.location.href = url.toString();
  };

  return (
    <div class="transpose-controls mb-4 flex items-center gap-2">
      <span class="text-sm font-medium">Transpose:</span>
      <button
        onClick={() => updateTranspose(-1)}
        class="px-3 py-1 bg-gray-200 hover:bg-gray-300 rounded text-sm"
        disabled={!IS_BROWSER}
      >
        -1
      </button>
      <span class="px-3 py-1 min-w-[3rem] text-center border border-gray-300 rounded text-sm">
        {initialTranspose === 0 ? "0" : (initialTranspose > 0 ? `+${initialTranspose}` : initialTranspose)}
      </span>
      <button
        onClick={() => updateTranspose(1)}
        class="px-3 py-1 bg-gray-200 hover:bg-gray-300 rounded text-sm"
        disabled={!IS_BROWSER}
      >
        +1
      </button>
    </div>
  );
}
