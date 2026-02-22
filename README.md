# NDC Site (ndc + Deno SSR)

Small web site built as an ndc application with a Deno-based SSR backend. The repository contains the ndc core, several modules (auth, poem, mpfd, index_api) and an SSR proxy module that forwards requests to a Deno SSR server.

Quick start
- Build native modules and run module tests: `make test`
- Start the full site (ndc + SSR): `./start.sh` (ndc listens on port 8080 by default)
- Run page smoke tests: `make pages-test` or `sh tests/pages/10-pages-render.sh`

Requirements
- A C compiler (clang/gcc) and development headers for ndc/ndx/qmap (used when building modules)
- `deno` installed at the path used by `start.sh` (or edit `start.sh` to point to your deno)
- `ndc` and `qmap` libs available on your system (the Makefile expects them under `/home/quirinpa` in this tree; adapt `Makefile`/env as needed)

Repository layout
- `mods/` — ndc modules (each has a `mod.json` and C/Makefile)
- `mods/ssr/ssr.c` — SSR proxy implementation (parses upstream response and forwards via ndc APIs)
- `mods/ssr/server.ts` — Deno SSR server (optional local file; not tracked by default)
- `tests/pages/` — small page smoke tests and helpers
- `AGENT_TESTING.md` — guidelines for agents to run tests on each change
- `.githooks/pre-commit` — sample pre-commit hook that runs `make test` (not installed automatically)

Testing rules (agents and humans)
1. Run `make test` after every change and before committing.
2. For end-to-end checks, run `make pages-test` (this starts the services then runs smoke tests).
3. Use `git status` and `git ls-files` to avoid committing build artifacts (see `.gitignore`).

Enable the pre-commit hook locally
```sh
# link the provided hook into Git hooks so it runs automatically
ln -s ../../.githooks/pre-commit .git/hooks/pre-commit
chmod +x .githooks/pre-commit
```

Development notes
- The SSR proxy was hardened to parse upstream status/headers and forward them via `ndc_head`/`ndc_header` to avoid duplicated headers and injection risks — see `mods/ssr/ssr.c`.
- Avoid committing generated binaries (`*.so`, `module.db`, `mods.load`) or editor swap files. These are ignored by `.gitignore`.

If something breaks
- Re-run `make test` to get failing module output.
- Check logs in `/tmp` if you use `start.sh` (or update `start.sh` to redirect logs where you want).

License
- MIT
