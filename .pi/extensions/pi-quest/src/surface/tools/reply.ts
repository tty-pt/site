// HIGH_LEVEL: #tools (main agent) — shared tool-result shape.
import type { AgentToolResult } from "../../hooks/events";

export function textResult(text: string, details: unknown = {}): AgentToolResult {
  return { content: [{ type: "text", text }], details };
}
