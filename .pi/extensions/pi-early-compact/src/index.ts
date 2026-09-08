import { installCommand } from "./command.ts";
import { installMidRunGuard } from "./midrun.ts";
import { installStatusFooter } from "./status.ts";
import { installPreflight, type UltraPi } from "./trigger.ts";

export default function install(pi: UltraPi): void {
  installPreflight({ pi });
  installMidRunGuard({ pi });
  installCommand(pi);
  installStatusFooter(pi);
}