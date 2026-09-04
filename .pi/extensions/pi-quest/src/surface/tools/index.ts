// HIGH_LEVEL: #surface — quest tools, user commands, and the skill.
// HIGH_LEVEL: #tools (main agent) — six quest tools.
import type { Pi } from "../../hooks/events";
import { archiveTool } from "./archive";
import { askHumanTool } from "./ask-human";
import { rebutTool } from "./rebut";
import { recoverTool } from "./recover";
import { subquestTool } from "./subquest";
import { updateStateTool } from "./update-state";

export function installTools(pi: Pi): void {
  pi.registerTool(updateStateTool(pi));
  pi.registerTool(subquestTool(pi));
  pi.registerTool(archiveTool(pi));
  pi.registerTool(recoverTool(pi));
  pi.registerTool(rebutTool(pi));
  pi.registerTool(askHumanTool(pi));
}
