---
phase: 20-detection-metric-bug-fixes
plan: 06
subsystem: detection
tags: [bug-fix, changepoints, bocpd, rust-ffi, "#287", BUG-06, BUG-07]

requires:
  - phase: 20-detection-metric-bug-fixes plan 05
    provides: TsCollectFinalizeBarrier, #287 md5 default-output pins (20-03)
provides:
  - "src/include/ts_changepoints_params.hpp: single shared params-MAP parser (hazard_lambda, threshold) for ts_detect_changepoints_by and ts_detect_changepoints_agg"
  - "ChangepointOptions / detect_changepoints_bocpd_with_options (Rust core): D2-ii threshold decision rule, validated"
  - "anofox_ts_detect_changepoints_bocpd_opts (FFI) + ChangepointOptionsFFI, generated header committed"
  - "issue287_A/B/C numpy reference cases pinning the D2-ii threshold rule against an independent reference"
  - "SQL parity, threshold-consistency, threshold-error and BUG-07 period-7 regression pins in detection_decomposition.test"
affects:
  - 20-07 (docs for hazard_lambda/threshold alongside deseasonalize)

actuals:
  tokens: 17372
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "Shared C++ params-MAP header (ts_changepoints_params.hpp) parsed once per consumer (table-function Bind; aggregate Update first row), mirroring the ts_forecast_params.hpp precedent"
    - "A decision rule that depends on more than one field (hazard_lambda + threshold) is decided once in Rust (detect_changepoints_bocpd_with_options) and exposed through one new FFI export both C++ consumers call, instead of each consumer re-deriving it"
    - "FFI options struct (ChangepointOptionsFFI) with NaN-as-unset sentinel for an optional f64 parameter, matching the existing hazard_lambda<=0-as-unset convention"

key-files:
  created:
    - src/include/ts_changepoints_params.hpp
    - .planning/phases/20-detection-metric-bug-fixes/20-06-SUMMARY.md
  modified:
    - crates/anofox-fcst-core/src/changepoint.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/tests/changepoint_reference.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_changepoints.cpp
    - src/aggregate_functions/ts_changepoints_agg.cpp
    - src/macros/ts_macros.cpp
    - test/sql/correctness/detection_decomposition.test
    - validation/reference/python/gen_changepoint_reference.py
    - validation/reference/fixtures/changepoints/bocpd_cases.json

key-decisions:
  - "ts_detect_changepoints_by's native second argument changed from VARCHAR to MAP(VARCHAR, VARCHAR); the old static regex-based ParseHazardLambda string parser was deleted in favour of the shared ParseChangepointParamSet (internal signature only -- public macro signature unchanged)"
  - "threshold is validated in Rust only (finite, [0, 1]); the C++ params header just parses the raw string into a double (quiet-NaN = unset) and only throws for a non-numeric value, per the plan's split of responsibility"
  - "The aggregate parses params once per state on the first row seen per group (documented: first row's params win if a group's rows somehow carry different MAPs) -- matches the existing Combine semantics that already copy whole states"

requirements-completed: [BUG-06, BUG-07]

coverage:
  - id: D1
    description: "ts_detect_changepoints_agg parses its params MAP through the same shared parser as ts_detect_changepoints_by; hazard_lambda=50 now flags A 8/B 6/C 6 on the #287 panel (was 6/2/6, params silently ignored), identical to the table function row for row"
    requirement: BUG-06
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (#287 agg/table parity block)"
        status: pass
      - kind: unit
        ref: "live CLI probe: ts_detect_changepoints_agg(ds, y, MAP{'hazard_lambda': '50'}) -> A,8;B,6;C,6;"
        status: pass
    human_judgment: false
  - id: D2
    description: "threshold param (D2-ii): is_changepoint = (changepoint_probability >= threshold) for every row after the first of each series; the first row is never flagged; decided once in Rust and shared by both SQL consumers via a new FFI options struct"
    requirement: BUG-06
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/changepoint.rs#test_with_options_threshold_flags_every_row_after_first_at_or_above_threshold, test_with_options_threshold_zero_flags_every_row_after_first, test_with_options_threshold_out_of_range_is_invalid_parameter, test_with_options_threshold_none_matches_detect_changepoints_bocpd"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/changepoint_reference.rs#bocpd_with_options_threshold_matches_numpy_expected_threshold_0_5"
        status: pass
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (#287 threshold consistency/parity/error blocks)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_detect_periods_by returns primary_period 7.0 (and periods[1].period 7.0) for A, B, C on the #287 panel (BUG-07 regression pin, already fixed on main)"
    requirement: BUG-07
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (BUG-07 block)"
        status: pass
    human_judgment: false

duration: 36min
completed: 2026-10-23
status: complete
---

# Phase 20 Plan 06: Changepoint params parity + D2-ii threshold rule Summary

`ts_detect_changepoints_agg` previously ignored its entire `params` MAP (hazard_lambda stayed hardcoded at 250 regardless of what was passed), while `ts_detect_changepoints_by` parsed the same key through its own static regex parser -- the two consumers could silently disagree. Both now share one C++ header (`ts_changepoints_params.hpp`) and, for the decision rule itself, one new Rust function (`detect_changepoints_bocpd_with_options`) reached through one new FFI export (`anofox_ts_detect_changepoints_bocpd_opts`). A new opt-in `threshold` param lets callers say `is_changepoint = (changepoint_probability >= threshold)` for every row after the first of each series (D2-ii, user decision) -- the first row is never flagged, since its probability is always the hazard prior alone (`1 - 1/hazard_lambda`), not evidence of a changepoint. Default outputs (no threshold set) are byte-identical to before (20-03's md5 pins still pass). `ts_detect_periods_by`'s period-7 regression (BUG-07, already fixed on main) is now pinned.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | 5b61688 | fix(20-06): shared changepoint params parser; agg honours hazard_lambda (#287 BUG-06) |
| 2 (tdd, RED) | 5b239d1 | test(20-06): add failing tests for changepoint threshold D2-ii rule |
| 2 (tdd, GREEN) | b9de686 | feat(20-06): threshold key with D2-ii rule, decided in Rust through a shared FFI options struct (#287, BUG-06) |
| 3 (tdd, RED) | cec3d3f | test(20-06): add failing numpy-reference test for #287 threshold flags |
| 3 (tdd, GREEN) | 9392ea2 | feat(20-06): numpy reference cases + SQL parity/threshold/period-7 pins for #287 (BUG-06, BUG-07) |

`git rev-list --count 052d95f..9392ea2` = 5. All five carry the `Co-Authored-By: Claude Sonnet 5` trailer.

## What changed

### Task 1 (tracer): shared params parser

- **New:** `src/include/ts_changepoints_params.hpp` -- `ChangepointParamSet { hazard_lambda }` and `ParseChangepointParamSet(const Value&)`, reading a MAP(VARCHAR, VARCHAR) or STRUCT. Unparsable `hazard_lambda` falls back to the default (matching the removed `ParseHazardLambda`'s pre-existing leniency).
- `ts_changepoints.cpp`: the native `_ts_detect_changepoints_by_native`'s second argument type changed from VARCHAR to MAP(VARCHAR, VARCHAR); the static regex-based `ParseHazardLambda` and `<regex>` include were deleted.
- `ts_changepoints_agg.cpp`: the previously-empty "Parse params MAP if valid" block now calls `ParseChangepointParamSet` on the first row seen per group.
- `ts_macros.cpp`: `ts_detect_changepoints_by`'s macro body passes `params::MAP(VARCHAR, VARCHAR)` directly instead of `COALESCE(TRY_CAST(params['hazard_lambda'] AS VARCHAR), '250.0')`.
- `detection_decomposition.test`: a parity block proving table-function/aggregate rows are byte-identical for `MAP{}` and `MAP{'hazard_lambda': '50'}`, the aggregate's flagged count is now 8/6/6 (was 6/2/6), and the aggregate's hazard_lambda=50 output now differs from its MAP{} default.

### Task 2 (TDD): threshold key, D2-ii rule

- RED: `ChangepointOptions`/`detect_changepoints_bocpd_with_options` added as a stub that parses `threshold` but ignores it; `test_with_options_threshold_zero_flags_every_row_after_first` and `test_with_options_threshold_out_of_range_is_invalid_parameter` failed intentionally.
- GREEN: full implementation. `threshold == None` reproduces `detect_changepoints_bocpd` bit for bit. `threshold == Some(t)`: `is_changepoint[i] = i >= 1 && prob[i] >= t`; `t` not finite or outside `[0, 1]` returns `ForecastError::InvalidParameter { param: "threshold", .. }`. Re-exported from `lib.rs`.
- FFI: `ChangepointOptionsFFI { hazard_lambda, threshold }` (types.rs; `hazard_lambda <= 0.0` / `threshold.is_nan()` mean "unset") and `anofox_ts_detect_changepoints_bocpd_opts` (lib.rs), same `catch_unwind`/null-check/alloc pattern as the existing BOCPD export, freed by the existing `anofox_free_bocpd_result`. Header regenerated via `cargo build --release` (`make header`); the new struct and function appear in `src/include/anofox_fcst_ffi.h` without any `cbindgen.toml` change (cbindgen auto-includes types reachable from an exported function's signature).
- `ts_changepoints_params.hpp`: `threshold` field added (quiet-NaN sentinel for "unset"); a present-but-unparsable value throws `InvalidInputException` naming `threshold`; range validation stays in Rust.
- Both consumers now build a `ChangepointOptionsFFI` from the parsed param set and call `anofox_ts_detect_changepoints_bocpd_opts`; an `INVALID_INPUT`-coded error throws `InvalidInputException` with the Rust message, on both surfaces.

### Task 3 (TDD): numpy reference + SQL pins

- RED: `bocpd_with_options_threshold_matches_numpy_expected_threshold_0_5` looked up `issue287_A/B/C` in the fixture by name and panicked (`no case named "issue287_A"`) since they didn't exist yet.
- GREEN: `gen_changepoint_reference.py` gained `issue287_series(base, step_at_50)` (the #287 panel's exact SQL formula) and three new cases (`hazard_lambda=250`) carrying `expected_threshold_0_5` (reference `prob[t] >= 0.5` for `t >= 1`). Regenerating the fixture was purely additive (existing cases byte-identical in the diff). Anchors: `issue287_A -> []`, `issue287_B -> [50]`, `issue287_C -> []`.
- `detection_decomposition.test`: with `MAP{'threshold': '0.5'}` on the raw #287 panel -- zero rows-after-the-first disagree with the threshold rule, zero first rows are ever flagged, on both surfaces; the readable form is exactly `B 2024-02-20`; table-function/aggregate parity holds with threshold+hazard_lambda combined; `MAP{'threshold': '1.5'}` errors naming `threshold` on both surfaces; the BUG-07 block pins `ts_detect_periods_by` returning `primary_period 7.0` / `periods[1].period 7.0` for A, B, C.

## Verification (observed)

- **Task 1 tracer** (CLI): `ts_detect_changepoints_agg(ds, y, MAP{'hazard_lambda': '50'})` on the #287 panel -> `A,8;B,6;C,6;` (exact match to the plan's expected value).
- **Task 2 tracer-style verify** (CLI): `ts_detect_changepoints_by`/`ts_detect_changepoints_agg` with `MAP{'threshold': '0.5'}` on the raw #287 panel -> `A,none,none;B,2024-02-20,2024-02-20;C,none,none;` on both surfaces. `MAP{'threshold': '1.5'}` -> `"Invalid parameter 'threshold' = '1.5': must be a finite number in [0, 1]"` on both `ts_detect_changepoints_by` and `ts_detect_changepoints_agg`. `MAP{'threshold': 'abc'}` -> `"invalid 'threshold' value 'abc' -- must be a number in [0, 1]"` (caught at the C++ parse layer, before Rust validation runs).
- `cargo test -p anofox-fcst-core --lib changepoint`: 26 passed, 0 failed.
- `cargo test -p anofox-fcst-core --test changepoint_reference`: 5 passed, 0 failed.
- `HOME=$(mktemp -d) ./build/release/test/unittest test/sql/correctness/detection_decomposition.test`: 113 assertions, all pass (up from 94 at the start of this plan; the #287 md5 default-output pins from 20-03 are unchanged).
- `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"`: **All tests passed (6165 assertions in 95 test cases)**.
- `cargo fmt --all -- --check` and `make audit`: exit 0 (after each task).

## Deviations from Plan

None -- plan executed exactly as written, including the TDD RED/GREEN split for Tasks 2 and 3. No REFACTOR commit was needed for either TDD task (the GREEN implementations were already clean).

### Out-of-scope discoveries

None new.

## Known Stubs

None.

## Requirement status

BUG-06 (#287 agg/table params parity + threshold/D2-ii) and BUG-07 (ts_detect_periods_by period-7 regression pin) are both complete per this plan's verification.

## Self-Check: PASSED

- FOUND: commits 5b61688, 5b239d1, b9de686, cec3d3f, 9392ea2 (`git log --oneline`)
- FOUND: `src/include/ts_changepoints_params.hpp` on disk
- FOUND: `grep -c ParseChangepointParamSet src/table_functions/ts_changepoints.cpp src/aggregate_functions/ts_changepoints_agg.cpp` -> 1 each
- FOUND: `grep -c anofox_ts_detect_changepoints_bocpd_opts src/include/anofox_fcst_ffi.h` -> 2
- FOUND: `grep -c issue287 validation/reference/fixtures/changepoints/bocpd_cases.json` -> 3
- FOUND: `detection_decomposition.test` -- 113 assertions pass; full native suite -- 6165 assertions in 95 test cases pass
