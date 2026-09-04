import { installDrafting } from "./drafting";
import { installImplementing } from "./implementing";
import { installValidation } from "./validation";
import { installSubQuests } from "./subquests";
import { installHumanAbsence } from "./absence";
import { installDurability } from "./durability";
import { installSurface } from "./surface";
import type { Pi } from "./hooks/events";

export default function install(pi: Pi): void {
  installDrafting(pi);
  installImplementing(pi);
  installValidation(pi);
  installSubQuests(pi);
  installHumanAbsence(pi);
  installDurability(pi);
  installSurface(pi);
}
