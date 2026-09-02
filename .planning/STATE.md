---
gsd_state_version: 1.0
milestone: v0.9.0
milestone_name: WASM Runtime Verification (Phases 7-8)
current_phase: 08
current_phase_name: CI Gating + Dedicated Workflow + Badge
status: verification_deferred_human
stopped_at: Phase 08 implemented + locally verified (gate mechanism green→red→green); live-CI negative-control push deferred to operator
last_updated: "2026-09-02T00:50:00.000Z"
last_activity: 2026-09-02
last_activity_desc: Autonomous run paused — Phase 08 live-CI verification deferred (user choice)
state_head: 3acd878c0f569931fcd0cc6827a81acfc37c8def
progress:
  total_phases: 2
  completed_phases: 1
  total_plans: 2
  completed_plans: 2
  percent: 50
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-09-01 — started v0.9.0 WASM Runtime Verification)

**Core value:** Prove the built `anofox_forecast` `.wasm` actually loads and runs in DuckDB-Wasm — not just that it compiles and links — and gate it in CI so WASM regressions fail the build.
**Current focus:** Phase 08 — CI Gating + Dedicated Workflow + Badge

## Current Position

Phase: 08 (CI Gating + Dedicated Workflow + Badge) — IMPLEMENTED, live-CI verification deferred
Plan: 1 of 1 done (tasks 1-3); Task 4 negative-control = operator live-CI push
Status: Autonomous run paused — both phases implemented; Phase 08 awaits operator live-CI observation
Last activity: 2026-09-02 — Phase 08 live-CI verification deferred (user choice)

## Deferred Verification

| Phase | State | Resume |
|-------|-------|--------|
| 8 | verification_deferred_human | /gsd-verify-work 8 |

Phase 8 is implemented and locally verified: CI-01/02/03 files are committed, the LOCKED
curated-subset constraint is honored (no `--all` in CI), and the gate mechanism is proven
locally (curated run exit 0 → broken curated test exit 1 → reverted exit 0). The remaining
step is the operator's: push the branch, observe `wasm-runtime-test` go green → red (on a
deliberate curated break) → green in GitHub Actions, confirm the `WasmTest.yml` badge
renders/flips, and record the run URLs. Then `/gsd-verify-work 8`, then
`/gsd-complete-milestone v0.9.0`.

### ⚠ Phase 07 finding that must shape Phase 08 planning

A fresh HEAD `wasm_eh` build was made locally during Phase 07 verification. It
proved DEP-02 (zero OpenSSL compiled for wasm) and disproved the "stale artifact"
theory: the full-suite `--all` run is **2259 pass / 184 fail / 23 files**, and the
23 failures are **pre-existing `test/sql` test debt** (removed/renamed API refs like
`ts_backtest_auto_by`/`ts_hydrate_features_by`, `DATE+BIGINT` bugs, cascades), not
WASM or artifact issues — masked natively by the unittest `require json` skip.

**Phase 08 (CI gating) must gate on the CURATED green subset**, NOT `--all`.
Do NOT assume "CI building from HEAD makes the suite green" — it will not. Full
66-file green is out-of-scope test-triage tracked separately. See
07-VERIFICATION.md Re-Verification section + memory `project_wasm_suite_reveals_test_debt`.

## Performance Metrics

**Velocity:**

- Total plans completed: 0 (this milestone)
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 7. WASM Node Harness + Local Green | 0/TBD | - | - |
| 8. CI Gating + Dedicated Workflow + Badge | 0/TBD | - | - |

**Recent Trend:**

- Last 5 plans: none yet this milestone
- Trend: -

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 07-wasm-node-harness-local-green P01 | 180 | 4 tasks | 7 files |
| Phase 08 P01 | 3 | 3 tasks | 3 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap (v0.9.0): two phases in strict dependency order — Phase 7 stands up the Node harness and gets the full `test/sql` suite green locally (WASM-01/02/03), including engine-version pinning (DEP-01, an ABI prerequisite the harness depends on) and the openssl `!wasm32` tidy (DEP-02, small/self-contained); Phase 8 wires the green harness into gating CI + a dedicated workflow + README badge (CI-01/02/03).
- Reference implementation to port: anofox-statistics PR #131 (`test/wasm/run.mjs`, `test/wasm/sqllogic.mjs`, the `wasm-runtime-test` job, `WasmTest.yml`).
- This is a CI/infra hardening milestone — no new SQL functions, no Rust crate changes; work lives in `test/wasm/`, `.github/workflows/`, `vcpkg.json`, and README.
- [Phase 07]: Pinned @duckdb/duckdb-wasm@1.33.1-dev64.0 (engine v1.5.5) and web-worker@1.2.0; pthreadWorker=null mandatory for eh bundle
- [Phase 07]: SKIP_FILES contains only 4 structurally WASM-infeasible files; 23 remaining failures are artifact-API drift tracked in SUMMARY
- [Phase 08]: Gate CI on curated subset only (no --all): 184 pre-existing test-debt failures under --all must not permanently red the build
- [Phase 08]: Same-run vs cross-run artifact download split: gating job (same run) vs WasmTest.yml (cross-run via workflow_run.id + github-token)

### Known Gotchas (issue #255 — must shape Phase 7 plans)

- `@duckdb/duckdb-wasm` npm version ≠ engine version — must ABI-match the built DuckDB version or `LOAD` fails.
- DECIMAL renders unscaled in duckdb-wasm Arrow-JS → format results through `::VARCHAR` to match native sqllogictest output.
- Per-file catalog isolation — re-open the DB + re-`LOAD` the extension per `.test` file.
- Node pins `web-worker@1.2.0`, `pthreadWorker=null` for the `eh` bundle, and uses `FORCE INSTALL`.
- 66 `.test` files in the suite; WASM-03 permits an explicit, documented skip-list for tests infeasible on WASM.

### Pending Todos

None yet.

### Blockers/Concerns

- None. Exact pinned `@duckdb/duckdb-wasm` version is a plan-phase determination (must match the built DuckDB version), not a blocker.

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| WASM (future) | Browser-based (not just Node) WASM E2E harness (WASM-F1) | Deferred | 2026-09-01 | v0.9.0 |
| WASM (future) | Shared-memory `wasm_threads` build — blocked upstream (WASM-F2) | Deferred | 2026-09-01 | v0.9.0 |
| ENS (future) | Custom hand-supplied combination weights (ENS-F1) | Deferred | 2026-08-30 | v0.8.0 |
| ENS (future) | Panel/multivariate ensembling (ENS-F2) | Deferred | 2026-08-30 | v0.8.0 |

## Session Continuity

**Resume file:** None

Last session: 2026-09-01T22:28:55.918Z
Stopped at: Completed 08-01-ci-gating-wasm-badge-PLAN.md (Tasks 1-3 committed; Task 4 = blocking-human checkpoint awaiting operator CI push)
Resume: /gsd-plan-phase 7 to plan the WASM Node harness + local green.

## Operator Next Steps

- Plan Phase 7 with /gsd-plan-phase 7
