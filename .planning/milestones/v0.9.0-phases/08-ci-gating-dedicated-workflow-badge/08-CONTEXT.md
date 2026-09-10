# Phase 8: CI Gating + Dedicated Workflow + Badge - Context

**Gathered:** 2026-09-02
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase — smart discuss skipped)

<domain>
## Phase Boundary

Wire the Phase 7 WASM Node harness into CI so WASM load/runtime regressions fail the build automatically, and make WASM health independently observable via a dedicated workflow + a README badge. Ports the CI half of anofox-statistics PR #131 (`WasmTest.yml`, the `wasm-runtime-test` job pattern). Delivers CI-01 (gating job that `needs:` the wasm build, downloads the `wasm_eh` artifact, runs the harness, turns red on any WASM load/runtime error), CI-02 (a dedicated WASM workflow separate from the main distribution pipeline), and CI-03 (a README status badge wired to that dedicated workflow).

Work lives in `.github/workflows/` and `README.md`. No SQL functions, no Rust, no crate changes.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure CI/infrastructure phase. Use the ROADMAP goal, the three success criteria, the anofox-statistics PR #131 reference (`WasmTest.yml` + `wasm-runtime-test` job), and this repo's existing `.github/workflows/` conventions.

### LOCKED — CI must gate on the CURATED subset, not `--all`
**This is the single most important constraint** (from Phase 7 verification). The CI gate MUST run the harness against the **curated green subset** (`node test/wasm/run.mjs` — 8 files / 396 assertions, exit 0), NOT `node test/wasm/run.mjs --all`. The full 66-file `--all` run currently has 184 failures across 23 files that are **pre-existing `test/sql` test debt** (removed/renamed API refs, DATE+BIGINT bugs, cascades) — NOT WASM problems. Gating on `--all` would make CI permanently red on unrelated test debt. Full-suite green is tracked separately as an out-of-scope triage effort. See `.planning/phases/07-wasm-node-harness-local-green/07-VERIFICATION.md` (Re-Verification section).

The gate's job is to catch **WASM load/runtime regressions** (extension fails to LOAD, harness crashes, curated assertions regress) — which the curated subset does exactly.

</decisions>

<code_context>
## Existing Code Insights

### Reference Implementation to Port (CI half of PR #131)
- `/home/simonm/projects/duckdb/anofox-statistics/.github/workflows/WasmTest.yml` — the dedicated WASM workflow + `wasm-runtime-test` gating job. Substitute `anofox_statistics` → `anofox_forecast`, artifact names, and the harness invocation to the curated run.

### This repo's CI
- `.github/workflows/MainDistributionPipeline.yml` — main distribution (builds wasm via `duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@v1.5-variegata`; also a v1.4 line). The gating job must `needs:` the wasm build and download the `wasm_eh` artifact it produces.
- `.github/workflows/_extension_deploy.yml`, `_extension_smoke_test.yml` — existing reusable workflows; artifact naming pattern: `${extension_name}-${duckdb_version}-extension-${arch}${...}.wasm` (e.g. `anofox_forecast-v1.5.5-extension-wasm_eh`).
- Node harness entry: `test/wasm/run.mjs`; deps installed via `npm --prefix test/wasm install` (pinned `@duckdb/duckdb-wasm@1.33.1-dev64.0`, `web-worker@1.2.0`).

### Integration Points
- `README.md` header badges (around lines 9-12) — add a WASM status badge next to the existing License/DuckDB/Build/Tests badges, wired to the dedicated WASM workflow's latest run (GitHub Actions workflow-status badge URL: `https://github.com/DataZooDE/anofox-forecast/actions/workflows/<file>.yml/badge.svg`).

</code_context>

<specifics>
## Specific Ideas

- Verify CI-01's "turns red on WASM failure" claim per the success criterion — the plan should demonstrate the gate actually fails on a broken `.wasm`/failing curated test (e.g. a documented negative-control check), not just that it's green.
- Determine the correct DuckDB version line(s) to gate (the built wasm target is `wasm_eh` at v1.5.5; the v1.4 LTS line has no matching duckdb-wasm npm package per Phase 7 research and is intentionally not harness-tested).
- Reuse the repo's existing workflow ref conventions (`@v1.5-variegata` etc.) so the wasm build artifact the gate consumes matches the pipeline that produces it.

</specifics>

<deferred>
## Deferred Ideas

- Making the full 66-file `--all` suite green (pre-existing test-suite triage) — out of scope; tracked separately, do NOT gate CI on it.
- Browser-based (not just Node) WASM E2E harness (WASM-F1) — deferred at milestone open.
- Shared-memory `wasm_threads` build (WASM-F2) — blocked upstream.

</deferred>
