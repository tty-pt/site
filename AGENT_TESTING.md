Agent Testing Guidelines

An agent working in this repository must run the test suite after every change and before committing or proposing a change.  This file explains the minimal, discoverable checks to run so humans and other agents can rely on CI-like validation locally.

Quick rules
- Run the module/unit tests: `make test`
- Run the pages smoke tests (end-to-end): `make pages-test` or `sh tests/pages/10-pages-render.sh`
- Only commit changes after the above tests pass locally.
- Do not commit build artifacts or editor temp files; check `git status` and `git ls-files`.

Commands
```sh
# build and run module tests
make test

# start the site and run page smoke tests
make pages-test

# quick manual smoke run (if you started the site yourself)
NDC_HOST=127.0.0.1 NDC_PORT=8080 sh tests/pages/10-pages-render.sh

# verify there are no tracked runtime/editor artifacts
git ls-files --exclude-standard --others
git status --porcelain
``` 

Helpful checks before committing
- `git status` — ensure only intended files are staged.
- `git diff --staged` — review staged changes.
- `git clean -nX` — preview ignored build artifacts that should be removed from the working tree.

If any test fails, do not commit. Fix the issue or open an issue/PR describing the failure and include relevant test output.

Location: top-level file so agents and humans find it quickly (root of repository).
