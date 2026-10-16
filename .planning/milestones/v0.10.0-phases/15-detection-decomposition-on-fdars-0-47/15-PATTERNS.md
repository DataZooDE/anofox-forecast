# Phase 15: Detection & Decomposition on fdars 0.47 - Pattern Map

**Mapped:** 2026-10-12
**Files analyzed:** 18 (6 core modules modified mechanically, 4 algorithm re-implementations, 3 validation-reference generators, 2 Rust fixture tests, 1 SQL reference test, 1 shared stats refactor, 1 docs/skills sweep)
**Analogs found:** 18 / 18 (all analogs are in-repo; several are the files being modified against Phase 13/14 siblings that already solved the same shape of problem)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `crates/anofox-fcst-core/src/detrending.rs` (B1 fix, Plan 1) | utility (Rust FFI-adjacent core) | transform | itself (pre-bump), pattern from `seasonality.rs`'s already-correct calls at :499-634 | exact (same file, sibling function already right) |
| `crates/anofox-fcst-core/src/seasonality.rs` (B2 fix, Plan 1) | utility | transform | itself — correct calls at :499-634, :727-754 vs. buggy ones at :672, :798 | exact |
| `crates/anofox-fcst-core/src/periods.rs` (FdMatrix touch-up, non-migrated fns preserved, Plan 1/5) | utility | transform | itself — existing correct `(values, 1, n, …)` call sites at :312,352,387 | exact |
| `crates/anofox-fcst-core/src/peaks.rs` (mechanical FdMatrix wrap in Plan 1, full rewrite in Plan 2) | utility | transform | scipy `find_peaks` (external spec, no in-repo analog) for the rewrite; itself for the Plan-1 mechanical FdMatrix wrap | role-match (rewrite), exact (mechanical touch) |
| `crates/anofox-fcst-core/src/changepoint.rs` (BOCPD fix D-06, PELT fix D-07, Plan 3) | utility | transform / batch | itself — existing `// BUG FIX:` comment style, `detect_changepoints_bocpd` structure | exact (same file, same conventions) |
| `crates/anofox-fcst-core/src/decomposition.rs::mstl_decompose` (Plan 4) | utility | transform | `crates/anofox-fcst-core/src/stats.rs::stl_strengths` (lines ~457-476) + `crates/anofox-fcst-core/src/stl.rs::stl()` | exact — explicit reuse target named by research |
| `crates/anofox-fcst-core/src/filter.rs::is_constant` (D-13, Plan 5) | utility | transform | itself, lines 39-48 (one-function mechanical fix) | exact |
| `crates/anofox-fcst-core/src/imputation.rs` (docs-only touch, D-14, Plan 5) | utility | transform | itself — `fill_nulls_interpolate`/`fill_nulls_forward`/`fill_nulls_backward`, lines 9-127 | exact |
| `Cargo.toml` (workspace) + `crates/anofox-fcst-core/Cargo.toml` (DEP-03, Plan 1) | config | batch | itself — current `fdars-core = { version = "0.3", ... }` entries | exact |
| `validation/reference/python/gen_scipy_peaks_reference.py` (new, Plan 2) | utility (fixture generator) | file-I/O | `validation/reference/python/gen_tsfresh_features.py` (Phase 14) | exact — same `fixture_io.py` import, docstring, provenance pattern |
| `validation/reference/python/gen_bocpd_pelt_reference.py` (new, Plan 3) | utility (fixture generator) | file-I/O | `validation/reference/python/gen_tsfresh_features.py` (Phase 14) | role-match (independent-implementation generator rather than library-call generator, but same `fixture_io.py` plumbing) |
| `validation/reference/r/gen_mstl_decompose_reference.R` (new, Plan 4) | utility (fixture generator) | file-I/O | `validation/reference/r/gen_stl_strength_reference.R` (Phase 13) | exact — same `common.R` header, `forecast::mstl`/`stats::decompose` call shape |
| `validation/reference/python/gen_period_detection_reference.py` (new, Plan 5) | utility (fixture generator) | file-I/O | `validation/reference/python/gen_tsfresh_features.py` (Phase 14) | exact |
| `crates/anofox-fcst-core/tests/peaks_reference.rs` (new, Plan 2) | test | batch | `crates/anofox-fcst-core/tests/stl_reference.rs` (Phase 13) | exact |
| `crates/anofox-fcst-core/tests/changepoint_reference.rs` (new, Plan 3) | test | batch | `crates/anofox-fcst-core/tests/stl_reference.rs` (Phase 13) | exact |
| `crates/anofox-fcst-core/tests/mstl_decompose_reference.rs` (new, Plan 4) | test | batch | `crates/anofox-fcst-core/tests/stl_reference.rs` (Phase 13, same fixture family even) | exact |
| `crates/anofox-fcst-core/tests/period_detection_reference.rs` (new, Plan 5) | test | batch | `crates/anofox-fcst-core/tests/stl_reference.rs` (Phase 13) | exact |
| `test/sql/ts_detection_reference.test` (new, non-json-gated, all plans) | test | request-response | `test/sql/ts_features_reference.test` (Phase 14) | exact — same non-gated header comment, literal-copy-from-fixture convention |

## Pattern Assignments

### `crates/anofox-fcst-core/src/detrending.rs`, `seasonality.rs` (B1/B2 FdMatrix migration, Plan 1)

**Analog:** the file's own already-correct call sites, plus the documented before/after in RESEARCH.md §Code Examples.

**Before (buggy, B1 — swapped args):**
```rust
// detrending.rs:149
let result = fdars_detrend_linear(values, n, 1, &argvals);
```

**After (FdMatrix, mechanical fix):**
```rust
// Source: fdars-core 0.47.0 src/matrix.rs + src/detrend/linear.rs
let mat = fdars_core::matrix::FdMatrix::from_slice(values, 1, n)
    .map_err(|e| ForecastError::InvalidInput(e.to_string()))?;
let result = fdars_detrend_linear(&mat, &argvals);
```
Apply the identical `FdMatrix::from_slice(values, 1, n)` wrap to all 7 B1 call sites (detrending.rs:149,172,195,224,246,311,358) and the 2 B2 call sites (seasonality.rs:672,798). **Do not touch** `seasonality.rs`'s other fdars calls (:499-634, :727-754) — they already pass `(values, 1, n, &argvals, ...)` correctly; those just need the same `FdMatrix::from_slice` wrap with no logic change, since they migrated to `&FdMatrix` too in 0.47.

**Threshold type change (Pitfall 2):** `detect_amplitude_modulation_wavelet`/`detect_seasonality_changes` thresholds are `Option<f64>` in 0.3, plain `f64` in 0.47 — the fix is deleting the now-redundant `Option` wrapping around the existing `.unwrap_or(default)` output, not re-deriving the unwrap logic.

**Method-name validation (D-02, B3), no in-repo analog — write fresh using the project's existing error-construction idiom:**
```rust
let degree = match method {
    "linear" => 1,
    "quadratic" => 2,
    "cubic" => 3,
    other => return Err(ForecastError::InvalidParameter {
        param: "method".to_string(),
        value: other.to_string(),
        reason: "expected one of: linear, quadratic, cubic, loess, diff, auto".to_string(),
    }),
};
```
This mirrors the project's existing `ForecastError::InvalidParameter { param, value, reason }` shape (CLAUDE.md "Error Handling" / `crates/anofox-fcst-core/src/error.rs`), already used for `period <= 0.0` checks in `seasonality.rs`.

**Pitfall to avoid (Plan 1/5):** `periods.rs`'s `estimate_period_acf`, `estimate_period_regression`, `detect_multiple_periods` did NOT migrate to `&FdMatrix` — leave their raw `(data, n, m, argvals, ...)` signatures and existing correct call order (`periods.rs:312,352,387`) untouched; do not wrap them in `FdMatrix`.

---

### `crates/anofox-fcst-core/src/peaks.rs` (Plan 1 mechanical touch, Plan 2 full rewrite, DET-01)

**Plan 1 analog:** itself — the file already passes the correct `(values, 1, m, &argvals, ...)` order (lines 146-160,211-216,304-315); only needs `FdMatrix::from_slice(values, 1, m)` inserted before the (soon-to-be-discarded) `fdars_detect_peaks` call so the crate compiles against 0.47.

**Plan 2 analog:** no in-repo analog (D-05 is a from-scratch scipy `find_peaks` port). Build directly from the documented scipy algorithm (plateau → midpoint index; prominence via nearest-higher-neighbour `wlen` window; greedy height-based distance pruning). Existing SQL parameter names/wiring in `src/table_functions/ts_peaks.cpp` and `anofox_ts_detect_peaks` FFI stay the same — only the Rust body changes. Keep the existing regression-test convention: `peaks.rs`'s own `test_issue_59_peaks_detected` (line 304) is the project's precedent for "regression test for GitHub issue #N, quoting the bug in the doc comment" — follow this style for the new scipy-parity tests.

---

### `crates/anofox-fcst-core/src/changepoint.rs` (Plan 3, D-06/D-07)

**Analog:** itself — existing `// BUG FIX:` / `// FIX:` comment convention (lines 264-297) shows the project's precedent for annotating a changepoint-logic correction in place; follow the same inline-comment style for the new Student-t normaliser, Normal-Gamma update, renormalised truncation, MAD-based PELT penalty, and median-based L1 cost.

**Student-t normaliser gap (G3), current code:**
```rust
// :264-270 (current, missing normalising constant)
let scale = ((beta_n * (kappa_n + 1.0)) / (alpha_n * kappa_n)).sqrt();
let z = (x - mu_n) / scale.max(1e-10);
let nu = 2.0 * alpha_n;
pred_prob[r] = (1.0 + z.powi(2) / nu).powf(-(nu + 1.0) / 2.0);
```
Add the `Γ((ν+1)/2) / (Γ(ν/2)·√(νπ)·scale)` normalising constant (per D-06).

**Truncation drops mass (G5), current code:**
```rust
// :251-260
let max_keep = 500;
if run_length_prob.len() > max_keep {
    run_length_prob.truncate(max_keep);
    sum_x.truncate(max_keep);
    sum_x2.truncate(max_keep);
    run_counts.truncate(max_keep);
}
```
Renormalise `run_length_prob` to sum to 1 after truncation.

**PELT L1 cost (G8), mean → median:**
```rust
// :44-58 current (buggy)
fn cost_l1(values: &[f64], start: usize, end: usize) -> f64 {
    let segment = &values[start..end];
    let n = segment.len() as f64;
    let mean: f64 = segment.iter().sum::<f64>() / n;   // WRONG for L1
    segment.iter().map(|v| (v - mean).abs()).sum()
}
```
Fix direction: compute the median of `segment`, not the mean.

**PELT scale-invariant penalty (G7):**
```rust
// current: pen = ln(n) * 2.0 — no noise-scale term
// target: normalise cost by sigma_hat^2, sigma_hat = MAD(diff(values)) / 0.6745
```

**Scope decision to record explicitly in Plan 3 (per research §Critical Finding):** `anofox_ts_detect_changepoints` (PELT FFI) has zero C++ callers (`src/table_functions/ts_changepoints.cpp:29-35` documents its Phase-12 removal). Recommend Rust-only fix, proven via `changepoint_reference.rs` fixture tests against the numpy exact-DP — do not add a new SQL wrapper unless the plan explicitly decides otherwise.

---

### `crates/anofox-fcst-core/src/decomposition.rs::mstl_decompose` (Plan 4, D-09)

**Analog:** `crates/anofox-fcst-core/src/stats.rs::stl_strengths` (~lines 457-476) + `crates/anofox-fcst-core/src/stl.rs::stl()` — the exact R-faithful, ascending-order, 2-pass `forecast::mstl` iteration this function needs already exists, built for scalar strength output only.

**Core pattern to extract/share:**
```rust
// stats.rs::stl_strengths, ~462-473
usable.sort_unstable();   // ASCENDING period order — fixes G9 (current decomposition.rs sorts descending)
usable.dedup();
for _pass in 0..2 {
    for (pos, &period) in usable.iter().enumerate() {
        let s_window = 7 + 4 * (pos + 1);
        let params = StlParams::r_default(period, s_window);
        let fit = stl(&deseas, &params)?;
        seas[pos] = fit.seasonal;
        last_trend = fit.trend;
    }
}
```
Current `decomposition.rs::mstl_decompose` bug to replace (G9/G10/G11):
```rust
// decomposition.rs:71 — longest-first, WRONG
sorted_periods.sort_by(|a, b| b.cmp(a));
// decomposition.rs:243-245 — even window is period+1, not centred 2xm
let window = if period % 2 == 0 { period + 1 } else { period };
```
Factor the shared loop into one function both `stl_strengths` (returns scalars) and `mstl_decompose` (returns trend/seasonal/remainder arrays) call, satisfying D-09's "one shared implementation" literally. Classical (non-MSTL) even-period decomposition still needs the real R `stats::decompose` centred 2×m moving-average filter (half-weight at endpoints) — this is new code, no in-repo analog; write per R's documented filter directly.

---

### `crates/anofox-fcst-core/src/filter.rs::is_constant` (Plan 5, D-13)

**Analog:** itself, lines 39-48 — one-function mechanical tolerance fix.
```rust
// current (G25)
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();
    if non_null.len() < 2 { return true; }
    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}
```
Change the tolerance to `max - min <= 1e-10 * max(1.0, max|x|)` (scale-relative), same function signature and NaN/NULL filtering.

---

### Validation-reference generators (Plans 2-5)

**Analog:** `validation/reference/python/gen_tsfresh_features.py` (Phase 14) for all Python generators; `validation/reference/r/gen_stl_strength_reference.R` (Phase 13) for the R generator.

**Imports / provenance pattern (Python, copy verbatim shape):**
```python
"""<One-paragraph description of what this generates and why, citing the
CONTEXT.md decision ID>.

Writes one fixture per <case> under `validation/reference/fixtures/<family>/`.

Run via:
  uv run --locked --project validation/reference/python python \
    validation/reference/python/<this_file>.py
"""
from __future__ import annotations
import json
from pathlib import Path
from fixture_io import FIXTURES, generator_path, tool_versions, write_fixture
```
Use `fixture_io.write_fixture(...)` for strict-JSON output with `tool_versions(...)` provenance — never hand-roll `json.dump`. For the independent-implementation generators (BOCPD, PELT numpy-DP — no library call exists), the file still imports `fixture_io` the same way; only the computation body differs (hand-written formula instead of a library call), matching the existing `FORMULAS` dict pattern already used in `gen_tsfresh_features.py` for the `numpy/scipy`-sourced features.

**R generator header pattern (copy verbatim shape):**
```r
# <description, citing CONTEXT.md decision ID>.
#
# Writes validation/reference/fixtures/<family>/cases.json — ...
# consumed by crates/anofox-fcst-core/tests/<family>_reference.rs.
#
# Regenerate with:
#   Rscript validation/reference/r/gen_<family>_reference.R
# (works from any cwd — the script resolves its own directory via --file=).

.gen_script_dir <- local({
  args <- commandArgs(trailingOnly = FALSE)
  file_arg <- grep("^--file=", args, value = TRUE)
  normalizePath(dirname(sub("^--file=", "", file_arg[1])), mustWork = TRUE)
})
source(file.path(.gen_script_dir, "common.R"))
suppressMessages({ library(forecast) })
GENERATOR_PATH <- "validation/reference/r/gen_<family>_reference.R"
```

---

### Rust fixture tests (Plans 2-5)

**Analog:** `crates/anofox-fcst-core/tests/stl_reference.rs` (Phase 13).

**Imports + fixture-load pattern (copy verbatim):**
```rust
mod common;
use anofox_fcst_core::<module>::<function>;
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, num, tolerance};
use serde_json::Value;

#[test]
fn <case>_matches_<reference_tool>() {
    let fx = load_fixture("<family>/cases.json");
    let abs_tol = tolerance(&fx, "component_abs");
    // ... iterate cases, assert_close/assert_vec_close against fx fields
}
```
`common::load_fixture` panics with a "regenerate per validation/reference/README.md" message on missing/malformed fixtures — reuse this helper (`crates/anofox-fcst-core/tests/common/mod.rs:27-39`) rather than writing a new loader; it already resolves `fixtures_dir()` via `CARGO_MANIFEST_DIR`.

---

### `test/sql/ts_detection_reference.test` (all plans, D-15)

**Analog:** `test/sql/ts_features_reference.test` (Phase 14).

**Header + non-gating pattern (copy verbatim shape):**
```
# name: test/sql/ts_detection_reference.test
# description: <detection/decomposition family> value assertions against <fixture> (Phase 15 plan NN, DET-NN)
# group: [sql]
#
# Non-json-gated so it runs on every native build (Phase 13 lesson, D-12).
# Literals below are copied from validation/reference/fixtures/<family>/<case>.json via:
#   python3 -c "import json; d=json.load(open('validation/reference/fixtures/<family>/<case>.json')); print(d['expected'])"

require anofox_forecast
```
Assertion pattern — literal tolerance-bounded equality, not `require json`:
```sql
query I
SELECT abs(<col> - <literal_from_fixture>) <= 1e-9 + 1e-9 * abs(<literal_from_fixture>)
FROM ts_<function>_by(...);
----
true
```
**Do not** add `require json` gating — D-15 explicitly requires this file to be non-gated so the assertion count visibly rises in `make test_release`, unlike the existing json-gated `ts_changepoints`/`ts_classify_seasonality`/`ts_decomposition`/`ts_detrend`/`ts_peaks`/`ts_periods*`/`ts_seasonality` test files (those stay gated; ungating them is Phase 18/TEST-03, out of scope here).

## Shared Patterns

### FdMatrix construction (Plan 1, applies to detrending.rs, seasonality.rs, peaks.rs mechanical touch)
**Source:** fdars-core 0.47.0 `src/matrix.rs` (via RESEARCH.md §Code Examples)
```rust
let mat = fdars_core::matrix::FdMatrix::from_slice(values, 1, n)
    .map_err(|e| ForecastError::InvalidInput(e.to_string()))?;
```
**Apply to:** every call site migrated in Plan 1 (detrending.rs ×7, seasonality.rs ×2+5 already-correct, peaks.rs ×1 mechanical). Always `(values, 1, n)` for a single series — review this literal order at every new call site per RESEARCH.md's explicit warning that `from_slice(values, n, 1)` still compiles but is wrong.

### Error construction (`ForecastError::InvalidParameter` / `InvalidInput`)
**Source:** `crates/anofox-fcst-core/src/error.rs` (project convention, CLAUDE.md "Error Handling")
**Apply to:** D-02 method-name validation, D-11 Lomb-Scargle input validation, any new parameter rejection in Plans 1/5 — never a silent fallback or a bare `panic!`.

### Fixture provenance (`fixture_io.py` / R `common.R`)
**Source:** `validation/reference/python/fixture_io.py`, `validation/reference/r/common.R`
**Apply to:** all 4 new reference generators (Plans 2-5) — strict JSON, `tool_versions(...)` provenance, no timestamps/hostnames (byte-identical regeneration).

### Non-json-gated SQL reference test
**Source:** `test/sql/ts_features_reference.test` (Phase 14, "Phase 13 lesson, D-12")
**Apply to:** `test/sql/ts_detection_reference.test` (all plans contribute sections to this one file per D-15) — literal values copied from fixture JSON via a one-line `python3 -c` snippet in the file's own header comment.

### Rust fixture-test loader
**Source:** `crates/anofox-fcst-core/tests/common/mod.rs`
**Apply to:** `peaks_reference.rs`, `changepoint_reference.rs`, `mstl_decompose_reference.rs`, `period_detection_reference.rs` — reuse `load_fixture`, `assert_close`, `assert_vec_close`, `tolerance`, `num`, `f64_vec` rather than writing new helpers.

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `crates/anofox-fcst-core/src/peaks.rs` (scipy `find_peaks` rewrite body, Plan 2) | utility | transform | No existing scipy-algorithm port in the codebase; must be written from the documented scipy spec (plateau midpoint, prominence `wlen`, distance greedy removal) |
| `validation/reference/python/gen_bocpd_pelt_reference.py` (independent numpy BOCPD + exact-DP, Plan 3) | utility (fixture generator) | file-I/O | No prior "independent reference implementation of an algorithm, not a library call" generator exists; closest is `gen_tsfresh_features.py`'s `FORMULAS` dict for the handful of `numpy/scipy`-sourced features, which is a partial precedent only |
| `decomposition.rs`'s classical (non-MSTL) even-period centred 2×m filter (D-10, Plan 4) | utility | transform | No R `stats::decompose`-faithful centred moving-average filter exists in-repo yet; must be written fresh per R's documented half-weight-endpoint algorithm |

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/` (detrending, seasonality, periods, peaks, changepoint, decomposition, filter, imputation, stl, stats), `crates/anofox-fcst-core/tests/`, `validation/reference/{python,r}/`, `test/sql/`
**Files scanned:** ~20 (all enumerated in RESEARCH.md's "Sources: Primary" list, read directly this session per research, cross-checked here)
**Pattern extraction date:** 2026-10-12

## PATTERN MAPPING COMPLETE

