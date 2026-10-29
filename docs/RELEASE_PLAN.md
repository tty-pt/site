# Release Plan: Core Dependencies & Pi Extensions

This document defines the release plan, dependency sequencing, and batching strategy for the `tty.pt` system libraries and Pi agent extensions. Releases proceed from low-level dependencies up to user-facing extensions, bounded to **at most 3 releases per batch**.

---

## 1. Release Architecture & Dependency Graph

```
             ┌────────────────┐
             │   axil-auren   │ (v1.4.0: /v1/classify, collections)
             └───────┬────────┘
                     │
         ┌───────────┴───────────┐
         ▼                       ▼
   ┌───────────┐           ┌───────────┐
   │   pi-mm   │           │ pi-quest  │
   │ (v0.5.0)  │           │ (v1.9.1)  │
   └───────────┘           └───────────┘
```

- **`axil-auren`**: Fundamental local inference, embedding, and zero-allocation classification engine (`POST /v1/classify`) running on port 4242.
- **`pi-mm`**: Persistent cognitive memory extension utilizing `axil-auren` for turn classification, standing directive identification, overview routing, and recall intent detection.
- **`pi-quest`**: Autonomous software engineering and quest journal extension utilizing `axil-auren` for quest intent detection, difficulty tiering, and steering evaluation.

---

## 2. Release Batches (Max 3 Releases per Batch)

### Batch 1: Core Engine Dependency (Target: 1 Release)

#### 1. `axil-auren` `v1.4.0`
- **Repository**: `/home/quirinpa/axil-auren`
- **Prior Tag**: `v1.3.1`
- **Release Target**: `v1.4.0`
- **Key Changes**:
  - **In-Memory Classification Endpoint (`POST /v1/classify`)**: High-throughput, zero-allocation cosine similarity classification against exemplar embeddings (`src/openai_classify.c`), providing 10–20 ms decision evaluation without generative LLM decoding.
  - **Multi-Tenant Vector Collections (`POST /v1/collections`)**: Dynamic in-memory collection management for grouped embeddings and semantic recall.
  - **Bounded Choice Generation**: Capped judge generation (`SYSTEMONE_MAX_GEN`, 64 tokens) and logit-scored choice evaluation (`auren_choice_eval()`), preventing runaway context consumption.
  - **Compiler Deprecation Warnings**: Deprecated legacy `systemzero` (`POST /v1/chat/completions`) and `systemone` (`POST /v1/judge`) with `__attribute__((deprecated))` compiler attributes and runtime upgrade notices.
- **Verification**: `LD_LIBRARY_PATH=lib ./bin/engine-test ~/.models/qwen2.5-0.5b-instruct.gguf`

---

### Batch 2: Pi Agent Extensions (Target: 2 Releases)

#### 2. `pi-mm` `v0.5.0`
- **Repository**: `.pi/extensions/pi-mm` (`git@github.com:tty-pt/pi-mm.git`)
- **Prior Tag**: `v0.4.1`
- **Release Target**: `v0.5.0`
- **Key Changes**:
  - **Embedding-Only Auto-Download**: Auto-download strictly downloads required embedding models (`bge-base`, 117 MB). Unused inference models (`qwen`, `olmoe`) are never downloaded; auto-download is skipped entirely if any embedding model is present.
  - **Non-Blocking Visual Feedback**: Live download progress (`⬇ bge-base (42%)`) and model usage status (`⚡ bge-base`) displayed in footer status key `"mm-judge"`.
  - **Proactive Recall Intent**: Detects epistemic inquiries against bilingual exemplars (`recallEmbed: "auto"`), lifting unprompted filters and injecting actionable tool guidance to the agent.
  - **Standing Directives & Overview Routing**: Classifies operational invariants (`directiveEmbed: "auto"`) and timeline queries (`overviewEmbed: "auto"`) via `POST /v1/classify`.
  - **Turn Capture Migration**: Conversational turn capture evaluated via `/v1/classify`, eliminating generative LLM judge latency.
  - **Daemon Optimization**: Decoupled `cormd` daemon startup latency and memoized daemon runner.
- **Verification**: `npm --prefix .pi/extensions/pi-mm test` (466 passing tests)

#### 3. `pi-quest` `v1.9.1`
- **Repository**: `.pi/extensions/pi-quest` (`git@github.com:tty-pt/pi-quest.git`)
- **Prior Tag**: `v1.5.0`
- **Release Target**: `v1.9.1`
- **Key Changes**:
  - **Embedding-Only Auto-Download**: Restricts downloads to embedding models (`bge-base`), skips downloads if any embedding model exists, and reports non-blocking visual progress (`quest-judge`).
  - **Reviewer Rate-Limit Failover & Cooldowns**: 15-minute provider cooldown on HTTP 429 / quota exhaustion; automatic failover across router default, session model, and inherit.
  - **Compound Advance Steering**: Detects `"go"` followed by trailing instructions (`parseLeadingAdvance`), promoting to implementation and injecting trailing directives.
  - **Quest Kind Classification**: Classifies objectives into 6 kinds (`POST /v1/classify`); relaxes binary test commands for `docs` quests.
  - **Brainstorming Intent Disambiguation**: Adds technical inquiry and collaborative thinking exemplars to prevent accidental quest triggering.
- **Verification**: `npm --prefix .pi/extensions/pi-quest test` (580 passing tests)

---

## 3. Subsequent Downstream Batches (Future Scope)

The following sibling components have updates queued for subsequent release cycles:

### Batch 3: NeverDark Service & Terminal Infrastructure (Future)
- **`axil-nd` `v1.4.0`**: Room art path preservation, opcode 19 collision resolution, map-cell header parser.
- **`axil-tty` `v1.6.0`**: Dynamic terminal fitting via `FitAddon` + `ResizeObserver`, non-root execution under `-A`.
- **`libbud` `v1.1.1`**: Stable VDOM diff patch node IDs and explicit parent pointers.

### Batch 4: NeverDark Gameplay Modules (Future)
- **`nd-core` `v1.0.3`**, **`nd-attr` `v1.0.2`**, **`nd-equip` `v1.0.3`**, **`nd-fight` `v1.0.3`**, **`nd-spell` `v1.0.8`**, **`nd-stone` `v1.0.2`**, **`nd-plant` `v1.0.3`**.

---

## 4. Release Checklist & Verification Gates

For each package released:
1. **Pre-Tag Test Verification**: Run full test suites prior to git tagging to guarantee zero regressions.
2. **Changelog Validation**: Verify `CHANGELOG.md` explicitly brackets the version heading (`## [X.Y.Z] - YYYY-MM-DD`) and lists all functional additions, fixes, and deprecations.
3. **Version Alignment**: Ensure package manifests (`package.json`, `.pc`, headers) match the release tag exactly.
4. **Git Tag & Push**: Create annotated git tag (`git tag -a vX.Y.Z -m "..."`) and push commits and tags to origin.
5. **Submodule Synchronization**: Update parent repository (`site`) submodule pointers and rebuild distribution bundles (`npm run zip`).
