---
phase: 06-ensemble-intervals-introspection
plan: "02"
subsystem: forecasting
tags:
  - ensemble
  - conformal
  - epi-01
  - insp-01
  - examples
  - docs

dependency_graph:
  requires:
    - phase: 06-ensemble-intervals-introspection
      plan: "01"
      provides: "ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by macros in built extension"
    - phase: prior
      provides: "conformal surface (ts_cv_folds_by, ts_conformal_calibrate, ts_conformal_apply_by, _ts_forecast_scalar, _ts_forecast_ensemble_native)"
  provides:
    - "examples/forecasting/ensemble_intervals.sql — EPI-01 DoD (two sections, both bad_rows=0)"
    - "examples/forecasting/ensemble_inspect.sql — INSP-01 full DoD (4 sections, 8 assertions all 0)"
    - "docs/api/11-conformal-prediction.md — ensemble conformal section (EPI-01)"
    - "docs/api/07-forecasting.md — ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by entries"
    - "docs/reference/models/ensemble/ensemble_inspect.md — new introspection reference doc"
  affects:
    - "Phase 6 milestone: EPI-01 + INSP-01 fully closed with verified examples + docs"

actuals:
  tokens: 18000
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "_ts_forecast_scalar('AutoEnsemble') as manual-fold workaround for ts_cv_forecast_by crash"
    - "Manual per-fold loop via ts_cv_folds_by + scalar function per fold → ts_conformal_calibrate"
    - "CROSS JOIN global conformity_score to apply intervals to forecast rows"
    - "MAP{'alpha': '0.1'} syntax for ts_conformal_calibrate (STRUCT syntax requires json extension; MAP avoids it)"

key-files:
  created:
    - "examples/forecasting/ensemble_intervals.sql — EPI-01 DoD (both sections verified)"
    - "examples/forecasting/ensemble_inspect.sql — INSP-01 full DoD (4 sections)"
    - "docs/reference/models/ensemble/ensemble_inspect.md — introspection reference doc"
  modified:
    - "docs/api/11-conformal-prediction.md — ensemble conformal section appended"
    - "docs/api/07-forecasting.md — ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by entries"

key-decisions:
  - "AutoEnsemble Section 1 uses _ts_forecast_scalar (manual fold) not ts_cv_forecast_by: ts_cv_forecast_by('AutoEnsemble') segfaults in the built 0.15.3 extension; _ts_forecast_scalar works correctly and achieves the same calibration"
  - "ts_conformal_calibrate params use MAP{'alpha': '0.1'} not STRUCT syntax: STRUCT {alpha: 0.1} requires the json extension; MAP avoids this dependency cleanly"
  - "All limitations documented honestly in example headers and docs (not hidden)"

requirements-completed:
  - EPI-01
  - INSP-01

coverage:
  - id: EPI-01-AutoEnsemble
    description: "ensemble_intervals.sql Section 1: AutoEnsemble conformal intervals — both bad_rows == 0"
    requirement: EPI-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_intervals.sql Section 1 — bad_rows=0, interval_width=0.734845"
        status: pass
    human_judgment: false
  - id: EPI-01-Explicit
    description: "ensemble_intervals.sql Section 2: explicit-member conformal intervals — both bad_rows == 0"
    requirement: EPI-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_intervals.sql Section 2 — bad_rows=0, interval_width=1.708277"
        status: pass
    human_judgment: false
  - id: INSP-01-Mean
    description: "ensemble_inspect.sql Section 1: explicit Mean unequal_mean_weights=0, bad_weight_sum=0"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect.sql Section 1"
        status: pass
    human_judgment: false
  - id: INSP-01-WeightedMSE
    description: "ensemble_inspect.sql Section 2: explicit WeightedMSE negative_weights=0, bad_weight_sum=0"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect.sql Section 2"
        status: pass
    human_judgment: false
  - id: INSP-01-AutoMean
    description: "ensemble_inspect.sql Section 3: AutoEnsemble Mean null_weights=0, unequal_weights=0, bad_scores=0, bad_ranks=0"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect.sql Section 3"
        status: pass
    human_judgment: false
  - id: INSP-01-AutoWeightedMSE
    description: "ensemble_inspect.sql Section 4: AutoEnsemble WeightedMSE non_null_weights=0, bad_scores=0"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect.sql Section 4"
        status: pass
    human_judgment: false

duration: "35m"
completed: "2026-08-31"
status: complete
---

# Phase 06 Plan 02: Ensemble Intervals + Introspection Docs (EPI-01, INSP-01) Summary

**EPI-01 conformal example (2 sections, both bad_rows=0) + INSP-01 full DoD example (4 sections, 8 assertions all 0) + 3 docs files — all SQL verified against the built extension; both CV-native limitations documented honestly**

## Performance

- **Duration:** ~35 minutes
- **Completed:** 2026-08-31T13:35:00Z
- **Tasks:** 3 / 3
- **Commits:** 3 task commits + 1 docs commit (pending)
- **Files created:** 3 (ensemble_intervals.sql, ensemble_inspect.sql, ensemble_inspect.md)
- **Files modified:** 2 (11-conformal-prediction.md, 07-forecasting.md)

## Accomplishments

### Task 1 — EPI-01: `examples/forecasting/ensemble_intervals.sql`

- **Section 1 (AutoEnsemble conformal):** Manual per-fold loop using `_ts_forecast_scalar('AutoEnsemble')`
  per fold on `ts_cv_folds_by` output. `ts_cv_forecast_by('AutoEnsemble')` crashes (segfault) in the
  built 0.15.3 extension — this limitation is documented in the file header. The manual loop achieves
  the same calibration. Both bad_rows assertions == 0; non-degenerate interval_width = 0.735.

- **Section 2 (explicit-member conformal):** Manual per-fold loop using `_ts_forecast_ensemble_native`
  per fold (required because `ts_forecast_ensemble_by` is a ScalarFunction, cannot flow through
  `ts_cv_forecast_by`). Both bad_rows assertions == 0; non-degenerate interval_width = 1.708.

- Both three CV-native limitations documented in the file header (ts_cv_forecast_by crash,
  ScalarFunction path, global quantile scope).

### Task 2 — INSP-01 DoD: `examples/forecasting/ensemble_inspect.sql`

- **Section 1 (explicit Mean):** `unequal_mean_weights=0`, `bad_weight_sum=0`
- **Section 2 (explicit WeightedMSE):** `negative_weights=0`, `bad_weight_sum=0`
- **Section 3 (AutoEnsemble Mean):** `null_weights=0`, `unequal_weights=0`, `bad_scores=0`, `bad_ranks=0`
- **Section 4 (AutoEnsemble WeightedMSE):** `non_null_weights=0`, `bad_scores=0`
- Documents the AutoEnsemble weight-NULL crate 0.15.3 limitation and the `''` → `'mean'` mapping.

### Task 3 — Docs

- **`docs/api/11-conformal-prediction.md`:** Ensemble conformal section (EPI-01) with AutoEnsemble
  and explicit-member manual-fold recipes, 3 limitations noted, reference to `predict_with_intervals()`
  as model-native widest-envelope alternative.

- **`docs/api/07-forecasting.md`:** `ts_ensemble_inspect_by` entry (signature, parameter table, weight
  properties by method, verified examples) + `ts_auto_ensemble_inspect_by` entry (signature, weight
  availability table, `rank` column note, row count note, verified examples).

- **`docs/reference/models/ensemble/ensemble_inspect.md`:** New reference doc — full signatures,
  parameter tables, 6-method weight properties, output column reference, DoD verification queries,
  3 limitations (score NULL for explicit-member, AutoEnsemble weight NULL for non-Mean, HorizonAdaptive
  average-weights, `''` → `mean` mapping).

All SQL snippets in all three docs verified against the built extension (PR #230 rule).

## Task Commits

1. **Task 1: EPI-01 ensemble conformal intervals example** — `6dd26d8` (feat)
2. **Task 2: INSP-01 full DoD example** — `45e3249` (feat)
3. **Task 3: Docs — conformal section + API entries + reference doc** — `37716cd` (docs)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Behavior] `ts_cv_forecast_by('AutoEnsemble')` segfaults in built 0.15.3 extension**
- **Found during:** Task 1, Section 1 development
- **Issue:** `ts_cv_forecast_by` with `method='AutoEnsemble'` causes a runtime segfault (exit 139)
  in the built extension. The RESEARCH confirmed from source-reading that the CV native accepts
  'AutoEnsemble' via ForecastOptions.model string, but the actual runtime behavior differs. This
  is a pre-existing bug in the built 0.15.3 extension — not caused by this plan.
- **Fix:** Section 1 uses `_ts_forecast_scalar('AutoEnsemble')` in a manual per-fold loop
  (same pattern as Section 2 with explicit-member). Achieves identical calibration without the crash.
- **Documentation:** All three CVlimitations documented in the file header and docs.
- **Files modified:** `examples/forecasting/ensemble_intervals.sql`
- **Committed in:** 6dd26d8

**2. [Rule 3 - Blocking] `ts_conformal_calibrate` with STRUCT syntax requires `json` extension**
- **Found during:** Task 1 development
- **Issue:** `ts_conformal_calibrate('bt', y, yhat, {alpha: 0.1})` raises
  "Function json_extract_string is not in the catalog" — the STRUCT literal `{alpha: 0.1}`
  requires the `json` extension.
- **Fix:** Use `MAP{'alpha': '0.1'}` instead of `{alpha: 0.1}`. Both syntaxes work equivalently
  for the conformal API; MAP does not require `json`.
- **Files modified:** `examples/forecasting/ensemble_intervals.sql`
- **Committed in:** 6dd26d8

## Known Stubs

None. Both EPI-01 and INSP-01 are fully wired and verified. The following are documented
limitations (not stubs):

- `ts_cv_forecast_by('AutoEnsemble')` crash — pre-existing bug in built 0.15.3 extension;
  workaround in example; limitation documented in examples + docs
- AutoEnsemble `weight IS NULL` for non-Mean — upstream crate 0.15.3 limitation; documented
  in examples + docs + reference doc

## Threat Surface Scan

No new threat surface introduced. This plan adds examples and documentation only — no new
source symbols, no new network paths, no new auth surfaces. All SQL examples use synthetic
in-memory `range()` series (no PII, no real data). T-06-07 (doc SQL drift) mitigated by
running every snippet through the built extension before commit (PR #230 rule).

## Self-Check: PASSED

- `examples/forecasting/ensemble_intervals.sql` exists: confirmed
- `examples/forecasting/ensemble_inspect.sql` exists: confirmed
- `docs/reference/models/ensemble/ensemble_inspect.md` exists: confirmed
- `docs/api/07-forecasting.md` contains `ts_ensemble_inspect_by` and `ts_auto_ensemble_inspect_by`: grep -c = 11
- `docs/api/11-conformal-prediction.md` contains ensemble conformal section: grep -ci = 19
- Both bad_rows assertions == 0 (confirmed by test runs above)
- All 8 INSP-01 assertions == 0 (confirmed by test runs above)
- All 3 task commits verified: 6dd26d8, 45e3249, 37716cd
