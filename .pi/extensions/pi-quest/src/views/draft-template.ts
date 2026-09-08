// HIGH_LEVEL: #storage — the drafting workspace starts from a template.
export function renderDraftTemplate(name: string, objective: string): string {
  return `# ${name}

> Draft proposal under adversarial review. Only this file is writable while drafting.

## Original request

${objective}

## Requirements

<!-- Author each requirement as its own bullet. A draft reaches the review bar at 2 requirements, or 1 requirement + 7 evidence items, with an actionable plan. -->

## Evidence

<!-- Record file-backed proof per requirement (file:line citations or measured verification). -->

## Implementation Plan
`;
}
