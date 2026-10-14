---
phase: 16-conformal-bootstrap-correctness
plan: 01
subsystem: forecasting
tags: [conformal-prediction, duckdb, rust-ffi, sql-macros, validation-fixtures]

# Dependency graph
requires:
  - phase: 15-detection-decomposition-on-fdars-0-47
    provides: clean serial execution baseline (no uncommitted shared-file edits); fdars 0.47 bump
provides:
  - "ts_conformal_by: required date_col, one row per (group, date), residuals/empirical coverage computed in Rust"
  - "ts_conformal_calibrate: calibrates per group by default, pooled opt-out via params := {'pooled': true}"
  - "ts_conformal_apply_by: takes a calibration TABLE (ts_conformal_calibrate output), matched per group or pooled"
  - "ts_interval_width_by: lower/upper paired by date_col via one date-ordered LIST, pairwise-complete in Rust"
  - "Rust conformal_from_pairs / conformal_calibrate / ConformalResult.{nominal_level,n_calibration}"
  - "FFI anofox_ts_conformal_from_pairs / anofox_ts_conformal_calibrate; regenerated src/include/anofox_fcst_ffi.h"
  - "numpy split-conformal fixture (validation/reference/fixtures/conformal/split_cases.json) + Rust/SQL parity tests"
  - "migrated examples: synthetic_conformal_examples.sql, ensemble_intervals.sql, synthetic_metrics_examples.sql"
affects: [16-02, 16-03, 18-test-coverage-sweep]

# Actuals (#2632)
actuals:
  tokens: 29358
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Per-group, date-ordered LIST of per-row STRUCTs (not a value-sorted LIST) as the pairing primitive for every conformal/interval-width macro"
    - "SQL params read json-free via params::MAP(VARCHAR, VARCHAR) with CAST (not TRY_CAST) so malformed values error instead of silently defaulting"
    - "A calibration TABLE (not a bare scalar) is the join key between calibrate and apply macros; a literal score is wrapped in a one-row group_col=NULL table"

key-files:
  created:
    - crates/anofox-fcst-core/tests/conformal_reference.rs
    - test/sql/ts_conformal_reference.test
    - validation/reference/python/gen_conformal_reference.py
    - validation/reference/fixtures/conformal/split_cases.json
  modified:
    - crates/anofox-fcst-core/src/conformal.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/src/periods.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - src/include/anofox_fcst_ffi.h
    - src/scalar_functions/conformal.cpp
    - src/include/anofox_forecast_extension.hpp
    - src/anofox_forecast_extension.cpp
    - src/macros/ts_macros.cpp
    - validation/reference/README.md
    - examples/conformal_prediction/synthetic_conformal_examples.sql
    - examples/forecasting/ensemble_intervals.sql
    - examples/metrics/synthetic_metrics_examples.sql

key-decisions:
  - "ConformalMethod::from_str now accepts 'split' as an alias of Symmetric (matching pre-existing docs usage); error message lists all three canonical names with aliases"
  - "conformal_from_pairs rejects Adaptive outright ('requires per-row difficulty values') -- 16-02 wires adaptive through this entry point"
  - "conformal_calibrate rejects Asymmetric explicitly (two margins can't collapse into one conformity_score) but delegates Adaptive to conformal_from_pairs, which currently also rejects it -- both error paths are acceptable for this plan's scope"
  - "ts_conformal_apply_by resolves calibration by JOIN+COALESCE (direct group match, then the group_col IS NULL pooled row) rather than correlated subqueries, keeping query_table(calibration) to exactly one reference"
  - "ts_interval_width_by keeps n_intervals as a SQL COUNT FILTER (not a Rust-returned count) since ts_mean_interval_width's FFI signature only returns the scalar width"

requirements-completed: [RUST-04]

coverage:
  - id: D1
    description: "ts_conformal_by returns one row per (group, date), residuals/empirical coverage computed in Rust, never value-sorted"
    requirement: "RUST-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-01 ts_conformal_by"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#conformal_from_pairs_matches_numpy_per_group"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_conformal_calibrate calibrates per group by default; pooled opt-out via params := {'pooled': true}"
    requirement: "RUST-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-01 ts_conformal_calibrate"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#conformal_calibrate_pooled_matches_numpy"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_conformal_apply_by takes a calibration table, matches per group or falls back to a pooled row, keeps NULL-bound rows for unmatched groups"
    requirement: "RUST-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-01 ts_conformal_apply_by"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_interval_width_by pairs lower/upper by date_col via one date-ordered LIST, Rust drops incomplete pairs"
    requirement: "RUST-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-01 ts_interval_width_by"
        status: pass
    human_judgment: false
  - id: D5
    description: "All four macros read params json-free; malformed alpha/method/length-mismatch raise SQL errors instead of silently defaulting"
    requirement: "RUST-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test (statement error blocks)"
        status: pass
    human_judgment: false
  - id: D6
    description: "Migrated examples run end-to-end against the built extension on the new signatures"
    verification:
      - kind: e2e
        ref: "examples/conformal_prediction/synthetic_conformal_examples.sql (duckdb -bail, exit 0)"
        status: pass
      - kind: e2e
        ref: "examples/forecasting/ensemble_intervals.sql (duckdb -bail, exit 0, bad_rows=0 both sections)"
        status: pass
      - kind: e2e
        ref: "examples/metrics/synthetic_metrics_examples.sql (duckdb -bail, exit 0)"
        status: pass
    human_judgment: false
  - id: D7
    description: "Full DoD: cargo fmt / clippy / test --workspace clean; make test_release assertion count rises"
    verification:
      - kind: unit
        ref: "cargo test --workspace (all green)"
        status: pass
      - kind: integration
        ref: "make test_release (2118 assertions vs 2082 baseline)"
        status: pass
    human_judgment: false

duration: 33min
completed: 2026-10-14
status: complete
---

# Phase 16 Plan 01: ts_conformal_by/calibrate/apply_by/interval_width_by SQL contract tracer Summary

**Rewrote all four conformal SQL macros to pair by a required date column and compute residuals/empirical coverage in Rust, backed by a new numpy split-conformal fixture and non-json-gated SQL/Rust parity tests.**

## Performance

- **Duration:** 33 min
- **Started:** 2026-10-14T09:47:00Z
- **Completed:** 2026-10-14T10:20:00Z
- **Tasks:** 3 completed
- **Files modified:** 18 (4 created, 14 modified)

## Accomplishments

- `ts_conformal_by(backtest_results, group_col, date_col, actual_col, forecast_col, point_forecast_col, params := MAP{})` now returns one row per (group, date), ordered and paired by `date_col` — proven on a non-monotonic point list `[30, 10, 20]` that previously got silently value-sorted to `[10, 20, 30]`.
- Residuals (`actual - forecast`) and empirical coverage are computed entirely in Rust (`conformal_from_pairs`), via the new internal scalars `_ts_conformal_from_pairs` / `_ts_conformal_calibrate` — the macro bodies contain zero `actual_col - forecast_col` arithmetic.
- `ts_conformal_calibrate` calibrates per group by default (groups with different residual scales get different quantiles: 1.905 vs 19.05 on the fixture) and pools only on explicit request (`params := {'pooled': true}`), giving 17.075.
- `ts_conformal_apply_by` now takes a calibration **table** (the shape `ts_conformal_calibrate` returns) instead of a bare score, matching each forecast group to its own calibration row or falling back to a pooled row (`group_col IS NULL`), with NULL bounds for an unmatched, non-pooled group.
- `ts_interval_width_by` pairs `lower`/`upper` from one date-ordered `LIST` of per-row structs instead of two independently-sorted lists — the single most severe of the four original bugs (silent mispairing, not just misordering).
- A new `nominal_level` column reports the requested coverage level separately from the (now genuinely empirical) `coverage` column on every affected macro and scalar function.
- Independent numpy split-conformal fixture (`gen_conformal_reference.py` / `split_cases.json`) backs a Rust parity test (`conformal_reference.rs`, 3 tests) and a non-json-gated SQL test (`ts_conformal_reference.test`, 36 assertions) — `make test_release` rose from 2082 to 2118 assertions.
- Migrated all three shipped examples (`synthetic_conformal_examples.sql`, `ensemble_intervals.sql`, `synthetic_metrics_examples.sql`) to the new signatures; `ensemble_intervals.sql` now demonstrates genuine per-series calibration (series_A and series_B get distinct conformity scores) instead of a single pooled quantile.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — ts_conformal_by end-to-end** — `29219db` (feat)
2. **Task 2: ts_conformal_calibrate / ts_conformal_apply_by / ts_interval_width_by** — `59651c5` (feat; includes a `cargo fmt` fix for Task 1's `conformal_reference.rs`, which was never run through rustfmt before that commit)
3. **Task 3: Migrate examples, full DoD** — `9900442` (feat; includes a one-line pre-existing clippy fix in `periods.rs`, unrelated to this plan's scope but blocking the task's own `cargo clippy -D warnings` gate)

**Plan metadata:** this SUMMARY + STATE.md/ROADMAP.md update (orchestrator-owned, not committed by this executor per its instructions).

## Files Created/Modified

- `crates/anofox-fcst-core/src/conformal.rs` — `ConformalResult.{nominal_level,n_calibration}`; empirical coverage in `conformal_predict`/`_asymmetric`/`_adaptive`; new `conformal_from_pairs`, `ConformalCalibration`, `conformal_calibrate`; `'split'` alias
- `crates/anofox-fcst-core/src/lib.rs` — re-exports for the two new public functions/struct
- `crates/anofox-fcst-core/src/periods.rs` — unrelated pre-existing clippy lint fix (deviation, see below)
- `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_conformal_from_pairs`, `anofox_ts_conformal_calibrate`, `fill_conformal_calibration`
- `crates/anofox-fcst-ffi/src/types.rs` — `ConformalResultFFI.{nominal_level,n_calibration}`, new `ConformalCalibrationFFI`
- `src/include/anofox_fcst_ffi.h` — regenerated by cbindgen, committed with the FFI change
- `src/scalar_functions/conformal.cpp` — internal `_ts_conformal_from_pairs`/`_ts_conformal_calibrate`; `nominal_level`/`n_calibration` appended to `ts_conformal_predict`/`_asymmetric` STRUCTs
- `src/include/anofox_forecast_extension.hpp`, `src/anofox_forecast_extension.cpp` — new registration function wired in
- `src/macros/ts_macros.cpp` — all four macro bodies rewritten
- `validation/reference/python/gen_conformal_reference.py`, `validation/reference/fixtures/conformal/split_cases.json`, `validation/reference/README.md` — new fixture family
- `crates/anofox-fcst-core/tests/conformal_reference.rs`, `test/sql/ts_conformal_reference.test` — new parity tests
- `examples/conformal_prediction/synthetic_conformal_examples.sql`, `examples/forecasting/ensemble_intervals.sql`, `examples/metrics/synthetic_metrics_examples.sql` — migrated to new signatures

## Decisions Made

See `key-decisions` in frontmatter. Most notable: `ts_conformal_apply_by`'s calibration lookup uses a single `query_table(calibration::VARCHAR)` CTE joined twice (direct match, then pooled fallback) rather than two separate `query_table()` calls — satisfies the plan's "exactly one `query_table(calibration` reference" acceptance criterion while still implementing match-by-existence semantics correctly (verified: a group present in both a direct-match table and a pooled table gets exactly one output row per forecast row, no fan-out).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `cargo fmt` fix for `conformal_reference.rs`**
- **Found during:** Task 2's `cargo fmt --all -- --check` run
- **Issue:** `conformal_reference.rs` (created in Task 1 via the Write tool) was never run through `cargo fmt` before Task 1's commit — the earlier fmt check that session had only covered `conformal.rs`.
- **Fix:** Ran `cargo fmt --all`, which reformatted `conformal_reference.rs` only (no semantic change — confirmed by re-running `cargo test -p anofox-fcst-core --test conformal_reference`, still 3/3 passing).
- **Files modified:** `crates/anofox-fcst-core/tests/conformal_reference.rs`
- **Verification:** `cargo fmt --all -- --check` exits 0; tests still pass.
- **Committed in:** `59651c5` (Task 2 commit)

**2. [Rule 3 - Blocking] Pre-existing clippy lint in `periods.rs`**
- **Found during:** Task 3's `cargo clippy --all-targets --all-features -- -D warnings` run
- **Issue:** `clippy::manual_range_contains` on `if nf < 2 || nf > 1_000_000` in `estimate_period_fft_ts`'s `n_frequencies` validation — pre-existing since Phase 15 (commit `8393cbb`, confirmed via `git show 793a16d:crates/anofox-fcst-core/src/periods.rs`), unrelated to this plan's conformal work, but blocking this task's own required DoD gate.
- **Fix:** `if !(2..=1_000_000).contains(&nf)` — purely stylistic, identical semantics (confirmed by `cargo test --workspace` staying green: 440/440 in `anofox-fcst-core`'s lib tests).
- **Files modified:** `crates/anofox-fcst-core/src/periods.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` exits clean; `cargo test --workspace` all green.
- **Committed in:** `9900442` (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (1 bug/formatting, 1 blocking/pre-existing lint).
**Impact on plan:** Neither changes conformal/bootstrap behavior. No scope creep beyond what was needed to clear this plan's own verification gates.

## Issues Encountered

None beyond the two deviations above.

## Phase 16 before-values (for the 16-03 CHANGELOG)

Captured in Task 1 step 1, from the EXISTING `build/release` binary (commit `793a16d`) before any code changes, via `LOAD json;` and the old macro signatures:

1. **`ts_conformal_by`** (old signature) on `two_group`: columns `group_col, point, lower, upper, coverage, conformity_score, method`; group A `point` = `[10.0, 20.0, 30.0]` (VALUE-sorted — the input order `[30.0, 10.0, 20.0]` was destroyed); `coverage` = `0.9` (the nominal `1 - alpha` literal, not empirical); `conformity_score` = `1.9050000000000056`; `method` = `'split_conformal'`.
2. **`ts_conformal_calibrate`** (old signature) on `two_group`: 1 row (no `group_col` parameter existed — pooled every group); `conformity_score` = `17.075000000000003`; `coverage` = `0.9` (nominal); `n_residuals` = `40`.
3. **`ts_conformal_apply_by(..., 15.0)`** on the A future rows: `lower` = `[-5.0, 5.0, 15.0]`, `upper` = `[25.0, 35.0, 45.0]` — VALUE-sorted forecast order `[10, 20, 30]`, not the input order `[30, 10, 20]`.
4. **`ts_conformal_quantile([1,2,3,4,5]::DOUBLE[], alpha)`**: `0.1` → `5.0`; `0.0` → `5.0`; `1.5` → `NULL`. (Unchanged by this plan — quantile math is 16-02's scope.)
5. **`ts_conformal_predict_asymmetric`** over the specified skewed residuals/forecast/alpha=0.2: `lower[1]` = `99.0` (margin 1.0), `upper[1]` = `103.75` (margin 3.75). (Unchanged by this plan.)
6. **`ts_conformal_learn([1.0,-1.0,2.0], [0.5], 'adaptive', 'split')`**: `NULL` (adaptive unreachable from SQL today).
7. **`ts_bootstrap_intervals([0,0,0,0,0,0,0,0,0,10]::DOUBLE[], [100.0], 2000, 0.9, 42)`**: `lower[1]` = `90.0`, `upper[1]` = `100.0` (the mirrored tail — 16-03's bootstrap-sign scope).
8. **`ts_conformal_learn(...).strategy`** for `S` in `'jackknife+'`, `'jackknife_plus'`, `'jk+'`, `'typo'`: `'jackknife_plus'`, `'jackknife_plus'`, `'split'`, `'split'` (16-03's jackknife+-removal scope).
9. **`ts_conformal_predict_per_step`** over 2 folds × horizon 3 (`[100,105,110,100,105,110]` vs `[101,108,109,101,108,109]`, point `[100,105,110]`, alpha 0.1): `half_widths` = `[1.0, 3.0, 1.0]`.

Baseline `make test_release`: **2082 assertions** (50 skipped, `require json`), 32 test cases — rose to **2118** after this plan (36 new, all from `ts_conformal_reference.test`).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 16-02 (split/asymmetric/adaptive math fixes: CONF-02/03/04) can build directly on `conformal_from_pairs`/`conformal_calibrate`, the `conformal/split_cases.json` fixture family, `conformal_reference.rs`, and `test/sql/ts_conformal_reference.test` — all four are structured to be extended, not replaced.
- Plan 16-03 (bootstrap sign, jackknife+ removal, docs/CHANGELOG) has this SUMMARY's "Phase 16 before-values" table ready to cite as the pre-phase baseline for breaking-change documentation.
- `test/sql/ts_conformal.test` / `ts_conformal_coverage.test` (the old, json-gated tests) still call the pre-16-01 signatures and were NOT touched by this plan — per D-11 their migration is Phase 18 (TEST-03) scope, not this phase's.
- No blockers.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/conformal_reference.rs` exists: FOUND
- `test/sql/ts_conformal_reference.test` exists: FOUND
- `validation/reference/fixtures/conformal/split_cases.json` exists: FOUND
- `validation/reference/python/gen_conformal_reference.py` exists: FOUND
- Commit `29219db` exists in git log: FOUND
- Commit `59651c5` exists in git log: FOUND
- Commit `9900442` exists in git log: FOUND
- `make test_release` reports 2118 assertions (> 2082 baseline): CONFIRMED
- All three migrated examples exit 0 with `-bail`: CONFIRMED
- `cargo fmt --all -- --check` / `cargo clippy --all-targets --all-features -- -D warnings` / `cargo test --workspace`: all clean

---
*Phase: 16-conformal-bootstrap-correctness*
*Completed: 2026-10-14*
