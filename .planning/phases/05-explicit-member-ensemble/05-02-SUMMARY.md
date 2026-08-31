---
phase: 05-explicit-member-ensemble
plan: "02"
subsystem: forecasting
tags:
  - ensemble
  - explicit-members
  - documentation
  - sql-surface
  - examples

dependency_graph:
  requires:
    - phase: 05-explicit-member-ensemble
      plan: "01"
      provides: "ts_forecast_ensemble_by macro + build_forecaster (36-variant) + FFI export + ScalarFunction — full wiring"
  provides:
    - "examples/forecasting/ensemble_explicit.sql — full ENS-02 DoD example: canonical Mean cross-check (mismatch_count=0), six-method smoke test, 26-member allowlist sample, error-path demonstrations"
    - "docs/reference/models/ensemble/ensemble_explicit.md — full reference doc with 26 supported members, 10 blocked members, 6 combination methods, seasonal fallback, error examples"
    - "docs/api/07-forecasting.md ts_forecast_ensemble_by entry — signature, param table, 3 verified examples, cross-reference"
    - "ENS-02 DoD complete: canonical ['AutoARIMA','AutoETS','Theta'] Mean cross-check verified end-to-end"
  affects:
    - "Phase 6 (EPI-01): ensemble prediction intervals — yhat_lower/yhat_upper currently NULL"
    - "Phase 6 (INSP-01): member/weight introspection — Ensemble::weights() reachable"

actuals:
  tokens: 8200
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "PR #230 doc-snippet verification: all SQL snippets in reference doc and API entry run through built extension before committing"
    - "Expected-error idiom: error-path demonstrations placed at END of example under EXPECTED ERRORS banner with documented messages; sections 1-3 run cleanly as a pipe"

key-files:
  created:
    - "examples/forecasting/ensemble_explicit.sql — full 4-section ENS-02 DoD example"
    - "docs/reference/models/ensemble/ensemble_explicit.md — reference doc (mirrors autoensemble.md)"
  modified:
    - "docs/api/07-forecasting.md — ts_forecast_ensemble_by section added after AutoEnsemble"

key-decisions:
  - "Section 4 error tests placed at END of example file (not try/catch wrapped): the project has no in-script error-capture idiom; placing them last means Sections 1-3 run cleanly as a pipe; Section 4 demonstrates each error with a clear comment documenting the expected message"
  - "Canonical DoD member set ['AutoARIMA','AutoETS','Theta'] cross-check passes (mismatch_count=0, diff=0.0 exact) — Theta on the 60-obs linear series is deterministic, making the Mean check exact to machine precision"
  - "All doc snippets verified end-to-end before committing (PR #230 rule): 4 snippets in reference doc + 3 snippets in API entry, all run through built extension"

patterns-established:
  - "expected-error-at-end pattern: place expected-failure SQL statements at end of example SQL file under -- EXPECTED ERRORS banner; document expected message in comment; sections that must pass cleanly come before the error section"

requirements-completed:
  - ENS-02

coverage:
  - id: D1
    description: "Canonical ['AutoARIMA','AutoETS','Theta'] Mean cross-check: ensemble yhat == arithmetic mean of three independent member forecasts within 1e-6 (mismatch_count=0)"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit.sql — Section 1 mismatch_count query returns 0 (diff=0.0 exact on all 5 steps)"
        status: pass
    human_judgment: false
  - id: D2
    description: "All six combination methods (mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive) produce finite non-NULL yhat; yhat_lower/yhat_upper NULL"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit.sql — Section 2 smoke assertions: 0 NULL/non-finite rows, 0 non-null intervals"
        status: pass
    human_judgment: false
  - id: D3
    description: "Representative member-allowlist samples across Tier 1/2/3 (Naive+SES, AutoARIMA+Theta+Holt, CrostonClassic+ADIDA+IMAPA, SeasonalNaive+HoltWinters with seasonal_period=12) all return finite non-NULL yhat"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit.sql — Section 3 smoke assertions: all 4 sample groups report 0 fails"
        status: pass
    human_judgment: false
  - id: D4
    description: "Error paths raise clear InvalidParameter: (a) unknown member 'NotAModel' names it, (b) <2 members mentions the 2-member minimum, (c) blocked 'GARCH' names GARCH and suggests AutoARIMA/AutoETS"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit.sql — Section 4 error statements, verified by running each against built extension individually; messages match documentation"
        status: pass
    human_judgment: false
  - id: D5
    description: "docs/reference/models/ensemble/ensemble_explicit.md: lists 26 supported members (Tier 1/2/3), 10 blocked members with reasons/alternatives, 6 combination methods, seasonal_period fallback, NULL intervals (EPI-01 deferred), error examples"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "All 4 SQL snippets in ensemble_explicit.md verified against built extension (PR #230 rule)"
        status: pass
    human_judgment: false
  - id: D6
    description: "docs/api/07-forecasting.md contains ts_forecast_ensemble_by entry with signature, parameter table, 3 verified examples, and cross-reference to reference doc"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "All 3 SQL snippets in API entry verified against built extension (PR #230 rule)"
        status: pass
    human_judgment: false

duration: "6m 2s"
completed: "2026-08-31"
status: complete
---

# Phase 05 Plan 02: Explicit-Member Ensemble Full DoD + Documentation Summary

**Full ENS-02 DoD delivered: canonical ['AutoARIMA','AutoETS','Theta'] Mean cross-check passes (mismatch_count=0, diff=0.0), all six combination methods smoke-tested, 26-member allowlist sampled, three error paths verified, reference doc + API entry written with all snippets run through built extension**

## Performance

- **Duration:** 6 min 2s
- **Started:** 2026-08-31T09:37:16Z
- **Completed:** 2026-08-31T09:43:18Z
- **Tasks:** 2 / 2
- **Files created/modified:** 3

## Accomplishments

- `examples/forecasting/ensemble_explicit.sql` — 4-section DoD example: Section 1 canonical Mean cross-check (mismatch_count=0 on `['AutoARIMA','AutoETS','Theta']`); Section 2 six-method smoke (all methods → finite non-NULL yhat; NULL intervals confirmed); Section 3 per-tier member samples (Tier 1/2/3 including seasonal `['SeasonalNaive','HoltWinters']` with `seasonal_period=12`); Section 4 expected-error demonstrations (unknown member, <2 members, blocked GARCH)
- `docs/reference/models/ensemble/ensemble_explicit.md` — reference doc mirroring `autoensemble.md`: 26 supported members (Tier 1/2/3), 10 blocked members with reasons and alternatives, 6 combination methods + aliases, seasonal_period fallback behavior (Pitfall 5 documented), NULL intervals (EPI-01 deferred), error examples; all 4 SQL snippets verified against built extension
- `docs/api/07-forecasting.md` — `ts_forecast_ensemble_by` entry in Ensemble Forecasting section: signature, param table, 3 verified examples, cross-reference to reference doc; all 3 snippets verified

## Task Commits

1. **Task 1: Full DoD example** — `2d614b9` (feat)
2. **Task 2: Documentation — reference doc + API entry** — `46cd632` (docs)

## Files Created/Modified

- `examples/forecasting/ensemble_explicit.sql` — new: 4-section ENS-02 DoD example (+312 lines)
- `docs/reference/models/ensemble/ensemble_explicit.md` — new: reference doc (+270 lines)
- `docs/api/07-forecasting.md` — modified: ts_forecast_ensemble_by entry added (+95 lines)

## Decisions Made

- **Section 4 error placement:** The project has no in-script error-capture idiom (no try/catch, no `.bail off`). Placed the three expected-error SELECT statements at the END of the example file under `-- EXPECTED ERRORS` with comments documenting the expected message for each. Sections 1-3 run cleanly as a pipe; Section 4 is documentation of expected behavior for reviewers running statements one-by-one.
- **Canonical DoD member set is exact:** `['AutoARIMA','AutoETS','Theta']` on the 60-obs linear series produces diff=0.0 (not just < 1e-6) — AutoARIMA selects ARIMA(0,1,0) deterministically on a trend series, giving integer-exact arithmetic.
- **PR #230 rule enforced:** Every SQL snippet in both docs was run through `./build/release/duckdb -unsigned` and produces the shown output. No snippet was eyeballed.

## Deviations from Plan

None — plan executed exactly as written. No source-level changes required (05-01 wiring was complete). All assertions passed on first run.

## Known Stubs

None. The full 26-member allowlist is proven at the compiler level (exhaustive `build_forecaster` match from 05-01) and smoke-tested at runtime by per-tier representative samples in Section 3.

The following remain deferred per plan design (not stubs):
- Prediction intervals (`yhat_lower`/`yhat_upper`) — Phase 6, EPI-01
- Member/weight introspection (`Ensemble::weights()`) — Phase 6, INSP-01
- Per-member parameter maps — future milestone

## Threat Surface Scan

No new threat surface. All doc snippets use synthetic in-memory `range()` series with no real/PII data (T-05-05, accepted). All SQL snippets verified against built extension before committing (T-05-06, mitigated). No new source code paths added.

## Issues Encountered

None. The full example ran cleanly on first attempt for Sections 1-3. Error paths verified individually with exact message matching documentation.

## Next Phase Readiness

- ENS-02 is fully complete: wiring (05-01) + DoD example + docs (05-02)
- Phase 6 (EPI-01): `Ensemble::predict_with_intervals()` is reachable — the current path uses `extract_forecast` (point-only); switching to `predict_with_intervals` is a localized change in `forecast_explicit_ensemble`
- Phase 6 (INSP-01): `Ensemble::weights()` / `.method()` accessors are available — introspection path is already reachable from the FFI boundary

## Self-Check: PASSED

Files exist:
- `examples/forecasting/ensemble_explicit.sql` — FOUND
- `docs/reference/models/ensemble/ensemble_explicit.md` — FOUND
- `docs/api/07-forecasting.md` — FOUND

Commits exist in git log:
- `2d614b9` — FOUND (feat(05-02): add full ENS-02 DoD example)
- `46cd632` — FOUND (docs(05-02): add ts_forecast_ensemble_by reference doc + API entry)

Content checks:
- `grep -c 'ts_forecast_ensemble_by' ensemble_explicit.md` → 18 (PASS)
- `grep -c 'ts_forecast_ensemble_by' 07-forecasting.md` → 6 (PASS)
- `grep -c 'yhat_lower.*NULL\|EPI-01' ensemble_explicit.md` → 2+ (PASS — NULL intervals documented)
