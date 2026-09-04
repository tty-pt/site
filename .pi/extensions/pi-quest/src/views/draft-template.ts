// HIGH_LEVEL: #storage — the drafting workspace starts from a template.
export function renderDraftTemplate(name: string, objective: string): string {
  return `# ${name}

> Draft proposal under adversarial review. Only this file is writable while drafting.

## Original request

${objective}

## Requirements

## Evidence

## Implementation Plan
`;
}
