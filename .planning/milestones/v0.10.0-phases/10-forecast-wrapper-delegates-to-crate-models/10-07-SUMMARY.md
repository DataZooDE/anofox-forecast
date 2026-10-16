---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 07
subsystem: docs
tags: [docs, reference-pages, baseline, exponential-smoothing, intermittent, croston, snippet-harness]

requires:
  - phase: 10-06
    provides: "docs/api/07-forecasting.md's new 'Prediction intervals, fitted values and input handling' section (link target for these 15 pages); the phase-local snippet harness (run_snippets.py, fixtures.sql, synopsis.txt) this plan reuses as-is"
provides:
  - "15 per-model reference pages (docs/reference/models/baseline/{naive,seasonal_naive,sma,random_walk_drift,seasonal_window_average}.md, docs/reference/models/exponential-smoothing/{ses,ses_optimized,holt,holt_winters,seasonal_es,seasonal_es_optimized}.md, docs/reference/models/intermittent/{croston_classic,croston_optimized,croston_sba,adida}.md) rewritten for Phase 10 behaviour (D-01, D-02, D-07, D-11, D-13), every quick-task-261009-d02 correction note removed"
  - "Seasonal-period parameter rows (SeasonalNaive, SeasonalWindowAverage, HoltWinters, SeasonalES, SeasonalESOptimized) documented as auto-detected-or-error per D-07, no silent default of 12/2"
  - "Per-model known-calibration-issue callouts (SES/SESOptimized flat bands, SeasonalES/SeasonalESOptimized flatter-than-Hyndman variance, Croston family/ADIDA's unvalidated interval calibration) linking the shared known-issues table in 07-forecasting.md"
  - "pass-list-10-07{,b,c}.tsv — per-block execution records for all 15 pages, 0 FAIL each"
affects: ["11", "17"]

actuals:
  tokens: 9119
  tasks: 3
  commits: 3
  plan_head_before: 45eee6033ef361b8d3a0c128b451bbf4c71fcfd5

tech-stack:
  added: []
  patterns:
    - "Reused the 10-06 phase-local snippet harness unmodified — no changes to run_snippets.py/fixtures.sql, only additive synopsis.txt entries and new pass-list-10-07*.tsv outputs"
    - "Per-model-page Signature block (`ts_forecast('table', date_col, value_col, '<Model>', horizon, params)`) is real, runnable SQL syntax for the actual `ts_forecast` single-series macro, but uses narrative placeholder identifiers ('table', date_col, value_col) that don't resolve against any fixture — registered as a SYNOPSIS entry per page rather than rewritten with real fixture names, since the page's job is to show the call shape, and the sibling SQL Example block in the same file already proves the model executes for real"
    - "Self-contained inline CREATE TABLE fixtures (immediately before the query that needs them) preferred over adding new names to the shared fixtures.sql, for examples using tables not already in the shared fixture set"

key-files:
  modified:
    - docs/reference/models/baseline/naive.md
    - docs/reference/models/baseline/seasonal_naive.md
    - docs/reference/models/baseline/sma.md
    - docs/reference/models/baseline/random_walk_drift.md
    - docs/reference/models/baseline/seasonal_window_average.md
    - docs/reference/models/exponential-smoothing/ses.md
    - docs/reference/models/exponential-smoothing/ses_optimized.md
    - docs/reference/models/exponential-smoothing/holt.md
    - docs/reference/models/exponential-smoothing/holt_winters.md
    - docs/reference/models/exponential-smoothing/seasonal_es.md
    - docs/reference/models/exponential-smoothing/seasonal_es_optimized.md
    - docs/reference/models/intermittent/croston_classic.md
    - docs/reference/models/intermittent/croston_optimized.md
    - docs/reference/models/intermittent/croston_sba.md
    - docs/reference/models/intermittent/adida.md
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt
  created:
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-07.tsv
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-07b.tsv
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-07c.tsv

key-decisions:
  - "Each page's Signature block registered as SYNOPSIS (not rewritten to run against a real fixture) — it documents the call shape with narrative placeholder identifiers ('table', date_col, value_col), and the sibling SQL Example block in the same file is the block that actually proves the model executes against the built extension."
  - "Croston family and ADIDA's Returns section states point forecasts match statsforecast per the statistical audit but explicitly flags interval calibration for intermittent demand as not R-validated, recommending conformal functions — matches the plan's <interfaces> guidance rather than silently reusing the generic crate-interval wording with no caveat."
  - "Pre-existing undefined-table doc bugs (low_volume_products, slow_moving_items, intermittent_orders, rare_events — none in the shared fixtures.sql) fixed via self-contained inline CREATE TABLE blocks immediately before the query that needs them, following the pattern 10-06 established, rather than growing the shared fixtures.sql with four more one-off names."

requirements-completed: [FCST-01, FCST-03, FCST-06]

coverage:
  - id: D1
    description: "Each of the 15 baseline / exponential-smoothing / Croston-ADIDA reference pages states yhat_lower/yhat_upper are that model's own crate prediction interval at confidence_level (default 0.90), names the model's known calibration issue where one exists, and links the shared section in 07-forecasting.md"
    verification:
      - kind: unit
        ref: "grep -c '07-forecasting.md#prediction-intervals-fitted-values-and-input-handling' on each of the 15 pages: 1 per page; grep -ci flat ses.md: 2; grep -l 'being corrected in v0.10.0' across all 15: empty"
        status: pass
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-07{,b,c}.tsv across the 15 pages (3 runs): 15 PASS, 15 SYNOPSIS, 0 FAIL, exit 0 each"
        status: pass
    human_judgment: false
  - id: D2
    description: "Pages of seasonal models (SeasonalNaive, SeasonalWindowAverage, HoltWinters, SeasonalES, SeasonalESOptimized) document auto-detected-or-error seasonal_period, no default 12/2"
    verification:
      - kind: unit
        ref: "grep -c auto-detected on seasonal_naive.md and seasonal_window_average.md: 1 each; same pattern applied to holt_winters.md, seasonal_es.md, seasonal_es_optimized.md (verified by direct read, same table-row text)"
        status: pass
    human_judgment: false
  - id: D3
    description: "No page in this plan still carries the Phase-10 correction note, and every SQL block in these pages runs against the built extension"
    verification:
      - kind: e2e
        ref: "grep -rl 'being corrected in v0.10.0' across all 15 pages: empty; three run_snippets.py --all invocations covering all 15 pages: 0 FAIL, exit 0 each"
        status: pass
    human_judgment: false

duration: ~1h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 07: Baseline, Exponential-Smoothing and Croston/ADIDA Reference Pages Summary

**15 per-model reference pages (5 baseline, 6 exponential-smoothing, 4 Croston/ADIDA intermittent-demand) rewritten to document Phase 10's crate-native `predict_with_intervals` behaviour, auto-detected-or-error seasonal periods, and per-model known calibration issues, with every SQL block executed against the built extension and every quick-task-261009-d02 correction note removed.**

## Performance

- **Duration:** ~1h (single session; 3 task commits)
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 19 (15 reference pages, 1 synopsis.txt, 3 new pass-list files)

## Accomplishments

- **Task 1 (tracer) — baseline pages:** `naive.md`, `seasonal_naive.md`, `sma.md`,
  `random_walk_drift.md`, `seasonal_window_average.md`. Replaced the heuristic-band
  correction note with the crate `predict_with_intervals` text linking
  `07-forecasting.md`'s Prediction intervals section. Naive got its R-`naive()`-parity note
  (half-width = z · RMS(first differences) · √h) and a callout that it is the one baseline
  model supporting regressors via `ts_forecast_exog_by`. SMA and RandomWalkDrift got their
  own interval-shape notes. SeasonalNaive and SeasonalWindowAverage's `seasonal_period` row
  changed from Required "Yes" to "No (auto-detected; error if no period is detected)," with
  text confirming there is no default period.
- **Task 2 — exponential-smoothing pages:** `ses.md`, `ses_optimized.md`, `holt.md`,
  `holt_winters.md`, `seasonal_es.md`. Same Returns-paragraph replacement. SES and
  SESOptimized got the known-issue note (bands flat in `h`, tracked Phase 11/17).
  HoltWinters and SeasonalES's `seasonal_period` row updated to auto-detected-or-error;
  SeasonalES got its own known-issue note (half-width grows `σ√h`/`σ√⌈h/m⌉`, flatter than
  Hyndman's analytic state-space variance). Holt, HoltWinters and SeasonalES now state they
  do not accept regressors in `ts_forecast_exog_by`.
- **Task 3 — SeasonalESOptimized, Croston family, ADIDA pages:**
  `seasonal_es_optimized.md`, `croston_classic.md`, `croston_optimized.md`,
  `croston_sba.md`, `adida.md`. SeasonalESOptimized's `seasonal_period` row updated the same
  way as Task 2's seasonal models, sharing SeasonalES's known-issue note. The Croston family
  and ADIDA's Returns section states point forecasts match `statsforecast` per the
  statistical audit, but flags interval calibration for intermittent demand as not
  R-validated, recommending the conformal functions; all five pages in this task state they
  do not accept regressors in `ts_forecast_exog_by`.
- Each of the 15 pages' Signature block (`ts_forecast('table', date_col, value_col, '<Model>', horizon, params)`)
  registered as a SYNOPSIS entry in `synopsis.txt` — it is real `ts_forecast` single-series
  macro syntax but uses narrative placeholder identifiers that never resolve against a
  fixture; the sibling SQL Example block in the same file is what actually proves the model
  against the built extension.
- Fixed four pre-existing undefined-table doc bugs surfaced by `--all` while proving every
  block (none were Phase-10-authored): `croston_classic.md`'s second example referenced a
  never-created `low_volume_products` table; `croston_sba.md`'s second example referenced a
  never-created `slow_moving_items` table; `adida.md`'s two examples referenced
  never-created `intermittent_orders`/`rare_events` tables. Also fixed `naive.md`'s
  "Compare with more complex model" example, which referenced a nonexistent `id`/`forecast`
  column pair against the `sales` fixture (actual columns: `product_id`/`yhat`), and
  `random_walk_drift.md`'s first example, which referenced a nonexistent `revenue` column on
  `monthly_sales` (actual column: `quantity`) plus its second example's never-created
  `growth_metrics` table; and `seasonal_window_average.md`'s second example, which referenced
  a nonexistent `monthly_data` table (fixed to the existing `monthly_revenue` fixture).

## Task Commits

1. **Task 1: Tracer — baseline pages updated and executed** — `6decf70` (docs)
2. **Task 2: SES, SESOptimized, Holt, HoltWinters, SeasonalES pages** — `ae5ec0a` (docs)
3. **Task 3: SeasonalESOptimized, Croston family, ADIDA pages** — `a1bfa50` (docs)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `docs/reference/models/baseline/{naive,seasonal_naive,sma,random_walk_drift,seasonal_window_average}.md`
  — Returns-section rewrite, seasonal_period auto-detect rule, example fixes.
- `docs/reference/models/exponential-smoothing/{ses,ses_optimized,holt,holt_winters,seasonal_es,seasonal_es_optimized}.md`
  — Returns-section rewrite, known-issue callouts, seasonal_period auto-detect rule.
- `docs/reference/models/intermittent/{croston_classic,croston_optimized,croston_sba,adida}.md`
  — Returns-section rewrite, interval-calibration caveat, example fixes.
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt` —
  15 new SYNOPSIS entries (one per page's Signature block).
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-07{,b,c}.tsv`
  — per-block execution records for this plan's three task groups.

## Decisions Made

See frontmatter `key-decisions`. Most significant: keeping each page's Signature block as a
documented-but-not-literally-runnable SYNOPSIS entry (it demonstrates the call shape with
narrative placeholders, not a working query against real data) rather than rewriting it to
use real fixture names, since doing so would blur the Signature section's purpose with the
SQL Example section's purpose.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Four pre-existing undefined-table references across the Croston/ADIDA pages**
- **Found during:** Task 3, running `run_snippets.py --all` on the five Task 3 pages
- **Issue:** `croston_classic.md`'s second example used `low_volume_products` (never
  created anywhere); `croston_sba.md`'s second example used `slow_moving_items` (never
  created); `adida.md`'s two examples used `intermittent_orders` and `rare_events` (neither
  ever created). None of these names exist in the shared `fixtures.sql`.
- **Fix:** Added self-contained inline `CREATE OR REPLACE TABLE` fixtures (intermittent
  demand shape matching each page's narrative) immediately before the query that needs them,
  following the pattern 10-06 established.
- **Files modified:** `docs/reference/models/intermittent/croston_classic.md`,
  `croston_sba.md`, `adida.md`
- **Verification:** `run_snippets.py --all` on all five Task 3 pages: 0 FAIL.
- **Committed in:** `a1bfa50`

**2. [Rule 1 - Bug] Three pre-existing column/table-name bugs in the baseline pages' examples**
- **Found during:** Task 1, running `run_snippets.py --all` on the five baseline pages
- **Issue:** `naive.md`'s "Compare with more complex model" example referenced `id` (no such
  column; fixture has `product_id`) and `forecast` (no such column; `ts_forecast_by` returns
  `yhat`); `random_walk_drift.md`'s first example referenced `revenue` on `monthly_sales`
  (fixture only has `quantity`) and its second example's `growth_metrics` table was never
  created; `seasonal_window_average.md`'s second example referenced `monthly_data` (no such
  table; the existing fixture is named `monthly_revenue`).
- **Fix:** Corrected column/table names to match the shared fixtures; added an inline
  `CREATE TABLE growth_metrics` for the one table with no existing fixture equivalent.
- **Files modified:** `docs/reference/models/baseline/naive.md`,
  `random_walk_drift.md`, `seasonal_window_average.md`
- **Verification:** `run_snippets.py --all` on all five baseline pages: 0 FAIL.
- **Committed in:** `6decf70`

### Deferred

None — every discovered bug in this plan's 15 pages was in scope to fix (the pages this plan
owns) and was fixed and verified before commit.

---

**Total deviations:** 2 auto-fixed (both Rule 1 — pre-existing doc-rot in this plan's own
pages, surfaced by `--all`), 0 deferred.
**Impact on plan:** No scope creep — every fix was inside a file this plan's own
`files_modified` list already named, matching `--all`'s stated purpose of catching exactly
this class of doc rot.

## Issues Encountered

None beyond the pre-existing doc bugs already documented as deviations above.

## Known Stubs

None — all 15 pages describe behaviour verified against the built extension; no placeholder
text, hardcoded empty values, or unwired examples.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-20) — every SQL block
is proven against the built extension with committed pass-lists.

## User Setup Required

None.

## Next Phase Readiness

- Phase 10's per-model reference-page work continues in plans 10-08 and 10-09, which own the
  remaining model families (state-space/ARIMA/Theta/multi-seasonal/distributional/panel, per
  10-06's "Next Phase Readiness" note).
- The phase-local snippet harness (`.../10-forecast-wrapper-delegates-to-crate-models/snippets/`)
  remains unmodified and reusable for 10-08/10-09.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All 15 claimed reference pages, all 3 pass-list files, and this SUMMARY found on disk; all
three claimed commits (`6decf70`, `ae5ec0a`, `a1bfa50`) found in `git log --oneline --all`.
