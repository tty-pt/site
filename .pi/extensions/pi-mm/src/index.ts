import { installTools } from "./tools";

export default function install(pi: import("./hooks/events.ts").Pi): void {
  installTools(pi);
}