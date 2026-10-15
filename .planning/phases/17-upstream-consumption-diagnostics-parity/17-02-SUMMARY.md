---
phase: 17-upstream-consumption-diagnostics-parity
plan: 02
subsystem: diagnostics
tags: [anofox-forecast, rust-ffi, ljung-box, portmanteau, duckdb-extension, docs]

requires:
  - phase: 17-upstream-consumption-diagnostics-parity
    plan: 17-01
    provides: anofox-forecast 0.16.1, regression := 'c'|'ct' on ts_adf/ts_kpss/ts_stationarity, D-04 four-way verdict, the Phase 17 R diagnostics fixture (gen_diagnostics_reference.R) and test/sql/ts_diagnostics_reference.test as a foundation to append to
provides:
  - ts_ljung_box/ts_ljung_box_by fitted_params parameter (D-06, DIAG-04): df = lags - fitted_params, R Box.test(fitdf)-exact p-values via exact chi-square survival function
  - df <= 0 at the effective lag count is a SQL error (fitted_params at/above lags, explicit lags = 0); negative fitted_params is a SQL error
  - docs/api/10-diagnostics.md fully rewritten to fixture-verified claims only (D-07, DIAG-05)
  - Phase 17 snippet harness (.planning/phases/17-upstream-consumption-diagnostics-parity/snippets/), forked from Phase 16's, with sales_data/resids fixtures
affects: [17-03]

actuals:
  tokens: 33757
  tasks: 3
  commits: 3
plan_head_before: 5b7616f9ccaa78350f3fc517e09327ee7a51b7e8

tech-stack:
  added: []
  patterns:
    - "ljung_box now returns Result<LjungBoxOut, ForecastError>: resolve the crate's own effective-lags formula first, validate fitted_params against it, THEN call the crate -- reusable pattern for any future wrapper around a crate fn that silently floors an invalid parameter"
    - "statrs::distribution::{ChiSquared, ContinuousCDF}.sf(x) recomputes a crate-approximate p-value with the exact chi-square survival function when df > 30 -- same approach applicable to any other approximate upstream p-value"

key-files:
  created:
    - crates/anofox-fcst-core/tests/portmanteau_reference.rs
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/run_snippets.py
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/fixtures.sql
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/BASE
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/expected_errors.txt
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/synopsis.txt
    - .planning/phases/17-upstream-consumption-diagnostics-parity/snippets/pass-list-17-02.tsv
  modified:
    - validation/reference/r/gen_diagnostics_reference.R
    - validation/reference/fixtures/r_diagnostics/stationarity_portmanteau.json
    - crates/anofox-fcst-core/src/validation.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/anofox_fcst_ffi.h
    - src/scalar_functions/diagnostics.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_diagnostics_reference.test
    - CHANGELOG.md
    - validation/reference/README.md
    - docs/api/10-diagnostics.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - examples/diagnostics/stationarity.sql
    - examples/diagnostics/residuals.sql
    - README.md

key-decisions:
  - "ljung_box's df validation resolves the crate's exact effective-lags formula (default min(10, n/5).max(1), then .min(n-1)) before checking fitted_params, so the same df the crate would compute is what gets validated -- avoids a second source of truth drifting from the crate's own lag resolution"
  - "n < 3 is never an error (Ok with the crate's NaN result) since there's no lag count yet to validate fitted_params against; the crate's own early-return path is reused unchanged"
  - "Task 2's entire <behavior> block passed against Task 1's implementation with zero core/FFI/C++ changes needed -- Task 1's tracer implementation was already comprehensive. The only RED->GREEN cycle in Task 2 was in the test code itself (a NaN == NaN assert_eq! bug), documented as a deviation below rather than silently 'fixed in place'"
  - "Jarque-Bera and Durbin-Watson needed zero core changes (as the plan anticipated) -- both already match the R fixture to the required tolerances; portmanteau_reference.rs's fixture-row tests for them are the proof, not a claim"

patterns-established:
  - "ljung_box(residuals, lags, fitted_params) -> Result<LjungBoxOut, ForecastError> -- model-aware degrees-of-freedom validation pattern, reusable for any future portmanteau-style test that takes a fitted-parameter count"

requirements-completed: [DIAG-04, DIAG-05]

coverage:
  - id: D1
    description: "ts_ljung_box(residuals, lags, fitted_params) and ts_ljung_box_by(..., fitted_params := p + q) use df = lags - fitted_params and match R Box.test(fitdf) to 1e-9 (ARMA(1,1) residuals lag 10/20, diff(rw) lag 40/30)"
    requirement: DIAG-04
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/portmanteau_reference.rs#ljung_box_matches_r_box_test_fitdf_for_portmanteau_cases"
        status: pass
      - kind: integration
        ref: "test/sql/ts_diagnostics_reference.test (17-02 Ljung-Box fitted_params section)"
        status: pass
    human_judgment: false
  - id: D2
    description: "fitted_params at or above the effective lags (explicit or default) is a SQL error naming fitted_params/lags/degrees of freedom; negative fitted_params is a SQL error; lags >= n clamps to n-1; n<3 returns NaN without an error; constant residuals return statistic 0/p 1"
    requirement: DIAG-04
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/validation.rs#ljung_box_fitted_params_at_or_above_lags_is_err, #ljung_box_default_lags_validated_against_fitted_params, #ljung_box_huge_lags_clamped_no_panic, #ljung_box_n_below_3_is_nan_not_err, #ljung_box_constant_series_statistic_0_p_1"
        status: pass
      - kind: integration
        ref: "test/sql/ts_diagnostics_reference.test (17-02 Ljung-Box edge cases section)"
        status: pass
    human_judgment: false
  - id: D3
    description: "p-values exact chi-square survival function for df > 30 (matches R pchisq, not the crate's Wilson-Hilferty approximation); ts_residual_diagnostics unchanged (fitted_params 0, never errors); Jarque-Bera/Durbin-Watson parity documented"
    requirement: DIAG-04
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/validation.rs#ljung_box_df_over_30_uses_exact_chi_square_survival_function, #residual_diagnostics_no_panic_and_matches_ljung_box_for_degenerate_inputs; portmanteau_reference.rs#jarque_bera_matches_tseries_for_every_r_diagnostics_series, #durbin_watson_matches_closed_form_for_every_portmanteau_case"
        status: pass
    human_judgment: false
  - id: D4
    description: "docs/api/10-diagnostics.md rewritten to claim only fixture-verified behaviour (every fix-planned note and stale caveat removed); every SQL block in the doc, the EDA skill, and README passes the Phase 17 snippet harness (0 FAIL); both diagnostics examples run"
    requirement: DIAG-05
    verification:
      - kind: other
        ref: ".planning/phases/17-upstream-consumption-diagnostics-parity/snippets/pass-list-17-02.tsv (0 FAIL rows, PASS: 7)"
        status: pass
      - kind: integration
        ref: "examples/diagnostics/stationarity.sql, examples/diagnostics/residuals.sql (both exit 0)"
        status: pass
    human_judgment: false
  - id: D5
    description: "New Ljung-Box SQL assertions in the non-json-gated test/sql/ts_diagnostics_reference.test; make test_release assertion count rises (2260 baseline -> 2279); CHANGELOG has before/after rows"
    verification:
      - kind: integration
        ref: "make test_release (2279 assertions / 34 test cases, Task 1 baseline 2260/34); CHANGELOG.md Phase 17 table (4 new Ljung-Box rows)"
        status: pass
    human_judgment: false

duration: 115min
completed: 2026-10-15
status: complete
---

# Phase 17 Plan 02: Ljung-Box fitted_params + Diagnostics Docs Rewrite Summary

**`ts_ljung_box`/`ts_ljung_box_by` gained a `fitted_params := p + q` parameter making degrees of freedom model-aware (`df = lags - fitted_params`, R `Box.test(fitdf)`-exact p-values via the exact chi-square survival function), with `df <= 0` now a named SQL error instead of the crate's silent floor-to-1, and `docs/api/10-diagnostics.md` was fully rewritten to claim only what fixture-backed tests verify, with a new Phase 17 snippet harness proving every doc/skill/README SQL example runs.**

## Performance

- **Duration:** ~115 min
- **Started:** 2026-10-15T00:10:00Z (approx.)
- **Completed:** 2026-10-15T02:05:00Z (approx.)
- **Tasks:** 3
- **Files modified:** 22 (7 created, 15 modified)

## Accomplishments

- `validation/reference/r/gen_diagnostics_reference.R` appends a `portmanteau_cases` block (append-only — every pre-existing fixture value regenerated byte-identical): `Box.test(type = "Ljung-Box", fitdf = 2)` on `residuals(arima(ar1[1:120], order = c(1, 0, 1)))` at lags 10/20, and on `diff(rw)`/`diff(rw)[1:30]` at lags 40/6, plus the closed-form Durbin-Watson for each case.
- Core `ljung_box(residuals, lags, fitted_params) -> Result<LjungBoxOut, ForecastError>`: resolves the crate's own effective-lags formula, rejects `fitted_params` at or above that count (`InvalidParameter`, naming "degrees of freedom"), and — when the statistic is finite and positive — recomputes the p-value via `statrs::distribution::ChiSquared::sf` (exact, matching R `pchisq`/`Box.test` even for `df > 30`, where the crate's own Wilson-Hilferty approximation deviates by up to ~2e-4). `residual_diagnostics` is unchanged in behaviour (`fitted_params` fixed at 0, maps the now-theoretically-possible-but-practically-unreachable `Err` to `NaN` rather than unwrapping).
- FFI `anofox_ts_ljung_box` gains `fitted_params: c_int` (negative → `InvalidInput`), C++ `ts_ljung_box`/`anofox_fcst_ts_ljung_box` gain a 3-arg `(LIST(DOUBLE), INTEGER, INTEGER)` overload with `INVALID_INPUT → InvalidInputException`, and `ts_ljung_box_by` gains `fitted_params := 0` named default. The FFI header was regenerated and committed alongside the signature change.
- `crates/anofox-fcst-core/tests/portmanteau_reference.rs` (4 tests): Ljung-Box fitdf parity against the 4 portmanteau cases and against every `r_diagnostics` series' pre-existing `ljung_box_lag10_fitdf0`/`_fitdf2` fixture blocks, plus Jarque-Bera and Durbin-Watson fixture-row parity (no core changes needed for either — both already matched).
- Task 2's entire `<behavior>` block (fitted_params at/above lags for explicit and default lag counts, huge-lags clamping, `n < 3`, constant series, `df > 30` exact p-value, `residual_diagnostics` degenerate inputs) passed against Task 1's implementation — no further core/FFI/C++ fix was required. `test/sql/ts_diagnostics_reference.test` gained 19 assertions total across both tasks (50 → 61 within the file; `make test_release` rises 2260 → 2279 workspace-wide), still with no `require json` line. CHANGELOG gained 4 new Ljung-Box before/after rows.
- `docs/api/10-diagnostics.md` fully rewritten: every function section now names its reference implementation and the fixture-backed test proving it (`stationarity_reference.rs`, `portmanteau_reference.rs`, `ts_diagnostics_reference.test`); every "fix planned for v0.10.0" note and stale caveat (non-augmented ADF, minimum-RSS lag choice, 9-point p-value lookup, unbounded KPSS p, inverted verdict labels, `df == lags` always) is gone, replaced with the Phase 17 `regression`/`fitted_params` behaviour and an Edge Cases section. The EDA skill's stale "Current limitations" paragraph and inverted-label note, both diagnostics examples (new `regression := 'ct'` trend-stationary section, new `fitted_params` section), and the README Stationarity bullet were all updated to match.
- Forked the Phase 16 plan 16-03 snippet harness into `.planning/phases/17-upstream-consumption-diagnostics-parity/snippets/` (reused by plan 17-03), with `BASE` set to the parent of the first `(17-01)` commit and `sales_data`/`resids` fixtures added for the diagnostics doc/examples. `pass-list-17-02.tsv`: 0 FAIL (7 PASS rows — most of the doc's blocks were byte-identical to the pre-17-01 base and correctly skipped by the harness's changed-only default).

## Task Commits

1. **Task 1: Tracer — Ljung-Box fitted_params end-to-end** - `8c85a67` (feat)
2. **Task 2: Ljung-Box edge cases, CHANGELOG rows, README row** - `c4eb7df` (test — no core/FFI/C++ fix needed; see Decisions)
3. **Task 3: Rewrite diagnostics doc, EDA skill, examples, README, snippet harness** - `4d9aa0b` (docs)

**Plan metadata:** commit pending (this SUMMARY, orchestrator-owned STATE/ROADMAP/REQUIREMENTS)

_Note: Task 2 carries `tdd="true"` but produced a single `test(17-02)` commit, not the usual RED→GREEN pair — see Deviations._

## Files Created/Modified

- `crates/anofox-fcst-core/src/validation.rs` - model-aware `ljung_box` returning `Result`, `residual_diagnostics` NaN-safe fallback, 7 new edge-case unit tests
- `crates/anofox-fcst-ffi/src/lib.rs` - `anofox_ts_ljung_box` gains `fitted_params: c_int`, maps core `Err` via `ffi_error_code`
- `src/include/anofox_fcst_ffi.h` - regenerated for the FFI signature change
- `src/scalar_functions/diagnostics.cpp` - 3-arg `ts_ljung_box` overload, `INVALID_INPUT → InvalidInputException`
- `src/macros/ts_macros.cpp` - `ts_ljung_box_by` gains `fitted_params := 0`
- `validation/reference/r/gen_diagnostics_reference.R` + `stationarity_portmanteau.json` - `portmanteau_cases` (4 cases) + 3 new tolerance keys
- `crates/anofox-fcst-core/tests/portmanteau_reference.rs` - new (4 tests: Ljung-Box fitdf parity ×2, Jarque-Bera, Durbin-Watson)
- `test/sql/ts_diagnostics_reference.test` - "17-02 Ljung-Box fitted_params" + "17-02 Ljung-Box edge cases" sections (19 assertions)
- `CHANGELOG.md` - 4 new Ljung-Box before/after rows in the Phase 17 table
- `validation/reference/README.md` - `r_diagnostics` row: new tolerance keys, `portmanteau_cases` rationale, new consumer
- `docs/api/10-diagnostics.md` - fully rewritten (fixture-verified claims only)
- `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md` - stationarity/Ljung-Box sections rewritten
- `examples/diagnostics/stationarity.sql` - new Section 7 (`regression := 'ct'`)
- `examples/diagnostics/residuals.sql` - new `fitted_params` section
- `README.md` - Stationarity bullet updated
- `.planning/phases/17-upstream-consumption-diagnostics-parity/snippets/*` - new Phase 17 snippet harness fork

## Decisions Made

See `key-decisions` in frontmatter. Most consequential: Task 2's test additions all passed immediately against Task 1's implementation, meaning Task 2 produced no core/FFI/C++ fix — the TDD RED→GREEN cycle for Task 2 happened entirely within the test file itself (see Deviations).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Task 2's own test had a `NaN == NaN` `assert_eq!` bug**
- **Found during:** Task 2 (writing `residual_diagnostics_no_panic_and_matches_ljung_box_for_degenerate_inputs`)
- **Issue:** `assert_eq!(d.lb_statistic, lb.statistic)` fails for the `[1.0, 2.0]` case where both sides are legitimately `NaN` — IEEE754 `NaN != NaN`, so the assertion panicked even though the underlying behaviour was correct.
- **Fix:** Added a NaN-aware branch (`if lb.statistic.is_nan() { assert!(d.lb_statistic.is_nan()) } else { assert_eq!(...) }`) for both `lb_statistic` and `lb_p_value`.
- **Files modified:** `crates/anofox-fcst-core/src/validation.rs`
- **Verification:** `cargo test -p anofox-fcst-core --lib validation` — 30/30 pass
- **Committed in:** `c4eb7df` (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (Rule 1, test-code bug — not a production-code issue).
**Impact on plan:** None on behavior. No scope creep.

### TDD Gate Note (Task 2)

Task 2 carries `tdd="true"` and the plan's own action item explicitly anticipated this
outcome for Jarque-Bera/Durbin-Watson ("No change to Jarque-Bera / Durbin-Watson code is
expected... document instead of claiming parity") — this plan extends that same
anticipated-pass outcome to the full Ljung-Box `<behavior>` block as well, since Task 1's
tracer implementation already validated every edge case described. The RED phase
(`gsd_run check tdd-red-evidence` is noted as unable to parse `cargo test` output per this
plan's execution context, so RED was verified manually): writing the new unit tests and
running `cargo test -p anofox-fcst-core --lib validation` surfaced exactly one failure
(the `NaN == NaN` test bug above, in the test code, not production code). After fixing that
test bug, all 7 new Ljung-Box edge-case tests plus the SQL "17-02 Ljung-Box edge cases"
section passed with zero changes to `validation.rs`'s `ljung_box`/`residual_diagnostics`
implementations, zero FFI changes, and zero C++ changes beyond what Task 1 already
committed. Task 2 therefore produced a single `test(17-02)` commit (test additions +
CHANGELOG + README), not the standard `test` → `feat` pair — there was no GREEN-phase
production diff to commit. This is recorded here rather than fabricating an empty/no-op
`feat` commit.

## Issues Encountered

None beyond the documented deviation above.

## Known Stubs

None. No data is stubbed, hardcoded, or mocked in this plan's deliverables.

## Pre-existing Issues (out of scope, not fixed)

Running the Phase 17 snippet harness with `--all` (full re-run, beyond this plan's required
changed-only gate) surfaces 3 pre-existing `FAIL` rows in
`plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md`'s **Feature extraction**
section (`ts_features_by` signature-as-SQL block, a macro-argument mismatch, and a column
reference bug) — all in blocks this plan did not touch (diagnostics only). Per the deviation
rules' scope boundary, these are out of scope for this plan and are not fixed here. The
required gate for this plan (`run_snippets.py` without `--all`, i.e. changed-blocks-only)
returns 0 FAIL and is what `pass-list-17-02.tsv` records.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for 17-03 (R forecast parity + Monte Carlo coverage, FCST-09): the Phase 17 snippet
  harness fork (`.planning/phases/17-upstream-consumption-diagnostics-parity/snippets/`) is
  in place for 17-03 to reuse directly (its own `pass-list-17-03.tsv`).
- No blockers.

---
*Phase: 17-upstream-consumption-diagnostics-parity*
*Completed: 2026-10-15*

## Self-Check: PASSED

- All 5 spot-checked key files found on disk (`portmanteau_reference.rs`, snippet harness `run_snippets.py`/`fixtures.sql`/`pass-list-17-02.tsv`, `docs/api/10-diagnostics.md`).
- All 3 task commit hashes (`8c85a67`, `c4eb7df`, `4d9aa0b`) found in `git log --oneline --all`.
- Re-ran `./build/release/test/unittest test/sql/ts_diagnostics_reference.test`: 61 assertions pass.
- Re-ran `make test_release`: 2279 assertions / 34 test cases (rises from the Task 1 baseline 2260/34).
- Re-ran both diagnostics examples: `examples/diagnostics/stationarity.sql` and `examples/diagnostics/residuals.sql` both exit 0.
- Re-ran the Phase 17 snippet harness (changed-blocks-only, the plan's required gate): 0 FAIL, 7 PASS.
- `cargo fmt --all -- --check` and `cargo clippy --all-targets --all-features -- -D warnings` both clean; `cargo test --workspace` green (484+ tests across all crates, 0 failed).
