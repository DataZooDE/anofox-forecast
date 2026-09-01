---
phase: 08-ci-gating-dedicated-workflow-badge
plan: 01
subsystem: infra
tags: [github-actions, wasm, ci, duckdb-wasm, badge]

# Dependency graph
requires:
  - phase: 07-wasm-node-harness-local-green
    provides: "test/wasm/run.mjs harness (curated WASM subset, 8 files / 396 assertions, exit 0)"
provides:
  - "CI-01: wasm-runtime-test gating job in MainDistributionPipeline.yml (needs duckdb-latest-build, curated harness, no --all)"
  - "CI-02: dedicated WasmTest.yml workflow (workflow_run on pipeline, powers WASM badge)"
  - "CI-03: WASM status badge in README.md (WasmTest.yml/badge.svg?branch=main)"
affects: [future-phases, milestone-v0.9.0-wasm-runtime-verification]

actuals:
  tokens: 3600
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Curated CI gate: run harness without --all to avoid gating on pre-existing test debt (184 --all failures unrelated to WASM)"
    - "Same-run vs cross-run artifact download: gating job uses no run-id (same run); dedicated workflow uses run-id + github-token (cross-run via workflow_run)"
    - "Badge isolation: dedicated WasmTest.yml workflow powers the badge so any unrelated pipeline failure does not flip it"

key-files:
  created:
    - ".github/workflows/WasmTest.yml"
  modified:
    - ".github/workflows/MainDistributionPipeline.yml"
    - "README.md"

key-decisions:
  - "Gate on curated subset (no --all): node test/wasm/run.mjs --ext \"$EXT\" — the LOCKED constraint from 08-CONTEXT.md. Full --all suite has 184 pre-existing test-debt failures across 23 files unrelated to WASM load/runtime."
  - "Artifact name anofox_forecast-v1.5.5-extension-wasm_eh derived from pipeline pattern (extension_name=anofox_forecast, duckdb_version=v1.5.5); verified structurally, live confirmation is Task 4 (human)."
  - "Same-run download in MainDistributionPipeline.yml (no run-id, no github-token); cross-run download in WasmTest.yml (run-id: github.event.workflow_run.id + github-token) — different contexts require different artifact-download modes."
  - "Permissions: actions read + contents read only (T-08-01 mitigated — no write scopes, no OIDC in WasmTest.yml)."

patterns-established:
  - "Port pattern: replace anofox_statistics with anofox_forecast throughout; replace --all with curated invocation; split same-run vs cross-run download by job context."

requirements-completed: [CI-01, CI-02, CI-03]

coverage:
  - id: D1
    description: "WasmTest.yml dedicated WASM workflow exists, valid YAML, name=WASM, triggers via workflow_run on Main Extension Distribution Pipeline, downloads correct artifact with cross-run token, runs curated harness (no --all)"
    requirement: CI-02
    verification:
      - kind: other
        ref: "python3 yaml.safe_load(.github/workflows/WasmTest.yml) — PASS; grep assertions for artifact name, run-id, github-token, harness invocation — PASS; no --all, no anofox_statistics — PASS"
        status: pass
    human_judgment: false
  - id: D2
    description: "wasm-runtime-test gating job in MainDistributionPipeline.yml: needs duckdb-latest-build, if-guarded on success, same-run artifact download, curated harness, all prior jobs intact"
    requirement: CI-01
    verification:
      - kind: other
        ref: "python3 yaml.safe_load(.github/workflows/MainDistributionPipeline.yml) — PASS; job assertions (needs, steps, no run-id/github-token, no --all, prior jobs intact) — PASS"
        status: pass
    human_judgment: false
  - id: D3
    description: "README.md WASM status badge wired to WasmTest.yml/badge.svg?branch=main with correct href and alt; existing badges unchanged"
    requirement: CI-03
    verification:
      - kind: other
        ref: "grep assertions for badge SVG URL, href, alt=WASM, existing badges — PASS"
        status: pass
    human_judgment: false
  - id: D4
    description: "Gate is proven to turn RED on a deliberate curated-test breakage and GREEN on revert — live CI negative-control (Task 4)"
    requirement: CI-01
    verification: []
    human_judgment: true
    rationale: "Task 4 is a blocking-human checkpoint: the gate cannot be proven to turn red without a live CI push and GitHub Actions UI observation. Observing green->red->green requires a human to push, introduce a deliberate breakage, observe the red run, revert, and confirm the green. No automation can substitute for this."

duration: 3min
completed: 2026-09-02
status: complete
---

# Phase 8 Plan 01: CI Gating + Dedicated Workflow + Badge Summary

**WasmTest.yml dedicated CI workflow + wasm-runtime-test gating job wired to the curated 8-file/396-assertion harness (no --all), plus a README WASM badge — ported from anofox-statistics PR #131**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-09-01T22:24:56Z
- **Completed:** 2026-09-01T22:27:20Z
- **Tasks:** 3 completed (Task 4 = blocking-human checkpoint, not executed autonomously)
- **Files modified:** 3

## Accomplishments

- Created `.github/workflows/WasmTest.yml` (CI-02): dedicated WASM workflow that triggers via `workflow_run` on the main pipeline, downloads the cross-run `wasm_eh` artifact, and runs the curated harness. Powers the README WASM badge independently of any unrelated pipeline failures.
- Added `wasm-runtime-test` gating job to `MainDistributionPipeline.yml` (CI-01): `needs: duckdb-latest-build`, same-run artifact download (no run-id/github-token), curated harness invocation `node test/wasm/run.mjs --ext "$EXT"` with no `--all` and no `--file`. No `continue-on-error` — harness exit non-zero turns the pipeline red.
- Added WASM status badge to `README.md` (CI-03): badge href points to `WasmTest.yml` workflow; `?branch=main` keeps the badge stable on development branches; file name matches exactly.

## Task Commits

Each task was committed atomically:

1. **Task 1: Create the dedicated WasmTest.yml workflow (CI-02)** - `f9d7f1d` (feat)
2. **Task 2: Add the wasm-runtime-test gating job to MainDistributionPipeline.yml (CI-01)** - `2c6e3ee` (feat)
3. **Task 3: Add the WASM status badge to README.md (CI-03)** - `3acd878` (feat)

## Files Created/Modified

- `.github/workflows/WasmTest.yml` — new: dedicated WASM workflow (50 lines). Triggers via `workflow_run`. Cross-run artifact download. Curated harness, no `--all`. Permissions: `actions: read`, `contents: read`.
- `.github/workflows/MainDistributionPipeline.yml` — modified: added `wasm-runtime-test` job (35 lines) after `duckdb-latest-deploy`. Same-run artifact download. Curated harness. No existing job altered.
- `README.md` — modified: added WASM badge line (1 line) inside the existing `<p align="center">` badge block after the Tests badge.

## Structural Verification Results

All automated checks ran against the committed files and passed:

**WasmTest.yml:**
- YAML parses cleanly (`python3 -c "import yaml; yaml.safe_load(...)"`)
- `name == 'WASM'`
- `on.workflow_run.workflows` contains `"Main Extension Distribution Pipeline"`
- `permissions: {actions: read, contents: read}`
- Artifact name `anofox_forecast-v1.5.5-extension-wasm_eh` present
- `run-id: ${{ github.event.workflow_run.id }}` present
- `github-token: ${{ secrets.GITHUB_TOKEN }}` present
- Harness invocation `node test/wasm/run.mjs --ext "$EXT"` — no `--all`, no `--file`
- Zero occurrences of `anofox_statistics`

**MainDistributionPipeline.yml:**
- YAML parses cleanly
- `jobs.wasm-runtime-test.needs == 'duckdb-latest-build'`
- `if` guard references `needs.duckdb-latest-build.result == 'success'`
- Artifact name `anofox_forecast-v1.5.5-extension-wasm_eh` present in steps
- Harness invocation `run.mjs --ext` — no `--all`
- `run-id` and `github-token` absent from wasm-runtime-test steps (same-run download)
- All prior jobs intact: `duckdb-lts-build`, `duckdb-lts-smoke-test`, `duckdb-lts-deploy`, `duckdb-latest-build`, `duckdb-latest-smoke-test`, `duckdb-latest-deploy`, `build-and-test-rust`

**README.md:**
- Badge SVG URL `WasmTest.yml/badge.svg?branch=main` present
- Badge href `https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml` present
- `alt="WASM"` present
- License badge (`img.shields.io/badge/License`) intact
- Tests badge (`Tests-295%20Rust`) intact

## Decisions Made

- **Curated gate only (no --all):** The LOCKED constraint from 08-CONTEXT.md. The full `--all` run has 184 pre-existing test-debt failures across 23 files (removed/renamed API refs, DATE+BIGINT bugs, cascades) that are not WASM problems. Gating on `--all` would permanently red the pipeline on unrelated debt.
- **Artifact name:** `anofox_forecast-v1.5.5-extension-wasm_eh` derived from the distribution pipeline convention (`extension_name=anofox_forecast`, `duckdb_version=v1.5.5`, `ci_tools_version=v1.5-variegata`). Structural derivation confirmed; live confirmation awaits Task 4.
- **Same-run vs cross-run download:** The gating job in MainDistributionPipeline.yml uses a same-run download (no `run-id`, no `github-token` — the artifact is produced within the same run). WasmTest.yml uses a cross-run download (`run-id: ${{ github.event.workflow_run.id }}` + `github-token`) because it is a separate workflow that reads an artifact from the triggering run.
- **Permissions in WasmTest.yml:** `actions: read` + `contents: read` only — read-only, no write scopes, no OIDC (T-08-01 mitigated verbatim from the reference).

## Deviations from Plan

None — plan executed exactly as written. The `--all` removal from the reference WasmTest.yml was a planned substitution (LOCKED constraint in the plan action, not a deviation).

## Task 4: Blocking Human Checkpoint (Negative Control)

Task 4 is `type="checkpoint:human-verify"` with `gate="blocking-human"`. It was NOT executed autonomously.

**Operator procedure:**
1. Push this branch and open a PR. Confirm `wasm-runtime-test` appears in the checks list and runs GREEN — this validates the artifact name `anofox_forecast-v1.5.5-extension-wasm_eh` against a live run.
2. Introduce a deliberate breakage in one curated test file (e.g., change an expected value in `test/sql/ts_diagnostics.test`). Push and confirm `wasm-runtime-test` turns RED (harness exits non-zero, step fails, job fails).
3. Revert the deliberate breakage. Confirm the job returns to GREEN.
4. After a pipeline run on the branch, confirm `WasmTest.yml` fires via `workflow_run` and the README badge resolves (badge may show grey until the first green run lands on main).

**Required evidence for CI-01 sign-off:** Record the run URLs (green, red, green-again) in the SUMMARY as the CI-01 negative-control evidence.

## Issues Encountered

None.

## Next Phase Readiness

- Tasks 1-3 are structurally verified and committed. The WASM CI gate and badge infrastructure is in place.
- Task 4 (blocking human checkpoint) requires the operator to push the branch, observe green CI, introduce a deliberate curated-test breakage, observe RED, revert, and confirm GREEN again. Once Task 4 is complete the CI-01 requirement is fully satisfied.
- No blockers for the operator push step.

---
*Phase: 08-ci-gating-dedicated-workflow-badge*
*Completed: 2026-09-02*
