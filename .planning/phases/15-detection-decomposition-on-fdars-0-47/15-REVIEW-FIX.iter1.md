---
phase: 15-detection-decomposition-on-fdars-0-47
fixed_at: 2026-10-14T09:10:00Z
review_path: .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 15: Code Review Fix Report

**Fixed at:** 2026-10-14T09:10:00Z
**Source review:** .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope (critical + warning): 5
- Fixed: 5
- Skipped: 0

Verification ran in the main working tree on branch `fix/statistical-correctness`
(`workflow.use_worktrees` is `false` for this project per project memory — no
isolated worktree was created; all edits, builds, and tests below are directly
reproducible from the current checkout).

## Fixed Issues

### CR-01: `_ts_mstl_decomposition` reads uninitialized memory when a requested period is unusable under `insufficient_data = 'none'`

**Files modified:** `src/table_functions/ts_decomposition.cpp`, `test/sql/ts_detection_reference.test`
**Commit:** `d381dfb` — fix(15): CR-01 stop _ts_mstl_decomposition from exposing uninitialized memory
**Applied fix:** Mirrored the table-in-out sibling (`ts_mstl_decomposition_native.cpp`)'s guard: the trend/remainder `ListVector` length is now `0` whenever the corresponding Rust pointer is null (i.e. `decomposition_applied == false`), instead of unconditionally using `n_observations` (the full input length) and leaving the buffer un-zeroed/stale. Added a SQL regression test hitting `insufficient_data = 'none'` with a period that is never usable (`[1000]` on a 20-row series), asserting a non-NULL result with empty `trend`/`remainder`/`periods`.

### CR-02: Unbounded/negative `n_frequencies` on `ts_lomb_scargle` can abort the DuckDB process

**Files modified:** `crates/anofox-fcst-core/src/periods.rs`, `src/table_functions/ts_periods.cpp`, `test/sql/ts_detection_reference.test`
**Commit:** `80ea2cf` — fix(15): CR-02 bound ts_lomb_scargle's n_frequencies to stop a process abort
**Applied fix:** Added an explicit upper bound (`nf > 1_000_000`) alongside the existing lower bound in `lomb_scargle`'s validation, matching the review's suggested fix. Also hardened the C++ caller to reject a negative `n_frequencies` with `InvalidInputException` before the `int64_t -> size_t` cast (rather than relying solely on the Rust-side bound to catch the wrapped value). Added two Rust unit tests (`test_lomb_scargle_n_frequencies_upper_bound_rejected`, `test_lomb_scargle_n_frequencies_lower_bound_rejected`) and two SQL regression tests for a negative and an absurdly large (2e9) `n_frequencies`.

### CR-03: `period <= 0.0` validation never rejects `NaN`, letting a `NaN` period silently produce wrong (non-error, non-NULL) results across ~9 functions

**Files modified:** `crates/anofox-fcst-core/src/detrending.rs`, `crates/anofox-fcst-core/src/seasonality.rs`, `crates/anofox-fcst-core/src/peaks.rs`, `src/table_functions/ts_seasonality.cpp`, `test/sql/ts_detection_reference.test`
**Commit:** `eba3904` — fix(15): CR-03 reject non-finite period across detrend/seasonality/peaks
**Applied fix:** Changed all 9 cited `period <= 0.0` guards (2 in `detrending.rs`, 6 in `seasonality.rs`, 1 in `peaks.rs`) to `!period.is_finite() || period <= 0.0`, per the review's suggested fix. While writing the Rust unit tests I found that an `f64::INFINITY` period additionally triggered an integer-overflow panic in 5 of these functions (`classify_seasonality`, `detect_seasonality_changes`, `detect_amplitude_modulation`, `decompose_additive`, `decompose_multiplicative`): their insufficient-data guard `n < 2 * period as usize` ran *before* the period-validity check, and `period as usize` saturates to `usize::MAX` for `+infinity`, so `2 * usize::MAX` overflows. Reordered the finiteness check to run first in all 5 (load-bearing ordering, documented inline), since the review's requested NaN/inf coverage surfaced this adjacent bug.

Additionally — while writing the SQL regression assertion for `ts_classify_seasonality('nan'::DOUBLE)` — found that its C++ wrapper (`src/table_functions/ts_seasonality.cpp`, not in the review's reviewed-file list) has its own pre-check `static_cast<size_t>(2 * period)`, which is undefined behaviour in C++ for a NaN/infinite `period` and was silently returning `NULL` instead of the documented SQL error. Added an explicit `std::isfinite` check that throws `InvalidInputException` before that cast. This is outside the review's listed file scope but is the same defect class as CR-03 and was necessary for the review-mandated SQL assertion to pass correctly (rather than weakening the assertion to tolerate NULL).

Added Rust unit tests for NaN and +infinity in `peaks.rs` (`test_invalid_period`) and `seasonality.rs` (`test_invalid_period_rejects_non_finite`, covering all 6 seasonality functions). Added 3 SQL regression assertions using `'nan'::DOUBLE` for `ts_seasonal_strength`, `ts_decompose_seasonal`, and `ts_classify_seasonality`.

### WR-01: `estimate_period_regression_ts` has no `period_min > 0` validation

**Files modified:** `crates/anofox-fcst-core/src/periods.rs`
**Commit:** `8393cbb` — fix(15): WR-01 validate period_min in estimate_period_regression_ts
**Applied fix:** Mirrored `lomb_scargle`'s validation exactly as the review suggested: reject a non-finite or non-positive `period_min` with `InvalidParameter`. This function has no FFI export / SQL caller today (confirmed via grep, matching the review's IN-01 note), so no SQL test was added — the review did not request one for this latent, non-reachable gap.

### WR-02: PELT's exact-safe pruning has no bound on `live` set growth for adversarial inputs

**Files modified:** `crates/anofox-fcst-core/src/changepoint.rs`
**Commit:** `5d300fd` — docs(15): WR-02 document PELT pruning's unbounded worst case
**Applied fix:** No behavior change, per the review's own fix guidance ("No action required while PELT stays Rust/FFI-only"). Added a doc comment on `detect_changepoints` documenting the unbounded worst case and recommending a `live.len()` cap (analogous to BOCPD's `MAX_RUN_LENGTH`) if a SQL surface is ever added.

## Skipped Issues

None — all 5 in-scope findings (3 critical, 2 warning) were fixed. IN-01 was out of scope (`fix_scope: critical_warning`) and required no action per the review itself (context note for WR-01, not an issue).

## Verification

- `cargo fmt --all --check`: clean.
- `cargo test --workspace`: all passed (431 unit tests in `anofox-fcst-core` including the new CR-02/CR-03 tests, plus all integration/doc tests across the workspace — 0 failures).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: succeeded (only pre-existing, unrelated SFINAE compiler warnings from DuckDB headers).
- `make test_release`: **all tests passed — 2042 assertions in 32 test cases** (50 json-gated tests skipped, as expected), up from the pre-fix baseline and above the 2035-assertion floor noted in the phase guidance. `test/sql/ts_detection_reference.test` (non-json-gated) carries all 8 new regression assertions (2 for CR-01, 2 for CR-02, 3 for CR-03, plus the corrected pre-existing `n_frequencies`/`min_period` tests which were unaffected).
- `src/include/anofox_fcst_ffi.h` was **not** regenerated/committed: no FFI function signature changed (CR-02's `n_frequencies` is already `size_t` at the FFI boundary; only the Rust-side bound and the C++ caller's pre-cast guard changed).

---

_Fixed: 2026-10-14T09:10:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
