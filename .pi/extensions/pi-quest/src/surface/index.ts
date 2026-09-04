// HIGH_LEVEL: #surface — quest tools, user commands, and the skill.
// HIGH_LEVEL: #tools (main agent) — six quest tools.
// HIGH_LEVEL: #commands — three manual handles.
// HIGH_LEVEL: #skill — workflow rules text.
import { installCommands } from "./commands";
import { installTools } from "./tools";
import type { Pi } from "../hooks/events";

export function installSurface(pi: Pi): void {
  installTools(pi);
  installCommands(pi);
}
