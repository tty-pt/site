// HIGH_LEVEL: #surface — quest tools, user commands, and the skill.
// HIGH_LEVEL: #tools (main agent) — six quest tools.
// HIGH_LEVEL: #commands — three manual handles.
// HIGH_LEVEL: #skill — workflow rules text.
// SPEC: slices 2-3 (tool contracts, commands, skill text).
import { installTools } from "./tools";
import { installCommands } from "./commands";
import type { ExtensionAPI } from "../index.ts";

export function installSurface(pi: ExtensionAPI): void {
  installTools(pi);
  installCommands(pi);
}
