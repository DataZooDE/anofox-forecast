---
phase: 13-core-statistics-metrics-data-quality
plan: 04
subsystem: statistics
tags: [rust, ffi, duckdb, metrics, mase, mape, r-forecast, cbindgen]

# Dependency graph
requires:
  - phase: 13-core-statistics-metrics-data-quality
    plan: 01
    provides: Phase 13 snippet harness under .planning/phases/13-core-statistics-metrics-data-quality/snippets/
  - phase: 13-core-statistics-metrics-data-quality
    plan: 02
    provides: nothing consumed directly (STL/seasonal-strength work is independent of metrics)
  - phase: 13-core-statistics-metrics-data-quality
    plan: 03
    provides: nothing consumed directly (data-quality/gap-fill work is independent of metrics)
provides:
  - Hyndman-Koehler mase(actual, forecast, insample, seasonal_period) -> Option<f64> in
    metrics.rs, matching R forecast::accuracy() MASE to 1e-10 on every r_mase fixture case
  - mape(actual, forecast) -> Option<f64> (NULL on any pairwise-complete zero actual, D-07)
  - ts_mase overload set (HK form + removal-error bind on the old 3-list form, naming ts_rmae)
  - ts_mase_by on history-rows-before-first-forecast + seasonal_period (removal-error bind
    on the old baseline-column form, naming ts_rmae_by)
  - r_mase reference-fixture family (8 cases) + gen_mase_reference.R generator
  - Phase 13 closed: no "Phase 13" string remains in docs/plugins/README/CHANGELOG/examples;
    phase-wide snippet harness 0 FAIL; deferred-items.md routes every open out-of-scope item
affects: []

actuals:
  tokens: 34964
  tasks: 3
  commits: 3

plan_head_before: c0c91113849ca638b95364c3d66c35e90ad564fb

tech-stack:
  added: []
  patterns:
    - "C++ ScalarFunctionSet overload-by-arity with a bind-time removal error: the 3-list
      ts_mase(LIST,LIST,LIST) overload's bind callback (not its execute callback) throws
      InvalidInputException unconditionally, so a removed signature fails at plan/bind
      time rather than only when rows actually flow through it."
    - "FFI out_is_null: *mut bool sibling output parameter for a metric whose product-side
      NULL policy differs from its error policy (scale-is-zero / any-actual-is-zero is a
      real success with a NULL value, not a ComputationError) -- first use of this
      convention in this FFI; anofox_ts_mape and anofox_ts_mase both adopt it."
    - "Table-function in-sample vs evaluation split by forecast-NULL boundary: _ts_mase_native
      sorts each group by date, finds t0 (first non-NULL forecast), and routes rows before
      t0 to the in-sample FFI argument (with validity, never complete-cased) and rows at/after
      t0 to the evaluation argument (with validity, complete-cased by Rust) -- two different
      missing-data policies on the same table function, matching the scalar contract exactly."

key-files:
  created:
    - crates/anofox-fcst-core/tests/metrics_reference.rs
    - validation/reference/r/gen_mase_reference.R
    - validation/reference/fixtures/r_mase/accuracy_mase.json
    - test/sql/ts_metrics_reference.test
  modified:
    - crates/anofox-fcst-core/src/metrics.rs
    - crates/anofox-fcst-core/src/stl.rs
    - crates/anofox-fcst-core/src/calendar.rs
    - crates/anofox-fcst-core/tests/reference_fixtures.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
    - src/include/anofox_fcst_ffi.h
    - src/scalar_functions/metrics.cpp
    - src/table_functions/ts_metrics_native.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_metrics.test
    - test/sql/ts_list_null_policy.test
    - docs/api/09-evaluation-metrics.md
    - docs/API_REFERENCE.md
    - docs/guides/03-cross-validation.md
    - examples/metrics/README.md
    - examples/metrics/synthetic_metrics_examples.sql
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - CHANGELOG.md
    - validation/reference/README.md
    - .planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md
    - .planning/phases/13-core-statistics-metrics-data-quality/snippets/expected_errors.txt

key-decisions:
  - "D-08 scalar/table signature conflict resolved per the plan's own objective: the scalar
    ts_mase is a C++ overload set (LIST,LIST,LIST,BIGINT) HK form + (LIST,LIST,LIST)
    bind-time removal error naming ts_rmae -- seasonal_period is a required 4th positional
    argument since DuckDB scalar functions never accept `:=` named arguments at all. The
    ts_mase_by TABLE MACRO keeps the `seasonal_period := 1` default because DuckDB resolves
    an old 5-positional call's baseline_col (a VARCHAR string literal) into that slot by
    TYPE, which _ts_mase_native's bind detects and rejects naming ts_rmae_by."
  - "mase()'s insample argument is Option<f64>, never complete-cased with actual/forecast:
    the lag-seasonal_period scale skips only the specific pairs touching a missing value,
    matching R forecast::accuracy()'s na.rm=TRUE diff() semantics exactly (verified against
    the r_mase fixture's insample_na_m4/test_na_m4 cases) -- never splices missing values out
    and re-pairs the remaining ones."
  - "A zero in-sample scale maps to Ok(None) (SQL NULL), not R's Inf: R's forecast::accuracy()
    literally returns Inf for a constant in-sample series: this product treats MASE as
    undefined there instead of propagating an infinity through DuckDB."
  - "gen_mase_reference.R's manual forecast objects need an explicit NA-filled `fitted` field:
    forecast::accuracy.forecast treats any list object as having a 'training set' too and
    unconditionally calls fitted() on it; with no fitted/residuals field, fitted() returns
    NULL and `dx - NULL` collapses to a length-0 vector, erroring Ops.ts's tsp assignment --
    reproduced and fixed directly, documented in the generator's own comment."
  - "gen_mase_reference.R's seasonal (det_m4/insample_na_m4/test_na_m4) cases pass the TEST
    argument to accuracy() as a frequency-matching ts object, not a plain vector: accuracy.forecast
    picks seasonal (D=1) vs non-seasonal (d=1) differencing from the TEST argument's OWN
    frequency attribute, not the training frequency -- a plain vector silently forces lag-1
    differencing even for a seasonal_period=4 case (confirmed directly: lag-1 gives 0.3292,
    lag-4 gives the correct 0.8156 that both forecast::accuracy() and the manual formula
    converge on once the test vector carries frequency=4)."

requirements-completed: [STAT-05, STAT-06]

coverage:
  - id: D1
    description: "ts_mase(actual, forecast, insample, seasonal_period) is Hyndman-Koehler
      MASE, matching R forecast::accuracy() to 1e-10 on 8 fixture cases (seasonal,
      non-seasonal, NA in-sample, NA test, constant in-sample); the old 3-list
      ts_mase(actual, forecast, baseline) form errors naming ts_rmae"
    requirement: STAT-06
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/metrics_reference.rs#r_mase_fixture_matches_mase, #r_mase_fixture_accuracy_and_manual_agree"
        status: pass
      - kind: integration
        ref: "test/sql/ts_metrics_reference.test (det_m4/det_m1/insample_na_m4/constant-in-sample/removal-error blocks, 25 assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)
      computes the same HK MASE per group from history rows (forecast NULL) + evaluation
      rows; the old 5-argument baseline-column form errors naming ts_rmae_by"
    requirement: STAT-06
    verification:
      - kind: integration
        ref: "test/sql/ts_metrics_reference.test (ts_mase_by det_m4/det_m1/history-only/removal-error blocks); test/sql/ts_metrics.test (migrated MASE sections)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_mape and ts_mape_by return NULL when any pairwise-complete actual is
      exactly 0 (no silent exclusion); docs point to sMAPE/MASE for intermittent data"
    requirement: STAT-05
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/metrics.rs#test_mape_any_zero_actual_is_none"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#mape_any_pairwise_complete_zero_actual_sets_is_null, #mape_zero_actual_paired_with_null_forecast_is_dropped_first; test/sql/ts_metrics_reference.test (MAPE NULL policy blocks)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Phase close: no Phase 13 fix-planned note remains in docs, skills,
      examples, README or CHANGELOG; every Phase-13-changed markdown SQL block runs against
      the built extension; make test_release passes"
    verification:
      - kind: other
        ref: ".planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-04.tsv (12 PASS, 1 EXPECTED-ERROR, 2 SYNOPSIS, 0 FAIL); git grep -e 'Phase 13' over docs/plugins/README.md/CHANGELOG.md/examples returns 0 lines; make test_release: 1786 assertions, 30 test cases, 0 failures"
        status: pass
    human_judgment: false

duration: ~2h
completed: 2026-10-11
status: complete
---

# Phase 13 Plan 04: Metrics Honesty (HK MASE + MAPE NULL Policy) & Phase Close Summary

**`ts_mase` is now Hyndman-Koehler MASE (R-verified to 1e-10, old relative-MAE form removed naming `ts_rmae`), `ts_mape`/`ts_mape_by` return NULL on a zero actual instead of silently excluding it, and Phase 13's docs/skills/CHANGELOG are swept clean of every forward-looking "Phase 13" note.**

## Performance

- **Duration:** ~2h
- **Tasks:** 3 (all type `tracer`/`auto`, autonomous)
- **Files modified:** 27 (6 created, 21 modified)

## Accomplishments

- `anofox_fcst_core::metrics::mase(actual, forecast, insample, seasonal_period) ->
  Result<Option<f64>>` is now the Hyndman & Koehler (2006) MASE: the out-of-sample
  forecast MAE scaled by the in-sample seasonal-naive MAE at lag `seasonal_period`.
  Lag pairs touching a missing in-sample value are skipped, never spliced out and
  re-paired (positions preserved). A numerically-zero in-sample scale maps to
  `Ok(None)` (R's `forecast::accuracy()` returns `Inf`; this product treats MASE as
  undefined there). Verified against a new 8-case `r_mase` reference-fixture family
  (`validation/reference/fixtures/r_mase/accuracy_mase.json`,
  `validation/reference/r/gen_mase_reference.R`) to `1e-10`+ on every case, including
  AirPassengers seasonal-naive (1.570881, matching the plan's planning-time
  verification), an ETS forecast, a random-walk drift forecast, two deterministic
  seasonal/non-seasonal formulas, an NA-in-insample case, an NA-in-test case, and a
  constant-insample (`Inf`-in-R / `NULL`-in-product) case.
- The pre-v0.10.0 3-list `ts_mase(actual, forecast, baseline)` form — which computed
  `MAE(actual, forecast) / MAE(actual, baseline)`, a relative MAE and not MASE at all —
  is REMOVED. `ts_mase` is now a C++ `ScalarFunctionSet` overload: the new
  `(LIST, LIST, LIST, BIGINT)` form computes HK MASE via the new `anofox_ts_mase` FFI
  signature (`insample`/`insample_validity`/`insample_len`/`seasonal_period`/
  `out_is_null` added, `baseline` removed); the old `(LIST, LIST, LIST)` form's BIND
  callback unconditionally throws `InvalidInputException` naming `ts_rmae` — the
  removal fails at plan/bind time, not only when rows flow through it.
- `_ts_mase_native` (the `ts_mase_by` table surface) is rewritten end-to-end:
  signature is `(TABLE, date, actual, forecast, seasonal_period ANY)`. Bind detects
  the old 5th-argument baseline-COLUMN-NAME form by type (a VARCHAR string literal)
  and errors naming `ts_rmae_by`; otherwise `seasonal_period` defaults to 1
  (NULL -> 1, < 1 errors). Per group, rows are sorted by date; the first row with a
  non-NULL forecast is `t0`; rows before `t0` (forecast NULL) are the in-sample
  history, passed to Rust with full validity and NO C++-side NaN filtering; rows at
  `t0` or later are the evaluation window. No forecast row, or no history row,
  returns `NULL` for that group. The `ts_mase_by` macro keeps its
  `seasonal_period := 1` named default (DuckDB resolves the old positional
  baseline-column call by type, not by position, so the default stays safe).
- `anofox_fcst_core::metrics::mape(actual, forecast) -> Result<Option<f64>>` now
  returns `None` if ANY pairwise-complete actual is exactly `0.0` (D-07) instead of
  silently excluding that pair from the sum and divisor count. The scalar
  `anofox_ts_mape` FFI function gets its own body (no longer routed through the
  shared `impl_metric_2arg` helper) with an `out_is_null: *mut bool` parameter; the
  generic `_ts_metrics_native` table function's MAPE branch now writes SQL `NULL`
  via a new `metric_is_null` flag instead of the literal NaN double every other
  metric's failure path still writes (pre-existing behavior, left unchanged for
  MAE/MSE/RMSE/SMAPE/R2/BIAS).
- Migrated every stale 3-list/baseline-column `ts_mase`/`ts_mase_by` usage still in
  the test suite: `test/sql/ts_metrics.test`'s MASE scalar section (now 4-arg HK
  calls) and its `test_extended_metrics`-based `ts_mase_by` sections (now a
  purpose-built `test_mase_history` table with real history rows);
  `test/sql/ts_list_null_policy.test`'s in-sample-NULL case (now asserts the
  non-spliced lag-skip value, `0.25` not a naive-splice `0.333`). A new, non-gated
  `test/sql/ts_metrics_reference.test` (25 assertions) directly pins the scalar HK
  MASE, the MAPE NULL policy, and the `ts_mase_by` history-rows contract against
  the built extension.
- Phase-wide close: `docs/api/09-evaluation-metrics.md` (MASE/MAPE rows and
  examples rewritten, with a self-contained runnable setup table so the new MASE
  examples actually execute, not illustrative pseudocode),
  `docs/API_REFERENCE.md`, `docs/guides/03-cross-validation.md`,
  `examples/metrics/README.md` and `synthetic_metrics_examples.sql` (new runnable
  sections demonstrating `ts_mase` with an in-sample list and the MAPE zero-actual
  NULL, verified end-to-end with `duckdb -unsigned -bail`), and the backtest
  skill all updated to the shipped behavior. `CHANGELOG.md` gets new Breaking
  changes entries for both D-07 and D-08, and three prior 13-01..13-03 bullets
  that said "Phase 13 plan 13-0N" were reworded so no "Phase 13" string remains
  anywhere in docs/plugins/README/CHANGELOG/examples (the phase-wide sweep's own
  acceptance gate). `deferred-items.md` records the D-08 signature-conflict
  resolution, carries forward 13-02's tsfeatures-vs-periodic STL note, and
  confirms the other `_by` metric table functions (MAE/MSE/RMSE/MAPE/SMAPE/R2/
  BIAS, rMAE, coverage, quantile-loss) still filter missing values in C++ rather
  than passing validity to Rust — unchanged by this plan, open RUST-10/Phase 18
  candidate. The phase-wide snippet harness (`pass-list-13-04.tsv`): 12 PASS, 1
  EXPECTED-ERROR, 2 SYNOPSIS, 0 FAIL.

## Task Commits

1. **Task 1 (tracer): Hyndman-Koehler MASE scalar end-to-end, removal error for old 3-list form** — `5802c73` (feat)
2. **Task 2: ts_mase_by on history-rows + seasonal_period, migrate stale 3-list/baseline tests** — `45ea372` (feat)
3. **Task 3: metrics docs/examples/skill/CHANGELOG sweep, deferred items, full DoD** — `87e6f99` (docs)

**Plan metadata:** (this commit, docs-only — see final commit below)

## Files Created/Modified

- `crates/anofox-fcst-core/src/metrics.rs` — HK `mase(actual, forecast, insample, seasonal_period)`; `mape` NULL-on-zero-actual policy; updated unit tests
- `crates/anofox-fcst-core/src/stl.rs` — one-line clippy fix (`v % 2 == 0` -> `v.is_multiple_of(2)`), see Deviations
- `crates/anofox-fcst-core/src/calendar.rs` — reworded a forward-looking "Phase 13's planned" doc comment
- `crates/anofox-fcst-core/tests/metrics_reference.rs` (new) — `r_mase` fixture assertions (2 tests)
- `crates/anofox-fcst-core/tests/reference_fixtures.rs` — reworded two "Phase 13 (STAT-01)" doc comments
- `validation/reference/r/gen_mase_reference.R`, `fixtures/r_mase/accuracy_mase.json` (new) — 8-case R reference
- `validation/reference/README.md` — `r_mase` family row + regeneration command
- `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_mase` new signature (insample/seasonal_period/out_is_null); `anofox_ts_mape` own body + out_is_null
- `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs` — migrated `mase` tests to the new contract; added `mape` zero-actual cases
- `src/include/anofox_fcst_ffi.h` — regenerated (cbindgen), committed
- `src/scalar_functions/metrics.cpp` — `ts_mase` overload set (HK + removal-error bind); `ts_mape` writes NULL on `out_is_null`
- `src/table_functions/ts_metrics_native.cpp` — `_ts_mase_native` rewritten end-to-end; shared MAPE table path carries `metric_is_null`
- `src/macros/ts_macros.cpp` — `ts_mase_by(..., seasonal_period := 1)`; `baseline_col` removed
- `test/sql/ts_metrics_reference.test` (new) — 25 non-gated assertions
- `test/sql/ts_metrics.test`, `test/sql/ts_list_null_policy.test` — migrated stale MASE usages
- `docs/api/09-evaluation-metrics.md`, `docs/API_REFERENCE.md`, `docs/guides/03-cross-validation.md`, `examples/metrics/README.md`, `examples/metrics/synthetic_metrics_examples.sql`, `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md`, `CHANGELOG.md` — shipped semantics documented, phase-wide sweep
- `.planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md`, `snippets/expected_errors.txt`, `snippets/pass-list-13-04.tsv` (new) — deferred items + snippet harness

## Decisions Made

See frontmatter `key-decisions` for the full list. Summary: the scalar/table D-08
signature conflict resolved via a C++ overload set (scalar) + type-detected bind
error (table macro default retained); `insample` is never complete-cased with the
evaluation pair (R-matching na.rm semantics); a zero in-sample scale is `None`, not
`Inf`; the R generator needed an explicit NA-filled `fitted` field and
frequency-tagged test vectors to reproduce `forecast::accuracy()`'s actual
seasonal/non-seasonal dispatch rule.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `crates/anofox-fcst-core/src/stl.rs`'s `odd_at_least3` blocked the DoD's literal `cargo clippy` invocation**
- **Found during:** Task 3, running the plan's exact DoD verify command
  (`cargo clippy --all-targets --all-features -- -D warnings`, no `RUSTUP_TOOLCHAIN`
  pin — unlike the `make release` line, which does pin it)
- **Issue:** `odd_at_least3`'s `v % 2 == 0` is flagged by the DEFAULT toolchain's
  clippy (0.1.99) as `manual_is_multiple_of` under `-D warnings`. This is a
  DIFFERENT symptom from 13-02's previously-logged pre-existing issue
  (`decomposition.rs`/`features.rs` each carry an `#[allow(clippy::manual_is_multiple_of)]`
  that the PINNED 1.89.0 toolchain's clippy (0.1.89) rejects as an *unknown lint
  name* — that issue is unrelated to this file and remains open/unfixed). Here, the
  lint IS recognized by the default toolchain and genuinely fires on `stl.rs`'s raw
  `%` pattern, blocking the whole `anofox-fcst-core` crate from compiling under
  clippy at all — not just warning on an unrelated file, but preventing the Task 3
  DoD gate from running to completion on ANY file.
- **Fix:** `v % 2 == 0` -> `v.is_multiple_of(2)` (stable since Rust 1.87, within the
  project's 1.89 MSRV). Mathematically identical, zero behavior change.
- **Files modified:** `crates/anofox-fcst-core/src/stl.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings`
  (default toolchain) now exits clean; `RUSTUP_TOOLCHAIN=1.89.0 cargo clippy
  --all-targets --all-features -- -D warnings` still shows the same 4
  previously-logged pre-existing findings (`decomposition.rs`/`features.rs` unknown-lint,
  `periods.rs:851`/`874` `needless_range_loop`), confirming this fix did not mask or
  change that unrelated, still-open, still-logged issue.
- **Committed in:** `87e6f99` (Task 3 commit)

---

**Total deviations:** 1 auto-fixed (Rule 3, blocking).
**Impact on plan:** Necessary to complete the plan's own literal DoD verification command; zero behavior change; the pre-existing, unrelated 4-finding clippy issue under the pinned toolchain (logged by 13-02/13-03) is confirmed unchanged and still open.

## Issues Encountered

- **R generator debugging (not a deviation — part of Task 1's own planned work):**
  `gen_mase_reference.R`'s manual forecast objects initially errored
  (`kann 'tsp' nicht einem Vektor der Länge 0 zuordnen`) because
  `forecast::accuracy.forecast` unconditionally calls `fitted()` on any list-typed
  forecast object even when only test-set accuracy is requested; fixed by supplying
  an explicit NA-filled `fitted` field (documented in the generator's own comment).
  A second issue: the seasonal det_m4/insample_na_m4/test_na_m4 cases initially
  computed the WRONG MASE (`0.3292` instead of the correct `0.8156`) because
  `accuracy.forecast` picks seasonal (lag = training frequency) vs non-seasonal
  (lag 1) differencing from the TEST argument's OWN frequency attribute, not the
  training series' frequency — fixed by wrapping the test vector as a frequency-4
  `ts` object before calling `accuracy()`. Both root causes are documented inline
  in `gen_mase_reference.R` for future regeneration.
- No other issues. `test/sql/extension_comparison.test`'s existing `ts_mape` call
  (2-arg, no zero actuals) was checked and confirmed unaffected by the D-07 policy
  change — no edit needed.

## TDD Gate Compliance

Plan frontmatter has no `type: tdd` (this is a `type: execute` plan), and no task
carried `tdd="true"`. Not applicable.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Phase 13 (STAT-01..06, WRAP-02/03) is complete: moments/entropy/gap-counting
  (13-01), STL-based trend/seasonal strength (13-02), data-quality/gap-fill/calendar
  (13-03), and metrics honesty + phase close (13-04, this plan) all shipped,
  R/scipy/pandas-verified, and documented with no forward-looking "fix planned"
  notes remaining.
- Open, explicitly routed deferred items for later phases: the `'3mo'`-style
  calendar-frequency multiplier bug (13-03, unrouted), the pre-existing
  `decomposition.rs`/`features.rs`/`periods.rs` clippy findings under the pinned
  1.89.0 toolchain (13-02/13-03, unrouted), and the other `_by` metric table
  functions' C++-side (not Rust-side) missing-value filtering (this plan, routed to
  Phase 18 RUST-10).
- No blockers for Phase 14 or later milestone work.

---
*Phase: 13-core-statistics-metrics-data-quality*
*Completed: 2026-10-11*

## Self-Check: PASSED

All created files verified present on disk (`crates/anofox-fcst-core/tests/metrics_reference.rs`,
`validation/reference/r/gen_mase_reference.R`, `validation/reference/fixtures/r_mase/accuracy_mase.json`,
`test/sql/ts_metrics_reference.test`, `.planning/phases/13-core-statistics-metrics-data-quality/snippets/pass-list-13-04.tsv`);
all three task commit hashes (`5802c73`, `45ea372`, `87e6f99`) verified present in
`git log --oneline --all`.
