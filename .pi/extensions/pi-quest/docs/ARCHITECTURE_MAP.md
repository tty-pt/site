# pi-quest v2 Architecture Map

Pending S4 rewrite. Current truth (skeleton, S0):

```
src/
  index.ts          composition root: 7 calls in HIGH_LEVEL order, zero comments
  drafting/         facade: gate + exemption + review loop (S1+S2)
  implementing/     facade: unrestricted + advisory notes + amendments (S3)
  validation/       facade: validator → archive or demote (S3)
  subquests/        facade: stack + links + returns + completion gating (S3)
  absence/          facade: ask-with-default + timeout race (S3)
  durability/       facade: snapshot store + reconstruct (S1)
  surface/          facade: tools + commands + skill (S3)
scripts/            dag · complexity (fail-closed) · spec-map · zip
tests/              smoke (S0); tests/domain/* pure from S1 on
```

- Effective architecture: `REBUILD_PLAN.md` §5 (layered: pure domain +
  reducer, thin adapters, snapshot store, review transport).
- v1 map (oracle, reference only — never source):
  `.pi/quest/archive/pi-quest-v1/docs/ARCHITECTURE_MAP.md`.
- v1 source oracle: `.pi/quest/archive/pi-quest-v1/src/` + tag
  `pi-quest-v1-baseline` (tag lands with the S0 commit).
