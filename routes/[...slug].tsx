import { Handlers } from "$fresh/server.ts";
import { renderModuleRoute } from "../lib/module-router.tsx";
import type { State } from "./_middleware.ts";

/**
 * Catch-all route for legacy NDC modules
 * 
 * This route handles all paths that don't match Fresh routes.
 * It delegates to the legacy module system (mods/star/index.tsx) 
 * using the same routing logic from the old custom SSR server.
 * 
 * Flow:
 *   1. Extract path from ctx.params.slug
 *   2. Try to match against module routes
 *   3. Call module's render() function if matched
 *   4. Return 404 if no module handles the route
 */
export const handler: Handlers<unknown, State> = {
  async GET(req, ctx) {
    const path = "/" + ctx.params.slug;
    const url = new URL(req.url);
    
    // Try to render using module router
    const result = await renderModuleRoute(path, ctx.state.modules, {
      user: ctx.state.user,
      path,
      params: {},
      searchParams: url.searchParams,
    });
    
    if (result) {
      return new Response(result.html, {
        status: result.status,
        headers: { "Content-Type": "text/html" },
      });
    }
    
    // 404 - No module handled this route
    return ctx.renderNotFound();
  },
  
  async POST(req, ctx) {
    const path = "/" + ctx.params.slug;
    const url = new URL(req.url);
    const body = await req.text();
    
    // Same logic but with POST body
    const result = await renderModuleRoute(path, ctx.state.modules, {
      user: ctx.state.user,
      path,
      params: {},
      searchParams: url.searchParams,
      body,
    });
    
    if (result) {
      return new Response(result.html, {
        status: result.status,
        headers: { "Content-Type": "text/html" },
      });
    }
    
    // 404
    return ctx.renderNotFound();
  },
};
