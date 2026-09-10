import { installDrafting } from "./drafting";
import { installImplementing } from "./implementing";
import { installValidation } from "./validation";
import { installSubQuests } from "./subquests";
import { installHumanAbsence } from "./absence";
import { installDurability } from "./durability";
import { installSurface } from "./surface";
import { onSessionStart, type Pi } from "./hooks/events";
import { resetNoticedReviews } from "./review/flow";
import { resetAnnouncements } from "./validation/flow";
import { normalizeKey } from "./views/plan-keys";
import { viewActivePlan } from "./surface/commands/plan";

export default function install(pi: Pi): void {
  installDrafting(pi);
  installImplementing(pi);
  installValidation(pi);
  installSubQuests(pi);
  installHumanAbsence(pi);
  installDurability(pi);
  installSurface(pi);
  // HIGH_LEVEL: #independent review contexts — fresh-session bookkeeping; a
  // stale "Review running" marker must never suppress a notice or a retry.
  onSessionStart(pi, (_e, ctx) => {
    resetNoticedReviews();
    resetAnnouncements();
    if (typeof ctx.ui?.onTerminalInput === "function") {
      ctx.ui.onTerminalInput((data: string) => {
        if (normalizeKey(data) !== "ctrlQ") return undefined;
        void viewActivePlan(ctx);
        return { consume: true };
      });
    }
  });
}