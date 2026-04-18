export interface ModuleEntry {
  id: string;
  title: string;
  flags: number;
}

/**
 * Parse X-Modules header from NDC proxy
 */
export function getModules(req: Request): ModuleEntry[] {
  const header = req.headers.get("X-Modules");
  if (!header) {
    return [];
  }
  
  try {
    const json = atob(header);
    const parsed = JSON.parse(json) as ModuleEntry[];
    
    if (!Array.isArray(parsed)) {
      console.error("X-Modules header is not an array");
      return [];
    }
    
    return parsed;
  } catch (e) {
    console.error("Failed to parse X-Modules header:", e);
    return [];
  }
}
