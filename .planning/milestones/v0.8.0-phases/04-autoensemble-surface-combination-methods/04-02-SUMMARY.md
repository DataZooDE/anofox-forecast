---
phase: 04-autoensemble-surface-combination-methods
plan: 02
subsystem: docs
tags: [autoensemble, ensemble, ts_forecast_by, combination-methods, docs, examples]

requires:
  - phase: 04-01
    provides: "Wired AutoEnsemble surface (method-string dispatch, top_k/combination_method params, built extension) + parse_combination_method accepting all six methods"
provides:
  - "examples/forecasting/autoensemble.sql extended: six-method smoke-test assertion block (COMB-01..04) + Mean-vs-Median demonstrability on a skewed series"
  - "docs/reference/models/ensemble/autoensemble.md (new reference doc for the AutoEnsemble surface)"
  - "docs/api/07-forecasting.md updated with AutoEnsemble models-table row + dedicated section"
affects: [Phase 5 explicit-member ensemble, Phase 6 ensemble intervals + introspection]

actuals:
  tokens: 12000
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns: ["Doc snippet PR #230 rule: every SQL block extracted and run against the built extension"]

key-files:
  created:
    - docs/reference/models/ensemble/autoensemble.md
  modified:
    - examples/forecasting/autoensemble.sql
    - docs/api/07-forecasting.md

key-decisions:
  - "Plan 02 added ZERO source wiring — parse_combination_method (Plan 01) already accepts all six methods; Plan 02 exercises and documents."
  - "Illustrative doc snippets reference example tables (sales/weekly_sales/product_id); the self-contained example file (examples/forecasting/autoensemble.sql) carries the runnable proof."

patterns-established:
  - "Six-method assertion via SQL UNION ALL producing a per-method ok=boolean column + a 0-row error guard."

requirements-completed: [COMB-01, COMB-02, COMB-03, COMB-04]

coverage:
  - id: D1
    description: "All six combination methods (mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive) produce finite non-NULL yhat per step via ts_forecast_by 'AutoEnsemble'"
    requirement: "COMB-01"
    verification:
      - kind: e2e
        ref: "examples/forecasting/autoensemble.sql Section 3 — six-method smoke test, all ok=true, 0 error rows"
        status: pass
  - id: D2
    description: "WeightedMSE / InverseAIC error/information-weighted combination selectable and valid"
    requirement: "COMB-02"
    verification:
      - kind: e2e
        ref: "examples/forecasting/autoensemble.sql Section 3 — weighted_mse + inverse_aic rows ok=true"
        status: pass
  - id: D3
    description: "Stacking (learned holdout weights) selectable and valid"
    requirement: "COMB-03"
    verification:
      - kind: e2e
        ref: "examples/forecasting/autoensemble.sql Section 3 — stacking row ok=true"
        status: pass
  - id: D4
    description: "HorizonAdaptive (per-step rolling-origin weights) selectable and valid"
    requirement: "COMB-04"
    verification:
      - kind: e2e
        ref: "examples/forecasting/autoensemble.sql Section 3 — horizon_adaptive row ok=true"
        status: pass
  - id: D5
    description: "Mean vs Median produce visibly different point forecasts on a skewed series (COMB-01 demonstrability)"
    requirement: "COMB-01"
    verification:
      - kind: e2e
        ref: "examples/forecasting/autoensemble.sql Section 4 — delta 1.45–2.69 across steps"
        status: pass
  - id: D6
    description: "AutoEnsemble surface documented (reference + API doc); every SQL snippet valid against built extension"
    verification:
      - kind: manual_procedural
        ref: "docs/reference/models/ensemble/autoensemble.md + docs/api/07-forecasting.md — snippets extracted and run; API syntax confirmed (ts_forecast_by(..., 'AutoEnsemble', ..., params:={combination_method, top_k, seasonal_period}))"
        status: pass
---

## Accomplishments

- Extended `examples/forecasting/autoensemble.sql` with a six-method assertion block (Section 3) and a Mean-vs-Median demonstrability section (Section 4) on a right-skewed series.
- Created `docs/reference/models/ensemble/autoensemble.md`: method-string dispatch, the three params (`top_k`, `combination_method`, `seasonal_period`), the six-method table, the Mean cross-check, the NULL-interval note (Phase 6 / EPI-01 deferral), fewer-than-top_k behavior, and the fixed ARIMA/ETS/Theta family list.
- Updated `docs/api/07-forecasting.md`: AutoEnsemble added to the models table plus a dedicated section with the param surface and a runnable example.

## Verification (independently re-run by orchestrator against the built extension)

- Section 1 (DoD internal-consistency cross-check): `AutoEnsemble(mean, top_k=3)` == arithmetic mean of independent AutoARIMA+AutoETS+AutoTheta — diff = 0.0, match = true on all 5 steps; `yhat_lower`/`yhat_upper` NULL.
- Section 3: all six methods return finite non-NULL `yhat`, `ok = true`, 0 error rows.
- Section 4: Mean vs Median delta 1.45–2.69 (visibly different).
- Doc snippets: illustrative blocks reference example tables; the AutoEnsemble API syntax was confirmed to bind and forecast against a matching table.

## Deviations

- **Executor mis-report reconciled by orchestrator.** The gsd-executor for this plan reported "PLAN COMPLETE" citing commit `a37f8c9` and a `04-02-SUMMARY.md` that were never actually written — the Task-2 docs were left uncommitted on disk and no SUMMARY existed. The orchestrator independently verified the on-disk work (example + doc snippets run clean against the built extension), then committed the docs (`5b90736`) and authored this SUMMARY. No re-execution of the (correct) technical work was needed.

## Commits

- `0d0589e` — feat(04-02): add six-method assertion + Mean-vs-Median demonstrability to autoensemble.sql
- `5b90736` — docs(04-02): document AutoEnsemble surface + six combination methods (orchestrator-reconciled)
