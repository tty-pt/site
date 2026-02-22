# Changelog

All notable changes to this project are documented in this file.

Unreleased
----------

This branch (`tidy/main`) contains a squashed, cleaned-up set of local commits made on top of `origin/main`.

Summary of changes (squashed):

- ci: require pages smoke tests; make target starts site, runs tests, stops services
- chore: remove local SSR dev files; they are not needed in repo
- chore: ignore local SSR dev files
- chore: add pre-commit hook template to run tests; add agent testing guidelines
- test(poem): remove unnecessary sleeps between requests; keep shorter server startup wait
- chore: limit swap ignores to project-specific start.sh swaps
- chore: ignore editor swaps, qmap and runtime artifacts
- ssr: canonicalize proxied response parsing; add pages smoke test runner and Makefile target

Notes
-----

- A backup of the original local branch was created and pushed as `backup/main-before-rewrite`.
- The cleaned history is on branch `tidy/main`; no remote branch rewrite was performed.
- If you want these changes applied in-place to `main`, we can either:
  1. Force-push `tidy/main` to `main` (requires coordination), or
  2. Open a pull request from `tidy/main` to `main` and merge using the project's normal process.

How to proceed
---------------

1. Review the changes on `tidy/main`:

   git fetch origin && git checkout tidy/main && git log --oneline --graph origin/main...HEAD

2. To publish these cleaned commits in-place (replace `main`):

   git checkout tidy/main
   git push --force-with-lease origin tidy/main:main

3. Or open a PR and merge `tidy/main` into `main` (safe, non-destructive):

   git push -u origin tidy/main

Contact
-------

If you are collaborating with others, inform them about the backup branch `backup/main-before-rewrite` before rewriting `main`.
