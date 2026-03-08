import { useEffect } from "preact/hooks";
import { IS_BROWSER } from "$fresh/runtime.ts";

interface TransposeFormProps {
  id: string;
  transpose: number;
  useBemol: boolean;
  useLatin: boolean;
}

/**
 * Transpose form with progressive enhancement
 * 
 * SSR: Form renders server-side with all controls (works without JS)
 * Client: Enhanced with auto-submit, AJAX updates, URL management
 * 
 * Features:
 *   - Dropdown for semitone transpose (-11 to +11)
 *   - Checkbox for flat notation (♭)
 *   - Checkbox for latin notation (Do/Re/Mi)
 *   - Apply button (hidden when JS enabled)
 *   - Auto-submit on control change
 *   - AJAX form submission (no page reload)
 *   - Updates URL with pushState
 *   - Handles browser back/forward
 */
export default function TransposeForm({ 
  id, 
  transpose, 
  useBemol, 
  useLatin 
}: TransposeFormProps) {
  
  // Client-side enhancement
  useEffect(() => {
    if (!IS_BROWSER) return;
    
    const form = document.getElementById('transpose-form') as HTMLFormElement | null;
    const chordData = document.getElementById('chord-data') as HTMLPreElement | null;
    
    if (!form || !chordData) return;
    
    const submitButton = form.querySelector('button[type="submit"]') as HTMLButtonElement | null;
    
    // Hide Apply button when JavaScript is enabled (progressive enhancement)
    if (submitButton) {
      submitButton.style.display = 'none';
    }
    
    const handleSubmit = async (e: Event) => {
      e.preventDefault();
      
      // Get form data
      const formData = new FormData(form);
      const params = new URLSearchParams(formData as any);
      
      // Build fetch URL
      const fetchUrl = `/song/${id}?${params}`;
      
      try {
        // Add loading state
        chordData.style.opacity = '0.5';
        if (submitButton) {
          submitButton.disabled = true;
        }
        
        // Fetch the new page
        const response = await fetch(fetchUrl);
        if (!response.ok) {
          throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        const html = await response.text();
        
        // Parse the response to extract the chord data
        const parser = new DOMParser();
        const doc = parser.parseFromString(html, 'text/html');
        const newChordData = doc.getElementById('chord-data') as HTMLPreElement | null;
        
        if (newChordData) {
          // Update the chord display
          chordData.textContent = newChordData.textContent;
          
          // Update the browser URL without reload
          window.history.pushState(
            { id, params: params.toString() },
            '',
            fetchUrl
          );
          
          // Update the page title if needed
          const newTitle = doc.querySelector('h2');
          const currentTitle = document.querySelector('h2');
          if (newTitle && currentTitle) {
            currentTitle.textContent = newTitle.textContent;
          }
        } else {
          throw new Error('Could not find chord data in response');
        }
        
      } catch (error) {
        console.error('Error fetching transposed content:', error);
        // Fall back to normal form submission on error
        form.submit();
      } finally {
        // Remove loading state
        chordData.style.opacity = '1';
        if (submitButton) {
          submitButton.disabled = false;
        }
      }
    };
    
    // Auto-submit on any control change
    const handleChange = () => {
      form.requestSubmit(); // Triggers handleSubmit
    };
    
    // Attach change listeners to all form controls
    const controls = form.querySelectorAll('select, input[type="checkbox"]');
    controls.forEach(control => {
      control.addEventListener('change', handleChange);
    });
    
    // Attach submit handler
    form.addEventListener('submit', handleSubmit);
    
    // Handle browser back/forward buttons
    const handlePopState = (_e: PopStateEvent) => {
      // User navigated back/forward, reload the page to get correct state
      window.location.reload();
    };
    
    window.addEventListener('popstate', handlePopState);
    
    // Cleanup
    return () => {
      controls.forEach(control => {
        control.removeEventListener('change', handleChange);
      });
      form.removeEventListener('submit', handleSubmit);
      window.removeEventListener('popstate', handlePopState);
    };
  }, [id]);
  
  // Build transpose options
  const transposeOptions = [];
  for (let i = -11; i <= 11; i++) {
    transposeOptions.push(
      <option key={i} value={i} selected={i === transpose}>
        {i === 0 ? "Original" : (i > 0 ? `+${i}` : i)}
      </option>
    );
  }
  
  // Render form (SSR + client hydration)
  return (
    <form id="transpose-form" method="get" action={`/song/${id}`}>
      <label>
        Transpose:
        <select name="t">
          {transposeOptions}
        </select>
      </label>
      
      <label>
        <input type="checkbox" name="b" value="1" checked={useBemol} />
        <span>Flats (♭)</span>
      </label>
      
      <label>
        <input type="checkbox" name="l" value="1" checked={useLatin} />
        <span>Latin</span>
      </label>
      
      <button type="submit" className="btn">Apply</button>
    </form>
  );
}
