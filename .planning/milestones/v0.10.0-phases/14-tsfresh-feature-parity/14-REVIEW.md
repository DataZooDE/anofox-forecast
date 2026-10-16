---
phase: 14-tsfresh-feature-parity
reviewed: 2026-10-12T00:00:00Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - crates/anofox-fcst-core/src/features.rs
  - test/sql/ts_features_reference.test
findings:
  critical: 0
  warning: 0
  info: 2
  total: 2
status: clean
---

# Phase 14: Code Review Report (iteration 2 — re-review of --auto fix loop)

**Reviewed:** 2026-10-12T00:00:00Z
**Depth:** standard
**Files Reviewed:** 2
**Status:** clean

## Summary

This is a re-review of commit `59a3e82`, which claims to fix WR-01 from iteration 1
(`linear_trend()`'s n=1 short-circuit hardcoding `(0.0, values[0], 0.0)` instead of
scipy's actual all-`NaN` output). Scope is narrow and proportionate to the size of the fix.

**WR-01 is genuinely resolved.** Verified independently in this review, not just by
reading the diff:

- `linear_trend()` (`features.rs:694-702`) now returns `(f64::NAN, f64::NAN, f64::NAN)`
  for `values.len() < 2`, matching scipy's documented n<2 behavior.
- Live scipy probe run against the pinned reference environment in this review
  (`uv run --locked --project validation/reference/python python`):
  ```
  >>> linregress(np.arange(1), np.array([5.0]))
  LinregressResult(slope=nan, intercept=nan, rvalue=nan, ...)
  ```
- Reproduced end-to-end against the actual built extension
  (`build/release/duckdb -unsigned`, `LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'`):
  a one-row group now returns `linear_trend_slope`/`intercept`/`r_squared` = `nan`/`nan`/`nan`
  (previously `0.0`/`5.0`/`0.0`).
- `cargo test -p anofox-fcst-core --lib test_linear_trend_nan_for_n_lt_2` passes and
  genuinely exercises both the private helper (n=1, n=0) and the FFI/SQL-reachable
  `extract_features(&[5.0])` path — not vacuous.
- `./build/release/test/unittest test/sql/ts_features_reference.test` passes for real
  against the built binary (111 assertions in 1 test case), including the three new
  `isnan(...)` assertions. The test file is `require anofox_forecast`-gated only (no
  json/pending gate), so it runs on every native build as claimed. The new assertions are
  not vacuous: `isnan(0.0)` is `false`, so they would have failed against the pre-fix code.
- Full regression suites re-run clean in this review: `cargo test -p anofox-fcst-core
  --lib` (356 passed), `cargo test -p anofox-fcst-core --test
  tsfresh_features_reference` (12 passed, including the unchanged 117/117
  `tsfresh_parity_*` series — none of which has n=1, consistent with the fixer's stated
  reasoning for not adding a fixture), `cargo fmt --all --check` (clean).

**No new defect was introduced by the fix:**

- `linear_trend()` has exactly one call site (`features.rs:218`, inside
  `extract_features`) — no other of the 117 features is derived from its output, so there
  is no secondary feature that could inherit a stale `0.0`-vs-`NaN` inconsistency.
- The n=2 boundary (the next value up from the changed n<2 branch) was explicitly
  re-checked for regression: `linear_trend(&[3.0, 7.0])` and the equivalent SQL call both
  produce `slope=4.0, intercept=3.0, r_squared=1.0`, matching
  `scipy.stats.linregress(range(2), [3.0, 7.0])` (`rvalue=1.0 → r_squared=1.0`) exactly —
  the n>=2 arithmetic path is untouched by this commit and was not regressed.
- The only other consumer of `linear_trend_*` column names is a straight passthrough
  struct-field selection in `src/macros/ts_macros.cpp:2000-2002` (no NaN-sensitive
  comparison or arithmetic on the C++ side), so NaN propagates cleanly to SQL output as
  intended.
- The fixer's claim that `aggregated_linear_trend` (the separate `agg_linear_trend_*`
  chunk-based feature, a different function) needed no change was spot-checked: it has its
  own independent `scipy_linregress`-style computation, is not called by or related to
  `linear_trend()`, and its guard (`chunk_len >= n -> NaN`) means a true single-point input
  can't reach the degenerate branch the same way — this is a correct scope boundary, not a
  missed sibling bug.

No new WARNING or CRITICAL findings from this re-review. IN-01 and IN-02 from iteration 1
remain unaddressed (expected — both were explicitly out of scope for this fix per
`fix_scope: critical_warning`) and are carried forward below unchanged, still Info-level.

## Structural Findings (fallow)

None provided for this re-review (no `<structural_findings>` block).

## Narrative Findings (AI reviewer)

No Critical or Warning findings in this iteration. See Info items below (carried forward
from iteration 1, unaddressed by design).

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

_Reviewed: 2026-10-12T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
