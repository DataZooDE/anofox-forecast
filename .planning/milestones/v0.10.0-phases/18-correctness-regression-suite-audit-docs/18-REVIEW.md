---
phase: 18-correctness-regression-suite-audit-docs
reviewed: 2026-10-15T21:33:00Z
depth: standard
files_reviewed: 21
files_reviewed_list:
  - crates/anofox-fcst-core/src/conformal.rs
  - crates/anofox-fcst-core/src/decomposition.rs
  - crates/anofox-fcst-core/src/detrending.rs
  - crates/anofox-fcst-core/src/features.rs
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/peaks.rs
  - crates/anofox-fcst-core/src/periods.rs
  - crates/anofox-fcst-core/src/seasonality.rs
  - crates/anofox-fcst-core/tests/stl_reference.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - extension_config.cmake
  - .github/workflows/audit.yml
  - Makefile
  - scripts/audit_no_stats_in_cpp.py
  - scripts/check_correctness_inventory.py
  - scripts/check_doc_snippets.py
  - scripts/check_length_only_tests.py
  - src/include/ts_forecast_params.hpp
  - src/macros/ts_macros.cpp
  - src/table_functions/ts_cv_split_native.cpp
  - src/table_functions/ts_fill_forward_native.cpp
  - test/sql/correctness/conformal_bootstrap.test
  - test/sql/correctness/data_prep.test
  - test/sql/correctness/data_quality.test
  - test/sql/correctness/detection_decomposition.test
  - test/sql/correctness/diagnostics.test
  - test/sql/correctness/features.test
  - test/sql/correctness/forecast_parity.test
  - test/sql/correctness/metrics.test
  - test/sql/correctness/stats.test
  - test/sql/ts_cv_split.test
  - test/sql/ts_integer_frequency.test
  - test/sql/ts_table_macro_aliases.test
  - test/sql/ts_forecast_ets_model.test
findings:
  critical: 0
  warning: 5
  info: 2
  total: 7
status: issues_found
---

# Phase 18: Code Review Report

**Reviewed:** 2026-10-15T21:33:00Z
**Depth:** standard
**Files Reviewed:** 21 (plus spot-checked `.test` files)
**Status:** issues_found

## Summary

Reviewed the git diff `09c8279..HEAD` restricted to the Phase 18 file set: the four production fixes from plan 18-01 (frequency `::VARCHAR` casts, `target_date` parsing fix, CV-split embargo implementation, ETS-only `model` validation), the `extension_config.cmake` json static-link addition, the four new audit/inventory/doc-snippet/length-only scripts plus `audit.yml`, the Rust test-value upgrades from plan 18-02, and a sample of the 18-01 SQL test re-pins.

This is unusually well-engineered work. Every script was actually executed against the live build during this review (not just read): `audit_no_stats_in_cpp.py` (0 violations / 0 stale, 34 real hits all allow-listed with specific per-line regexes), `check_length_only_tests.py` (0 un-allow-listed findings, self-test and `--check-module-list` both pass), `check_correctness_inventory.py` (0 missing, 116 covered), and `check_doc_snippets.py` (343 PASS / 0 FAIL / 0 STALE) — none of the four gates is vacuous; all scan real content and would fail on a real regression. The CV-split embargo fix was independently cross-validated live against the already-correct `ts_cv_split_folds_by` SQL macro on both single- and multi-group fixtures (including a case where two groups have different start dates and position offsets) and the positions agree with the date-based macro exactly. The sampled SQL test re-pins (`ts_cv_split.test`, `ts_integer_frequency.test`, `ts_table_macro_aliases.test`) and the new `test/sql/correctness/*.test` files all cite closed-form derivations or independent fixtures rather than blessing whatever the current build happens to output, and several were verified by hand in this review (sMAPE/R2 closed forms, MASE fixture value, 69-day inclusive date-range arithmetic).

The issues below are all WARNING/INFO tier — none blocks shipping, but two (the `std::stoll` overflow regression and the pre-existing NULL-frequency bug surfaced while probing the reviewed fix's edge cases) are genuine, reproduced-live defects worth fixing.

## Warnings

### WR-01: `std::stoll` overflow in the fixed `target_date` parser produces a leaked, unhelpful C++ exception instead of a clean DuckDB error

**File:** `src/table_functions/ts_fill_forward_native.cpp:127-136`
**Issue:** The 18-01 fix correctly replaced the old "parse as integer first, catch-and-fallback-to-timestamp" logic with a full-string `^-?[0-9]+$` regex match before calling `std::stoll`. But the fix also **removed** the surrounding `try { ... } catch (...) { ... }` that the old code had. A digit-only string that overflows `int64_t` (e.g. a user fat-fingering an 11-day lookalike date string, or any `target_date` literal with 19+ digits) now matches the regex, so `std::stoll` is called unguarded and throws `std::out_of_range`, which is not caught anywhere before it crosses the DuckDB bind boundary. Reproduced live against the built extension:
```
$ echo "LOAD anofox_forecast; CREATE TABLE t AS SELECT 'A' id, '2023-01-01'::DATE ds, 1.0 val; SELECT * FROM ts_fill_forward_by('t', id, ds, val, '99999999999999999999', '1d');" | ./build/release/duckdb -unsigned :memory:
Invalid Error: stoll
```
"Invalid Error: stoll" is the raw `std::out_of_range::what()` text, not a message naming the parameter or the problem — a clear regression in error-message quality versus the pre-fix behavior (which would have fallen through to `DefaultCastAs(TIMESTAMP)` and produced a normal, explanatory cast error). Does not crash the process, but is a real, user-reachable degradation introduced by this plan's own Rule-1 fix.
**Fix:** Re-wrap the `std::stoll` call (or bound the digit count before calling it):
```cpp
static const std::regex int_regex("^-?[0-9]+$");
if (std::regex_match(target_str, int_regex)) {
    try {
        bind_data->target_date_micros = std::stoll(target_str);
    } catch (const std::out_of_range &) {
        throw InvalidInputException(
            "target_date value '%s' is out of range for a raw integer day-offset", target_str);
    }
    bind_data->target_is_raw = true;
} else {
    ...
}
```

### WR-02: Pre-existing NULL-frequency bug surfaced while validating the reviewed `frequency::VARCHAR` cast fix

**File:** `src/table_functions/ts_fill_gaps_native.cpp:207-213` (not touched by this diff, but on the exact call path the diff's `frequency::VARCHAR` cast in `src/macros/ts_macros.cpp` feeds into)
**Issue:** `TsFillGapsNativeBind` calls `input.inputs[1].GetValue<string>()` without checking `IsNull()` first. A SQL `NULL` frequency argument (e.g. `ts_fill_gaps_by('t', id, ds, val, NULL)`) is not caught by the null check and resolves to the **literal string `"NULL"`** (DuckDB's `Value::GetValue<string>()` on a null value stringifies rather than throwing), which then fails `ParseFrequencyWithType` with a confusing message:
```
Invalid Input Error: Invalid frequency 'NULL'. Valid formats: ...
```
This predates the Phase 18 diff (the `::VARCHAR` cast added in `ts_macros.cpp` doesn't change this — `CAST(NULL AS VARCHAR)` is still a real SQL NULL, confirmed live), but it sits directly on the code path this plan's fix review was asked to validate for "NULL frequency," and the sibling `ts_fill_forward_native.cpp` (fixed by this same plan) already guards its own optional-argument parsing with `!input.inputs[1].IsNull()`. Worth fixing alongside, or at minimum tracking in `deferred-items.md` the way the other two out-of-scope gaps from this plan were.
**Fix:**
```cpp
if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
    string freq_str = input.inputs[1].GetValue<string>();
    ...
}
```

### WR-03: Several Rust/FFI "value check" test upgrades use sanity ranges too wide to catch realistic regressions

**File:** `crates/anofox-fcst-core/src/forecast.rs:4927-4941`, `crates/anofox-fcst-core/src/forecast.rs:5535-5548`, `crates/anofox-fcst-ffi/src/lib.rs:8514-8524, 8550-8564, 8604-8620, 9065-9075`
**Issue:** The 18-02 upgrade from length-only assertions to value assertions is a genuine improvement, and none of the new assertions are tautological. However, several use very wide "sane magnitude" bounds relative to the input range, e.g.:
- `forecast.rs:4927`: input `100..159` (strictly increasing), asserted bound `(50.0..=400.0)` — a forecast more than 2.5x the true trend ceiling would still pass.
- `forecast.rs:5535`: input `100..194`, bound `(50.0..=400.0)` — same issue.
- `lib.rs:9065` (Kalman): input `10..22`, bound `(0.0..=40.0)` — nearly double the true range still passes.
- `lib.rs:8514/8550/8604` (panel ETS/Theta): per-series bounds `(0.0, 30.0)`/`(0.0, 40.0)`/`(0.0, 25.0)` against series whose own ranges are roughly `1..12`/`3..19.5`/`2.4..11.2` — again, 2-3x the true range still passes.
These are better than pure `.len()`/`.is_finite()` checks (they would catch a sign flip, a total blowup, or an obviously garbage constant), but they would not catch the class of regression the "silently returns the right shape of garbage data" framing in `check_length_only_tests.py`'s own docstring warns about (e.g. a forecast that's systematically 1.5-2x too high/low, or ETS converging on the wrong component).
**Fix:** Where an exact closed form isn't available (these models involve iterative fitting), tighten the bound to e.g. `last_observed_value +/- (2 * observed amplitude or trend span)` rather than a flat multiple, or add a looser secondary assertion that the forecast tracks the local trend direction (e.g. `point.last() > point.first()` for a rising series) in addition to the magnitude bound.

### WR-04: `check_correctness_inventory.py` and `check_doc_snippets.py` are not wired into any CI workflow

**File:** `.github/workflows/audit.yml`, `Makefile:30-39`
**Issue:** `audit.yml` only runs `audit_no_stats_in_cpp.py --self-test`, `audit_no_stats_in_cpp.py`, and `check_length_only_tests.py`. The other two Phase 18 gates — `check_correctness_inventory.py` (TEST-03: every CHANGELOG-mentioned registered function has correctness-suite coverage) and `check_doc_snippets.py` (DOCS-01: every doc/skill SQL snippet actually runs against the built extension) — are only reachable via `make check-docs` (docs snippets) or a bare `python3` invocation (inventory), neither of which any workflow in `.github/workflows/` calls. Both require a full `make release` first, which is presumably why they were kept out of the 5-minute `audit.yml` job, but as it stands nothing in CI will catch a future PR that removes correctness coverage for a function, or breaks a doc snippet, until someone remembers to run these by hand. (Confirmed by inspection: `grep check_doc_snippets.py\|check_correctness_inventory.py .github/workflows/*.yml` returns no hits.)
**Fix:** Add a job to `MainDistributionPipeline.yml` (or a new lightweight workflow) that runs `make release && python3 scripts/check_correctness_inventory.py && make check-docs` after a build job completes, even if only on a schedule or on `main` pushes to keep CI time reasonable.

### WR-05: `extension_config.cmake`'s new `duckdb_extension_load(json)` statically links json into the build despite being documented as "test-time only"

**File:** `extension_config.cmake:3-10`
**Issue:** The added comment states "json is required at test-time only," but the call itself (`duckdb_extension_load(json)`, no `DONT_LINK`) uses the same linking mode DuckDB's own `in_tree_extensions.cmake` uses for extensions meant to be permanently bundled (`autocomplete`, `core_functions`, `icu`, `json`, `parquet`, ...), not the `DONT_LINK` mode that `bundled_extensions.cmake` explicitly uses for genuinely test-only dependencies (`tpcds DONT_LINK`, `tpch DONT_LINK`). There's no `if(EMSCRIPTEN)`/build-type guard distinguishing "building for tests" from "building for distribution," so every build of this extension (including, plausibly, the one driven by `_extension_distribution.yml` in `MainDistributionPipeline.yml`) compiles and statically links `json` into whatever binary this file's build step produces. The 18-01-SUMMARY.md shows this was verified not to add OpenSSL/crypto `NEEDED` entries and to still build cleanly for WASM, which is good evidence it's benign today, but the code doesn't structurally enforce "test-time only" the way the comment claims, and there's no CI check (see WR-04) that would catch this drifting in the future (e.g. if a later change makes `json`'s own linking pull in something undesirable).
**Fix:** Either add `DONT_LINK` (matching the `tpcds`/`tpch` precedent for genuinely test-only in-tree extensions) if the test harness can load it dynamically via `LOAD json;` at runtime, or soften the comment to accurately describe that it's statically linked into every build, not just test runs.

## Info

### IN-01: Negative `embargo` is silently treated as a no-op rather than rejected

**File:** `src/table_functions/ts_cv_split_native.cpp:460` (`if (bind_data.embargo > 0 && has_prev_fold)`)
**Issue:** `embargo` is parsed via `ParseIntParam` with no range validation; a negative value (confirmed live: `MAP{'embargo': '-5'}` produces identical output to `MAP{'embargo': '0'}`) is silently absorbed rather than erroring. This exactly mirrors the pre-existing `ts_cv_split_folds_by` SQL macro's own `CASE WHEN _embargo > 0` guard, so it's not a regression introduced by this plan's fix — but since this plan did touch this exact code path to implement the embargo logic properly, it would have been a natural place to also add an explicit `InvalidInputException` for `embargo < 0` (consistent with the "silent fallback becomes an error" theme stated elsewhere in this same plan's own fixes, e.g. the `model`-is-ETS-only validation).
**Fix:** `if (bind_data.embargo < 0) { throw InvalidInputException("embargo must be >= 0, got %lld", bind_data.embargo); }` near where `gap`/`min_train_size` are consumed, applied consistently to both `_ts_cv_split_native` and `ts_cv_split_folds_by`.

### IN-02: `check_length_only_tests.py`'s `MILESTONE_MODULES` is a frozen, hand-maintained list

**File:** `scripts/check_length_only_tests.py:48-109`
**Issue:** The frozen module list is explicitly designed to be reproducible on a shallow CI checkout (documented rationale is sound), and `--check-module-list` exists to detect drift — but that drift-check is not run anywhere in CI either (same gap pattern as WR-04), so a new file added to `crates/` after this plan closes will silently never be swept for length-only tests until someone runs `--check-module-list` by hand.
**Fix:** Low priority given the milestone is closing, but worth a one-line note in `check_length_only_tests.py`'s own docstring (or a tracked backlog item) that this frozen list needs periodic re-validation or retirement once the broader length-only-test problem is solved project-wide.

---

_Reviewed: 2026-10-15T21:33:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
