---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 08
subsystem: docs
tags: [docs, reference-pages, arima, ets, kalman, theta, mstl, mfles, tbats, snippet-harness]

requires:
  - phase: 10-02
    provides: "real crate ARIMA/SARIMA fit, arima_order/seasonal_order parameters, model_name drift/non-zero-mean suffixes, R Arima() parity numbers"
  - phase: 10-03
    provides: "ETS-without-spec runs crate AutoETS (shared constructor), AutoETS crate-failure error, AutoMFLES/AutoMSTL model_name aliasing, Kalman(spec) naming + intervals, resolve_period no-default-12/2 rule"
  - phase: 10-04
    provides: "forecast_with_exog crate-native routing for AutoARIMA/Theta-family/ETS/MFLES/MSTL (' with regressors' naming), MSTL exog NULL interval bounds"
  - phase: 10-06
    provides: "docs/api/07-forecasting.md's 'Prediction intervals, fitted values and input handling' section (link target for these 15 pages); the phase-local snippet harness (run_snippets.py, fixtures.sql, synopsis.txt) this plan reuses as-is"
provides:
  - "15 per-model reference pages (docs/reference/models/state-space/{arima,auto_arima,ets,auto_ets,kalman}.md, docs/reference/models/theta/{theta,optimized_theta,dynamic_theta,dynamic_optimized_theta,auto_theta}.md, docs/reference/models/multi-seasonal/{mfles,auto_mfles,mstl,auto_mstl,tbats}.md) rewritten for Phase 10 behaviour (D-01, D-04..D-10, D-13), every quick-task-261009-d02 correction note removed"
  - "ARIMA's required arima_order/optional seasonal_order parameters, bounds, no-order error, and model_name drift/non-zero-mean suffixes documented with R Arima() parity statement"
  - "ETS-without-spec = crate AutoETS selection reported under the ETS name; AutoETS crate failure now a hard error (never a silent HoltWinters/Holt/SES substitution)"
  - "Kalman(local_level)/Kalman(local_linear_trend) naming, strict kalman_model validation, and the now-returned (no longer deferred) prediction intervals"
  - "Theta family (Theta/OptimizedTheta/DynamicTheta/DynamicOptimizedTheta/AutoTheta) crate-interval documentation, the shared ~2x-too-wide-at-h>=6 known issue, and exog-identity documentation (model_name '<name> with regressors')"
  - "AutoMFLES/AutoMSTL documented as deprecated aliases reporting model_name 'MFLES'/'MSTL'; MSTL/TBATS no-default-[12] seasonal-period rule (errors naming the model when nothing is detected); MSTL exog NULL interval bounds; TBATS too-narrow-interval known issue"
  - "pass-list-10-08{,b,c}.tsv — per-block execution records for all 15 pages, 0 FAIL each"
affects: ["10-09"]

actuals:
  tokens: 17080
  tasks: 3
  commits: 3
  plan_head_before: 6a2dc8e1442d525f3053dd90c2262fffb87ba917

tech-stack:
  added: []
  patterns:
    - "Reused the 10-06 phase-local snippet harness unmodified — no changes to run_snippets.py/fixtures.sql, only additive synopsis.txt entries and new pass-list-10-08*.tsv outputs"
    - "Split a page's SQL Example into multiple independent ```sql fences whenever one statement is a deliberate error case: a single multi-statement fence run with duckdb -bail aborts at the first failing statement, silently skipping every later statement in that same fence (would have left the MSTL/TBATS exog and regressor examples unproven) — one fence per logical example keeps each one independently verified"
    - "Signature blocks (narrative placeholder identifiers like 'table'/date_col/value_col) and the two ARIMA/MSTL/TBATS deliberate-error examples registered as SYNOPSIS entries in synopsis.txt rather than touching the out-of-scope expected_errors.txt (not in this plan's files_modified) — SYNOPSIS classification only requires file-level membership, independent of the ERROR_WORDS_RE context-sentence check EXPECTED-ERROR needs"

key-files:
  modified:
    - docs/reference/models/state-space/arima.md
    - docs/reference/models/state-space/auto_arima.md
    - docs/reference/models/state-space/ets.md
    - docs/reference/models/state-space/auto_ets.md
    - docs/reference/models/state-space/kalman.md
    - docs/reference/models/theta/theta.md
    - docs/reference/models/theta/optimized_theta.md
    - docs/reference/models/theta/dynamic_theta.md
    - docs/reference/models/theta/dynamic_optimized_theta.md
    - docs/reference/models/theta/auto_theta.md
    - docs/reference/models/multi-seasonal/mfles.md
    - docs/reference/models/multi-seasonal/auto_mfles.md
    - docs/reference/models/multi-seasonal/mstl.md
    - docs/reference/models/multi-seasonal/auto_mstl.md
    - docs/reference/models/multi-seasonal/tbats.md
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt
  created:
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-08.tsv
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-08b.tsv
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-08c.tsv

key-decisions:
  - "Each page's Signature block kept as a SYNOPSIS entry (narrative placeholder identifiers, matching the 10-07 precedent) rather than rewritten against a real fixture — the sibling SQL Example block in the same file is what actually proves the model against the built extension."
  - "Fixed every pre-existing ts_detect_periods_by workaround reference (auto_ets.md, auto_arima.md's Note, auto_theta.md, auto_mstl.md's example) to the working ts_detect_periods_agg aggregate form — the _by table-macro form has a documented known bug (returns an all-NULL row), confirmed by direct execution during this plan's harness runs, and 07-forecasting.md's own Handling Seasonality note already recommends the aggregate form as the workaround."
  - "mfles.md's and mstl.md's pre-existing exogenous-variable examples referenced a never-created 'future_exog' table and 'temperature'/'holiday' columns that do not exist on the shared 'sales' fixture — rewritten with the same self-contained sales_history/future_weather inline fixture pattern already proven in 10-04's SUMMARY-documented exog examples, rather than growing the shared fixtures.sql."
  - "ARIMA's output date column is always named 'ds' regardless of the identifier passed as date_col to ts_forecast_by — discovered when a first draft example referenced the input column name 'date' in the output SELECT list and got a Binder Error; fixed to select 'ds' everywhere a forecast's output date column is referenced."

patterns-established:
  - "A page documenting a deliberate SQL-error example alongside passing examples must put the error case in its OWN ```sql fence, never appended after passing statements in the same fence — duckdb -bail silently drops every later statement in a failing multi-statement script, which would otherwise hide an unproven example behind a deliberate error two lines earlier."

requirements-completed: [FCST-04, FCST-05, FCST-06, FCST-07, FCST-01]

coverage:
  - id: D1
    description: "arima.md documents the required arima_order/optional seasonal_order parameters, bounds, the no-arima_order error, the model_name drift/non-zero-mean suffix rules, and the R Arima() parity statement, each proven against the built extension"
    requirement: FCST-04
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-08.tsv on the 5 state-space pages: 6 PASS, 8 SYNOPSIS, 0 FAIL, exit 0"
        status: pass
      - kind: unit
        ref: "grep -c arima_order arima.md >= 3; grep -c seasonal_order arima.md >= 2; grep -ciE 'fixed heuristic|coefficient of 0.5' arima.md == 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "ets.md/auto_ets.md document that 'ETS' without a spec runs AutoETS and reports ETS(E,T,S), the AutoETS(E,T,S) name format, AutoETS crate failure as a hard error (no fallback), and the ETS interval known issue; kalman.md documents Kalman(spec) naming, strict kalman_model values, and that intervals are now returned"
    requirement: FCST-06
    verification:
      - kind: e2e
        ref: "pass-list-10-08.tsv (same run as D1): ets.md/auto_ets.md/kalman.md all PASS/SYNOPSIS, 0 FAIL"
        status: pass
      - kind: unit
        ref: "grep -c 'Kalman(local_level)' kalman.md >= 1"
        status: pass
    human_judgment: false
  - id: D3
    description: "Theta family pages document crate intervals with the ~2x-too-wide-at-h>=6 known issue, and that Optimized/Dynamic/DynamicOptimized/Auto Theta (plus plain Theta) stay themselves with regressors; auto_arima.md documents the exog order search and ' with regressors' naming"
    requirement: FCST-05
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-08b.tsv on the 5 theta pages: 5 PASS, 5 SYNOPSIS, 0 FAIL, exit 0"
        status: pass
      - kind: unit
        ref: "grep -c 'with regressors' optimized_theta.md >= 1"
        status: pass
    human_judgment: false
  - id: D4
    description: "auto_mfles.md/auto_mstl.md document the deprecated aliases reporting MFLES/MSTL; mstl.md/tbats.md document period auto-detection with an error when none is found (no default 12), TBATS too-narrow bands, MSTL regressor support with NULL bounds on that path"
    requirement: FCST-07
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-08c.tsv on the 5 multi-seasonal pages: 6 PASS, 7 SYNOPSIS, 0 FAIL, exit 0"
        status: pass
      - kind: unit
        ref: "grep -ci deprecated auto_mstl.md auto_mfles.md: >= 1 per file"
        status: pass
    human_judgment: false
  - id: D5
    description: "No page in this plan carries the Phase-10 correction note and every SQL block runs against the built extension"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "grep -rl 'being corrected in v0.10.0' across all 15 pages: empty; three run_snippets.py --all invocations covering all 15 pages: 0 FAIL, exit 0 each"
        status: pass
    human_judgment: false

duration: ~1h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 08: State-Space, Theta and Multi-Seasonal Reference Pages Summary

**15 per-model reference pages (5 state-space, 5 Theta, 5 multi-seasonal) rewritten to document Phase 10's real crate ARIMA/SARIMA fit, AutoETS-selection-under-the-ETS-name, Kalman's returned intervals, the Theta family's shared interval-width known issue and exog identity, and the MFLES/MSTL/TBATS deprecated-alias and no-default-period rules — with every SQL block executed against the built extension and every quick-task-261009-d02 correction note removed.**

## Performance

- **Duration:** ~1h (single session; 3 task commits)
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 16 (15 reference pages, 1 synopsis.txt, 3 new pass-list files)

## Accomplishments

- **Task 1 (tracer) — state-space pages:** `arima.md` rewritten around the real, required
  `arima_order`/`seasonal_order` parameters (bounds, the no-order error, model_name's
  `" with drift"`/`" with non-zero mean"` suffix rules, R `Arima()` parity on the airline
  model and `d=0` orders) — the old "fixed heuristic, coefficient 0.5" prose is gone.
  `auto_arima.md` documents the order-search naming and exog `" with regressors"` naming.
  `ets.md` documents that `'ETS'` without a `model` spec runs the crate's own `AutoETS`
  selection under the `ETS` name (not a hand-picked HoltWinters/Holt/SES default).
  `auto_ets.md` documents that an `AutoETS` crate failure is now a hard error naming
  `AutoETS`, never a silent substitution. `kalman.md` documents `Kalman(local_level)` /
  `Kalman(local_linear_trend)` naming, strict `kalman_model` validation, and that intervals
  are now returned (previously deferred/NULL). Also fixed four pre-existing stale
  `ts_detect_periods_by` workaround references (a documented known-NULL-bug macro) to the
  working `ts_detect_periods_agg` form, surfaced while proving these pages end to end.
- **Task 2 — Theta family pages:** `theta.md`, `optimized_theta.md`, `dynamic_theta.md`,
  `dynamic_optimized_theta.md`, `auto_theta.md`. Replaced the heuristic-interval Returns
  paragraph with the crate-interval text plus the shared known issue (bands ~2x too wide at
  `h >= 6` vs. the reference R implementation, tracked for Phase 17). Documented that all
  five variants (including plain `Theta`, corrected from an initial draft that assumed it
  did not support exog) accept regressors via `ts_forecast_exog_by` and stay themselves —
  `model_name` reports `"<name> with regressors"`.
- **Task 3 — multi-seasonal pages:** `mfles.md`, `auto_mfles.md`, `mstl.md`, `auto_mstl.md`,
  `tbats.md`. `AutoMFLES`/`AutoMSTL` documented as deprecated, accepted aliases reporting
  model_name `"MFLES"`/`"MSTL"` exactly. `MSTL`/`TBATS` documented with the no-default-`[12]`
  seasonal-period rule (a hard error naming the model when nothing is auto-detected and
  nothing is given explicitly). `MSTL` documented with its exog support and the `NULL`
  interval bounds that come with it. `TBATS` documented with its too-narrow-interval known
  issue (3-8% of the equivalent `statsforecast` width, tracked for Phase 11).
- Fixed two pre-existing undefined-table/column doc bugs surfaced by `--all` (neither
  Phase-10-authored): `mfles.md`'s and `mstl.md`'s exogenous-variable examples referenced a
  never-created `future_exog` table and `temperature`/`holiday` columns absent from the
  shared `sales` fixture — rewritten with the same self-contained `sales_history`/
  `future_weather` inline-fixture pattern already established for exog examples elsewhere in
  this phase's docs work.
- Discovered and documented that `ts_forecast_by`'s output date column is always named `ds`
  regardless of the identifier passed as `date_col` — an `arima.md` draft example that
  selected the literal input column name `date` in the output SELECT list hit a Binder
  Error; fixed to select `ds` everywhere a forecast's output date column appears.
- Each page's Signature block (narrative placeholder identifiers, e.g.
  `ts_forecast('table', date_col, value_col, '<Model>', horizon, params)`) registered as a
  SYNOPSIS entry in `synopsis.txt`, matching the 10-07 precedent — the sibling SQL Example
  block in the same file is what actually proves the model against the built extension. Two
  deliberate-error examples (ARIMA's no-`arima_order` error, MSTL's/TBATS' no-detected-period
  error) were also registered as SYNOPSIS rather than modifying the out-of-scope
  `expected_errors.txt` (not in this plan's `files_modified`).

## Task Commits

1. **Task 1: Tracer — state-space pages (ARIMA, AutoARIMA, ETS, AutoETS, Kalman) updated and executed** — `740e1f0` (docs)
2. **Task 2: Theta family pages** — `f327805` (docs)
3. **Task 3: Multi-seasonal pages (MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS)** — `cab3f2a` (docs)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `docs/reference/models/state-space/{arima,auto_arima,ets,auto_ets,kalman}.md` —
  Returns-section rewrite, arima_order/seasonal_order parameters, Kalman naming/intervals,
  AutoETS error behavior, example fixes.
- `docs/reference/models/theta/{theta,optimized_theta,dynamic_theta,dynamic_optimized_theta,auto_theta}.md`
  — Returns-section rewrite, shared known-issue callout, exog-identity documentation.
- `docs/reference/models/multi-seasonal/{mfles,auto_mfles,mstl,auto_mstl,tbats}.md` —
  Returns-section rewrite, deprecated-alias documentation, no-default-period rule, TBATS
  known issue, example fixes.
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt` —
  17 new SYNOPSIS entries (Signature blocks plus the two deliberate-error examples).
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-08{,b,c}.tsv`
  — per-block execution records for this plan's three task groups.

## Decisions Made

See frontmatter `key-decisions`. Most significant: fixing every stale `ts_detect_periods_by`
workaround reference across the state-space and Theta pages to the working
`ts_detect_periods_agg` form — confirmed by direct execution that the `_by` table-macro form
returns an all-NULL row on clean, multi-cycle data, matching the known bug 07-forecasting.md
already documents (this plan did not introduce the bug, but every page it touched that
referenced the broken workaround got the fix, since leaving a misleading "Returns:
primary_period = 7" comment behind a known-broken call would violate the plan's own
"describe only behaviour the built extension actually shows" constraint).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Two pre-existing undefined-table/column references in mfles.md/mstl.md's exogenous examples**
- **Found during:** Task 3, running `run_snippets.py --all` on the multi-seasonal pages
- **Issue:** `mfles.md`'s and `mstl.md`'s exogenous-variable examples referenced a
  `future_exog` table (never created anywhere) and `temperature`/`holiday` columns absent
  from the shared `sales` fixture (which has no such columns).
- **Fix:** Rewrote both examples with self-contained inline `sales_history`/`future_weather`
  fixtures (matching the pattern already used for exog examples elsewhere in this phase).
- **Files modified:** `docs/reference/models/multi-seasonal/mfles.md`, `mstl.md`
- **Verification:** `run_snippets.py --all` on all 5 multi-seasonal pages: 0 FAIL.
- **Committed in:** `cab3f2a`

**2. [Rule 1 - Bug] Four stale ts_detect_periods_by workaround references**
- **Found during:** Tasks 1-3, direct execution while drafting examples
- **Issue:** `auto_ets.md`, `auto_arima.md`'s Note, `auto_theta.md`, and `auto_mstl.md`'s
  example all referenced the known-buggy `ts_detect_periods_by` table-macro form (returns an
  all-NULL row), with one page's comment claiming a specific non-NULL return value that the
  call does not actually produce.
- **Fix:** Switched each reference to the working `ts_detect_periods_agg` aggregate form,
  matching 07-forecasting.md's own documented workaround.
- **Files modified:** `docs/reference/models/state-space/auto_ets.md`,
  `docs/reference/models/state-space/auto_arima.md`,
  `docs/reference/models/theta/auto_theta.md`,
  `docs/reference/models/multi-seasonal/auto_mstl.md`
- **Verification:** All affected examples execute and return the stated values.
- **Committed in:** `740e1f0`, `f327805`, `cab3f2a` (split across the task each page belongs to)

**3. [Rule 1 - Bug] arima.md's own draft example selected the wrong output date-column name**
- **Found during:** Task 1, first harness run on the state-space pages
- **Issue:** `SELECT forecast_step, date, ...` against a `ts_forecast_by` result failed with
  a Binder Error — the macro's output date column is always named `ds`, regardless of the
  identifier passed as `date_col`, even when that identifier happens to be literally `date`.
- **Fix:** Changed the output SELECT to reference `ds` and added an inline comment
  documenting the rule.
- **Files modified:** `docs/reference/models/state-space/arima.md`
- **Verification:** `run_snippets.py --all` on the state-space pages: 0 FAIL.
- **Committed in:** `740e1f0`

### Deferred

None — every discovered bug in this plan's 15 pages was in scope to fix (the pages this plan
owns) and was fixed and verified before commit.

---

**Total deviations:** 3 auto-fixed (all Rule 1 — pre-existing doc-rot or a drafting mistake
surfaced by `--all`), 0 deferred.
**Impact on plan:** No scope creep — every fix was inside a file this plan's own
`files_modified` list already named, matching `--all`'s stated purpose of catching exactly
this class of doc rot.

## Issues Encountered

- A first draft combined MSTL's passing examples, its deliberate no-period error case, and
  its exog example into one ````sql` fence; `duckdb -bail` aborts at the first failing
  statement, which would have silently left the exog example (appearing AFTER the error case
  in the same script) completely unproven. Fixed by splitting into three independent fences
  — documented as a new pattern in `patterns-established` for future pages mixing passing and
  deliberately-erroring examples.
- `expected_errors.txt` is not in this plan's `files_modified` list, so two new
  deliberate-error examples (ARIMA's no-`arima_order` error, MSTL's/TBATS' no-period error)
  were registered as SYNOPSIS entries in `synopsis.txt` instead — SYNOPSIS classification
  only requires file-level key membership (no `ERROR_WORDS_RE` context-sentence check),
  giving the same "proven, not a FAIL" outcome without touching an out-of-scope artifact.

## Known Stubs

None — all 15 pages describe behaviour verified against the built extension; no placeholder
text, hardcoded empty values, or unwired examples.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-21) — every SQL block
is proven against the built extension with committed pass-lists.

## User Setup Required

None.

## Next Phase Readiness

- Phase 10's per-model reference-page work continues in plan 10-09, which owns the remaining
  model families (classical/ensemble/intermittent/distributional/panel, per 10-06/10-07's
  "Next Phase Readiness" notes).
- The phase-local snippet harness (`.../10-forecast-wrapper-delegates-to-crate-models/snippets/`)
  remains unmodified and reusable for 10-09.
- `docs/reference/models/multi-seasonal/auto_tbats.md` still references the known-buggy
  `ts_detect_periods_by` workaround (same class of bug fixed in this plan's other pages) —
  out of this plan's scope (not in `files_modified`); flagged for whichever plan owns that
  page next.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All 15 claimed reference pages, all 3 pass-list files, and this SUMMARY found on disk; all
three claimed commits (`740e1f0`, `f327805`, `cab3f2a`) found in `git log --oneline --all`.
