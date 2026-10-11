---
phase: 14-tsfresh-feature-parity
reviewed: 2026-10-11T22:49:01Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - crates/anofox-fcst-core/Cargo.toml
  - crates/anofox-fcst-core/src/features.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-core/tests/tsfresh_features_reference.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/macros/ts_macros.cpp
  - test/sql/ts_agg_duplicate_timestamp_tiebreak.test
  - test/sql/ts_features_reference.test
  - validation/reference/python/gen_tsfresh_features.py
  - validation/reference/fixtures/tsfresh_features/mapping.json
findings:
  critical: 0
  warning: 1
  info: 2
  total: 3
status: issues_found
---

# Phase 14: Code Review Report

**Reviewed:** 2026-10-11T22:49:01Z
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

This phase rewrites `anofox-fcst-core::features::extract_features` to be bit-faithful to
tsfresh 0.21.2 across all 117 features, backed by a hand-maintained parity contract
(`mapping.json`) and a Python fixture generator that calls tsfresh's calculators directly
(never the settings-driven convenience API). The work is unusually well-evidenced:

- `cargo test -p anofox-fcst-core` (unit tests + the `tsfresh_features_reference`
  integration suite) passes cleanly — 42 lib tests + 12 reference tests, all green,
  independently re-run during this review.
- `mapping.json` has exactly 117 rows, no duplicates, no `pending:*` status rows, and the
  name set matches `list_features()` exactly (verified live, not just by reading the
  test).
- The fixture generator genuinely invokes the installed tsfresh 0.21.2 / scipy 1.18.1 /
  numpy 2.5.3 (version-pinned and enforced via `check_tool_versions`); spot-checked
  fixture values (e.g. `short_n20.sample_entropy == ln(3)`) are real computed tsfresh
  output, not placeholders.
- `assert_close`'s NaN/NaN handling was checked for vacuous-pass risk: a NaN on only one
  side still fails the bound check (`NaN - number = NaN`, which is never `<= bound`), so
  the gate cannot silently accept a wrong numeric value by miscomparing against NaN.
- The `ts_stats` skewness/kurtosis refactor into shared `sample_skewness_g1` /
  `sample_excess_kurtosis_g2` helpers is a verbatim code move (confirmed via diff) —
  bit-identical, no behavior change for `ts_stats`.
- End-to-end SQL verification against the built extension (`ts_features_by`,
  `ts_features_table`) was run live for this review and matches the Rust/fixture values,
  including the new `autocorrelation_lag10` column and `last_location_of_maximum`'s
  `(idx+1)/n` fix (which the `ts_agg_duplicate_timestamp_tiebreak.test` update correctly
  reflects).
- No panics were reproducible from SQL for n=1, constant, all-zero, or very-large-value
  (1e308) series; NaN propagates cleanly instead.

One genuine numeric-edge-case defect was found (below): the `linear_trend` helper's own
n=1 short-circuit returns non-NaN placeholder values that this phase's own fix to the
same function's degenerate-case branch (`ss_xx`/`ss_yy == 0 → NaN`, in 14-02) did not
extend to cover, leaving an inconsistency with scipy's actual n=1 `linregress` output and
with sibling n=1 handling elsewhere in the same file (`mean_change`,
`mean_second_derivative_central` correctly emit NaN for n=1).

## Warnings

### WR-01: `linear_trend_slope`/`linear_trend_intercept`/`linear_trend_r_squared` silently wrong (not NaN) for n=1 series

**File:** `crates/anofox-fcst-core/src/features.rs:694-699`

**Issue:** `linear_trend()`'s early-return for a 1-point series hardcodes
`(slope=0.0, intercept=values[0], r_squared=0.0)`:

```rust
fn linear_trend(values: &[f64]) -> (f64, f64, f64) {
    if values.len() < 2 {
        return (0.0, values.first().cloned().unwrap_or(0.0), 0.0);
    }
    ...
```

tsfresh's `linear_trend` calculator calls `scipy.stats.linregress(range(n), x)` directly,
and scipy 1.18.1's actual behavior for `n=1` is **NaN for every field**, confirmed live
against the installed reference tool in this review:

```
>>> scipy.stats.linregress(np.arange(1), np.array([5.0]))
LinregressResult(slope=nan, intercept=nan, rvalue=nan, pvalue=nan, stderr=nan, intercept_stderr=nan)
```

Reproduced end-to-end through the built extension (not just the Rust unit, confirming the
bug is reachable from ordinary SQL — any group with exactly one row):

```sql
CREATE TABLE one_row AS SELECT 'a' id, DATE '2024-01-01' ds, 5.0 v;
SELECT linear_trend_slope, linear_trend_intercept, linear_trend_r_squared
FROM ts_features_by('one_row', id, ds, v);
-- 0.0 | 5.0 | 0.0   (want: NaN | NaN | NaN)
```

This is exactly the class of defect this phase otherwise took great care to eliminate
elsewhere (silent `0.0` instead of `NaN` for a degenerate/underdetermined fit): the
sibling fix in this same function, two lines below, replaced a hardcoded `0.0`
`r_squared` with `f64::NAN` for the `ss_xx == 0 || ss_yy == 0` branch and explicitly
documents "only `ss_yy == 0` ... is reachable, since x ... always varies for n >= 2" —
correctly reasoning about the `n >= 2` path but not revisiting the `n < 2` short-circuit
one level up, which the 14-01/14-02 parity contract and fixtures never exercise (no
fixture series has `n == 1`), so the 117/117 gate does not catch it.

Other n=1 branches in the same file get this right already (e.g.
`mean_change`/`mean_abs_change`/`mean_second_derivative_central` all explicitly emit
`f64::NAN` for `n <= 1`/`n <= 2`), making this short-circuit the outlier.

**Fix:**
```rust
fn linear_trend(values: &[f64]) -> (f64, f64, f64) {
    if values.len() < 2 {
        return (f64::NAN, f64::NAN, f64::NAN);
    }
    ...
```
Add a fixture/unit test pinning `n=1` against a live scipy probe (as this review did) so a
future change can't silently regress it back to `0.0` without the 117/117 gate noticing.

## Info

### IN-01: `extract_features`'s 117-key invariant guard is debug-only

**File:** `crates/anofox-fcst-core/src/features.rs:348-354`

**Issue:** The invariant that every call to `extract_features` returns exactly the 117
keys `list_features()` declares is enforced via `debug_assert_eq!`, which compiles to a
no-op in release builds (the ones DuckDB actually loads). A future change that
accidentally drops/duplicates a key would ship silently in `make release` and only be
caught by `cargo test` (debug profile) — not by the FFI/SQL layer, which degrades
gracefully (missing keys fall back to NULL in the C++ struct lookup) rather than erroring
loudly. This is a reasonable, documented tradeoff (`list_features()` allocates per call),
not a defect, but worth flagging since the invariant's guarantee is weaker than the doc
comment ("117-key output invariant") implies for production builds.

**Fix:** Low priority. If this invariant is considered load-bearing for the FFI
contract, consider a `#[cfg(test)]`-only exhaustive test across a wider range of `n`
(already partially done in `test_117_key_invariant_across_lengths`) rather than relying
on `debug_assert!` to catch regressions in any build profile.

### IN-02: Duplicate inline `mean`/`variance` recomputation across entropy/peak helpers

**File:** `crates/anofox-fcst-core/src/features.rs:823-833, 859-861, 891-893`

**Issue:** `count_peaks_threshold`, `sample_entropy`, and `approximate_entropy` each
recompute `mean`/population `variance` from scratch even though `extract_features`
already computed an equivalent `mean`/`variance` earlier in the same function (and the
`r` thresholds for peaks reuse the already-computed `std_dev` via its own call site, but
the *internal* mean inside `count_peaks_threshold` is independently recomputed). Purely a
quality/duplication nit — functionally correct and verified bit-identical by the passing
test suite — not a correctness bug.

**Fix:** Pass `mean`/`std_dev` as parameters where already available at the call site to
avoid repeated O(n) passes per feature; out of scope for this review's correctness focus
given it's explicitly a non-functional cleanup.

---

_Reviewed: 2026-10-11T22:49:01Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
