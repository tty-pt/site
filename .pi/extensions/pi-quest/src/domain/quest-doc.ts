// HIGH_LEVEL: #validation — the quest doc's ## Status section is the in-file
// completion signal; implementing updates it, the touch trigger reads it.
// Pure string helpers over the quest document; file I/O lives in the
// adapters (quest-doc.ts at src root), never here.

export interface DocStatus {
  phase: string | null;
  complete: boolean;
}

function sectionEnd(lines: string[], header: number): number {
  for (let i = header + 1; i < lines.length; i += 1) {
    if (/^##\s+/.test(lines[i])) return i;
  }
  return lines.length;
}

export function parseDocStatus(text: string): DocStatus {
  const status: DocStatus = { phase: null, complete: false };
  const lines = text.split("\n");
  const header = lines.findIndex((line) => /^##\s+status\b/i.test(line));
  if (header === -1) return status;
  const end = sectionEnd(lines, header);
  for (let i = header + 1; i < end; i += 1) {
    const phase = lines[i].match(/^-\s*Phase:\s*(.+?)\s*$/i);
    if (phase !== null) status.phase = phase[1];
    const complete = lines[i].match(/^-\s*Complete:\s*(true|false)\s*$/i);
    if (complete !== null) status.complete = complete[1].toLowerCase() === "true";
  }
  return status;
}

// Insert or replace the quest doc's ## Status section. Idempotent: present
// Phase/Complete lines are overwritten, missing ones are inserted under the
// header, and a missing header appends the whole section.
export function upsertDocStatus(text: string, phase: string, complete: boolean): string {
  const lines = text.split("\n");
  const header = lines.findIndex((line) => /^##\s+status\b/i.test(line));
  const phaseLine = `- Phase: ${phase}`;
  const completeLine = `- Complete: ${complete ? "true" : "false"}`;
  if (header === -1) {
    const body = text.endsWith("\n") ? text : `${text}\n`;
    return `${body}## Status\n${phaseLine}\n${completeLine}\n`;
  }
  const end = sectionEnd(lines, header);
  let phaseSet = false;
  let completeSet = false;
  const rebuilt = lines.slice(header, end).map((line) => {
    if (/^-\s*Phase:/i.test(line)) {
      phaseSet = true;
      return phaseLine;
    }
    if (/^-\s*Complete:/i.test(line)) {
      completeSet = true;
      return completeLine;
    }
    return line;
  });
  const insert: string[] = [];
  if (!phaseSet) insert.push(phaseLine);
  if (!completeSet) insert.push(completeLine);
  if (insert.length > 0) rebuilt.splice(1, 0, ...insert);
  return [...lines.slice(0, header), ...rebuilt, ...lines.slice(end)].join("\n");
}