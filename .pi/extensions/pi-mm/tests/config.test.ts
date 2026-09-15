import { readMmConfig, loadConfig, applyEnv, resolveQmapBin, resolveAxisPath, DEFAULT_CONFIG } from "../src/config.ts";
import { join } from "node:path";

function check(cond: boolean, msg: string): void {
  if (!cond) throw new Error(msg);
}

Deno.test("loadConfig: undefined → defaults", () => {
  const cfg = loadConfig(undefined);
  check(cfg.qmapBin === "", "qmapBin default empty");
  check(cfg.axisLibs === null, "axisLibs default null");
  check(cfg.memDir === ".pi/mm", "memDir default");
  check(cfg.scanLimit === 10, "scanLimit default");
});

Deno.test("loadConfig: valid overrides honored, invalid values fall back", () => {
  const cfg = loadConfig({
    qmapBin: "/usr/bin/qmap",
    axisLibs: "/a:/b",
    memDir: "mem",
    scanLimit: 5,
  });
  check(cfg.qmapBin === "/usr/bin/qmap", "qmapBin override");
  check(cfg.axisLibs === "/a:/b", "axisLibs override");
  check(cfg.memDir === "mem", "memDir override");
  check(cfg.scanLimit === 5, "scanLimit override");

  const bad = loadConfig({ qmapBin: 3, axisLibs: null, memDir: "", scanLimit: -2 });
  check(bad.qmapBin === "", "qmapBin non-string → default");
  check(bad.axisLibs === null, "axisLibs null → default");
  check(bad.memDir === DEFAULT_CONFIG.memDir, "memDir empty → default");
  check(bad.scanLimit === 10, "negative scanLimit → default");
});

Deno.test("applyEnv: fills only when settings unset; respects settings otherwise", () => {
  const fromSettings = loadConfig({ qmapBin: "/usr/bin/qmap", axisLibs: "/a:/b" });
  const merged = applyEnv(fromSettings, { QMAP_BIN: "/env/qmap", QMAP_AXIS_PATH: "/env:/axes" });
  check(merged.qmapBin === "/usr/bin/qmap", "settings qmapBin wins over env");
  check(merged.axisLibs === "/a:/b", "settings axisLibs wins over env");

  const bare = applyEnv(DEFAULT_CONFIG, { QMAP_BIN: "/env/qmap", QMAP_AXIS_PATH: "/env:/axes" });
  check(bare.qmapBin === "/env/qmap", "env QMAP_BIN fills unset qmapBin");
  check(bare.axisLibs === "/env:/axes", "env QMAP_AXIS_PATH fills unset axisLibs");
});

Deno.test("resolveQmapBin: precedence settings → env → PATH → in-site → none", () => {
  const probe = (p: string): boolean => p === "/usr/bin/qmap" || p === "/env/qmap" ||
    p === "/opt/bin/qmap" || p === "/repo/external/libqmap/bin/qmap";
  const cfg = loadConfig({ qmapBin: "/usr/bin/qmap" });
  check(resolveQmapBin(cfg, "/repo", ["/opt/bin"], probe).source === "settings", "settings chosen");

  const fromEnv = applyEnv(DEFAULT_CONFIG, { QMAP_BIN: "/env/qmap" });
  check(resolveQmapBin(fromEnv, "/repo", ["/opt/bin"], probe).path === "/env/qmap", "env chosen");

  const fromPath = resolveQmapBin(DEFAULT_CONFIG, "/repo", ["/opt/bin"], probe);
  check(fromPath.source === "path" && fromPath.path === "/opt/bin/qmap", "PATH chosen");

  const fromInSite = resolveQmapBin(DEFAULT_CONFIG, "/repo", ["/no/qmap"], probe);
  check(fromInSite.source === "insite", "in-site fallback chosen");

  const none = resolveQmapBin(DEFAULT_CONFIG, "/nowhere", ["/no/qmap"], (p) => p === "/nowhere/external/libqmap/bin/qmap" && false);
  check(none.source === "none" && none.path === "", "none when nothing resolves");
});

Deno.test("resolveAxisPath: explicit list used; null → in-site joint:stoma under cwd", () => {
  check(resolveAxisPath(loadConfig({ axisLibs: "/x:/y" }), "/repo") === "/x:/y", "explicit axisLibs");
  check(
    resolveAxisPath(DEFAULT_CONFIG, "/repo") ===
      join("/repo", "external", "libjoint", "lib") + ":" + join("/repo", "external", "libstoma", "lib"),
    "in-site joint:stoma default",
  );
});

Deno.test("readMmConfig: no settings file → defaults", async () => {
  const cfg = await readMmConfig("/definitely/not/a/real/pi/config/dir");
  check(cfg.memDir === ".pi/mm", "defaults on missing settings");
});