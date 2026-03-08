import { MiddlewareHandlerContext } from "$fresh/server.ts";
import { getModules, ModuleEntry } from "../lib/module-router.tsx";

/**
 * Global middleware state
 * 
 * Extracted from NDC proxy headers and made available to all routes via ctx.state
 */
export interface State {
  user: string | null;
  modules: ModuleEntry[];
}

/**
 * Global middleware handler
 * 
 * Runs on every request before routing to extract NDC headers.
 * Makes user and module list available to all route handlers.
 */
export async function handler(
  req: Request,
  ctx: MiddlewareHandlerContext<State>
) {
  // Extract user from NDC proxy header
  ctx.state.user = req.headers.get("X-Remote-User");
  
  // Parse modules from X-Modules header
  try {
    ctx.state.modules = getModules(req);
  } catch (e) {
    console.error("Failed to parse modules in middleware:", e);
    ctx.state.modules = [];
  }
  
  // Continue to next handler (route or another middleware)
  const resp = await ctx.next();
  
  return resp;
}
