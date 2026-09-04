import { installDrafting } from "./drafting";
import { installImplementing } from "./implementing";
import { installValidation } from "./validation";
import { installSubQuests } from "./subquests";
import { installHumanAbsence } from "./absence";
import { installDurability } from "./durability";
import { installSurface } from "./surface";

export interface ExtensionContext {
  cwd?: string;
  [key: string]: unknown;
}

export interface ExtensionAPI {
  on(
    event: string,
    handler: (event: unknown, ctx: ExtensionContext) => unknown,
  ): void;
}

export default function install(pi: ExtensionAPI): void {
  installDrafting(pi);
  installImplementing(pi);
  installValidation(pi);
  installSubQuests(pi);
  installHumanAbsence(pi);
  installDurability(pi);
  installSurface(pi);
}
