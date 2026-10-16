# Phase 14: tsfresh Feature Parity - Pattern Map

**Mapped:** 2026-10-11
**Files analyzed:** 9
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `crates/anofox-fcst-core/src/features.rs` (modified in place) | utility/transform | batch (pure function over `&[f64]`) | same file, pre-existing functions (`autocorr`, `partial_autocorr`, `time_reversal_asymmetry`, skewness/kurtosis block, `aggregated_linear_trend`) | exact (self-modification) |
| `crates/anofox-fcst-core/Cargo.toml` (add `rustfft` direct dep) | config | — | same file's existing `[dependencies]` block | exact |
| `validation/reference/python/gen_tsfresh_features.py` | utility (fixture generator) | batch/file-I/O | `validation/reference/python/gen_quality_reference.py` | exact |
| `validation/reference/fixtures/tsfresh_features/mapping.json` | config (data) | file-I/O | no direct prior mapping.json analog; model schema on `fixture_io.write_fixture`'s payload+provenance convention | role-match |
| `validation/reference/fixtures/tsfresh_features/{6 series}.json` + constant edge case | config (data) | file-I/O | `validation/reference/fixtures/moments/scipy_moments.json` (via `gen_quality_reference.py`'s `write_fixture` calls) | exact |
| `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` | test | batch/request-response (assert) | `crates/anofox-fcst-core/tests/stats_reference.rs` | exact |
| `test/sql/ts_features_reference.test` | test | request-response | `test/sql/ts_stats_reference.test` | exact |
| `docs/api/21-feature-reference.md` (add tsfresh-equivalent column) | docs | — | existing file, table-editing only (not separately analyzed — doc-table edit, no code pattern needed) | n/a |
| `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md` (update stale notes) | docs | — | existing file, text edit only | n/a |

## Pattern Assignments

### `crates/anofox-fcst-core/src/features.rs` (utility, transform) — self-modification, no analog file needed

This phase's core work is in-place correction of existing functions in this single file. The "analog" is each function's own current body (quoted in RESEARCH.md with exact replacement formulas). Key line anchors read this session:

**Skewness/kurtosis block** (lines ~66-83): currently population (biased) moments —
```rust
let skewness = values.iter().map(|v| ((v - mean) / std_dev).powi(3)).sum::<f64>() / n;
...
let kurtosis = values.iter().map(|v| ((v - mean) / std_dev).powi(4)).sum::<f64>() / n - 3.0;
```
Per D-05/FEAT-02, replace with the bias-corrected G1/G2 estimator. Check first whether Phase 13's `crate::stats` module already exposes a `pub` bias-corrected skew/kurtosis function (RESEARCH.md Assumption A3) — if so, call that instead of re-deriving.

**`mean_second_derivative_central`** (lines ~146-153):
```rust
let second_deriv: f64 = values.windows(3).map(|w| w[2] - 2.0*w[1] + w[0]).sum::<f64>() / (values.len() - 2) as f64;
```
Fix: divide the final result by an additional `2.0` (tsfresh's closed form divides by `2*(n-2)`).

**`partial_autocorr`** (lines ~506-520): ad-hoc Yule-Walker shortcut, wrong beyond lag 2 —
```rust
fn partial_autocorr(values: &[f64], lag: usize) -> f64 {
    if lag == 1 { return autocorr(values, 1); }
    let acf1 = autocorr(values, 1);
    let acf2 = autocorr(values, 2);
    if (1.0 - acf1.powi(2)).abs() < f64::EPSILON { return 0.0; }
    (acf2 - acf1.powi(2)) / (1.0 - acf1.powi(2))
}
```
Replace entirely with the Durbin-Levinson recursion in RESEARCH.md's "Code Examples" section (adjusted autocovariance `/(n-k)`, recursion for k=1..5).

**`first_last_location_of_value`** (lines ~626-644): raw 0-based index returned for both first and last —
```rust
fn first_last_location_of_value(values: &[f64], target: f64) -> (f64, f64) {
    ...
    if !found_first { first = i as f64; found_first = true; }
    last = i as f64;
    ...
}
```
Call-site division by `n` (lines ~136,138) must become `(last_idx + 1) / n` for the "last" result only; "first" stays `idx / n` (already correct per RESEARCH.md).

**`time_reversal_asymmetry`** (lines ~895-911):
```rust
sum += x_lead.powi(2) * x - x_lag * x.powi(2);
```
One-line fix: swap operand order on the second term → `sum += x_lead.powi(2) * x - x * x_lag.powi(2);`

**`aggregated_linear_trend`** (lines ~1049-1080, `r_squared.sqrt()` usage): always non-negative rvalue —
```rust
(slope, intercept, r_squared.sqrt(), stderr)
```
Fix: compute signed Pearson r directly (`ss_xy.signum() * (ss_xy.powi(2)/(ss_xx*ss_yy)).sqrt()` or `ss_xy/(ss_xx.sqrt()*ss_yy.sqrt())`) rather than taking the sqrt of r².

**`autocorr`** (denominator at the function's `num/denom` return, just above `partial_autocorr` — read lines ~481-504 for full context): denominator is whole-series `Σ(xᵢ-μ)²` instead of `(n-lag)·var(x)`. Fix: multiply current denominator by `(n-lag)/n`.

**New functions to add** (no existing analog in this file — build from RESEARCH.md's quoted tsfresh formulas directly): `sample_entropy` rewrite (full symmetric matrix, `n-m+1`/`n-m` windows, no triangular/combinatorial divisor), `binned_entropy` rewrite (floor-based `np.histogram` binning), `benford_correlation` fix (include `|v|<1`, denominator = full count incl. zero-digit), `permutation_entropy` (drop `ln(order!)` normalization), `lempel_ziv_complexity` full rewrite (min/max quantization + sub-word growth, per RESEARCH.md's Python-to-Rust port), FFT-based `fft_coefficient`/`spectral_centroid`/`spectral_variance` via `rustfft`.

### `crates/anofox-fcst-core/Cargo.toml` — add direct `rustfft` dependency

**Analog:** same file's existing `[dependencies]` block (read above):
```toml
[dependencies]
anofox-forecast = { workspace = true }
anofox-regression = { workspace = true }
faer = { workspace = true }
thiserror = { workspace = true }
chrono = { workspace = true }
statrs = { workspace = true }
serde_json = "1"
```
Add, per RESEARCH.md D-07/Installation: `rustfft = { version = "6.4", default-features = false }` — pin to the exact locked version (`6.4.1`) to avoid duplicate-crate resolution with `fdars-core`'s transitive pull.

### `validation/reference/python/gen_tsfresh_features.py` (utility, fixture generator)

**Analog:** `validation/reference/python/gen_quality_reference.py`

**Imports pattern** (lines 1-26):
```python
from __future__ import annotations
import math
from typing import Optional
import numpy as np
import pandas as pd
from scipy import stats
from fixture_io import generator_path, tool_versions, write_fixture
TOLERANCE = {"score_abs": 1e-12}
```
For this phase: `import tsfresh.feature_extraction.feature_calculators as fc` plus the same `fixture_io` import; `TOLERANCE = {"abs": 1e-9, "rel": 1e-9}` per D-10, with per-feature overrides read from `mapping.json`.

**Core generation pattern** — build each case as a dict, then call `write_fixture`:
```python
write_fixture(
    family="tsfresh_features", filename="white_noise_n200.json",
    generator=generator_path(__file__),
    versions=tool_versions("numpy", "scipy", "tsfresh"),
    payload={"tolerance": TOLERANCE, "series": {...}, "expected": {...}},
)
```
Critical deviation from the analog (per RESEARCH.md's "Fixture Generation Methodology" and Pitfall 2): do **not** import `ComprehensiveFCParameters`/call `tsfresh.extract_features()`; call `fc.<name>(x, ...)` directly per the mapping file's pinned params, and fall back to a hand-written numpy/scipy formula for the ~8 features with no tsfresh counterpart (D-02), each tagged `"reference": "numpy/scipy"`.

### `validation/reference/fixtures/tsfresh_features/mapping.json` (config/data)

No direct prior "mapping.json" exists in the repo; model its shape on `write_fixture`'s own provenance contract (`family`, `generator`, `tool_versions`, `tolerance` keys enforced by `fixture_provenance_contract` in `crates/anofox-fcst-core/tests/reference_fixtures.rs` — read that test if present before finalizing schema) plus a `features: [{name, tsfresh_calculator|reference, params, tolerance_override?, notes}]` array per D-01. 117 rows required.

### `validation/reference/fixtures/tsfresh_features/*.json` (6 series + constant)

**Analog:** `validation/reference/fixtures/moments/scipy_moments.json` (structure referenced via `gen_quality_reference.py`'s `write_fixture` calls and consumed by `stats_reference.rs`'s `fx["vectors"][name]["x"]` access pattern). Each fixture file: `{family, generator, tool_versions, tolerance, series: {values: [...]}, expected: {feature_name: value, ...}}` as laid out in RESEARCH.md's Pattern 1.

### `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` (test)

**Analog:** `crates/anofox-fcst-core/tests/stats_reference.rs` (full pattern read above)

**Imports pattern** (lines 1-19):
```rust
mod common;
use anofox_fcst_core::calendar::CalendarStep;
use anofox_fcst_core::gaps::gap_counts;
use anofox_fcst_core::{compute_ts_stats, compute_ts_stats_with_dates_and_type, FrequencyType};
use chrono::NaiveDateTime;
use common::{f64_vec, load_fixture, num, tolerance};
```
For Phase 14: `mod common; use common::{load_fixture, f64_vec, num, tolerance, assert_close}; use anofox_fcst_core::extract_features;`

**Core per-fixture test pattern** (lines 20-45, loop-over-vectors-then-literal-pin structure):
```rust
#[test]
fn moments_fixture_matches_compute_ts_stats() {
    let fx = load_fixture("moments/scipy_moments.json");
    let abs_tol = tolerance(&fx, "abs");
    let rel_tol = tolerance(&fx, "rel");
    ...
    for (name, v) in vectors {
        let x = f64_vec(&v["x"]);
        let stats = compute_ts_stats(&series, 0).unwrap_or_else(|e| panic!(...));
        common::assert_close(stats.skewness, want_skew, product_tol.max(abs_tol), rel_tol, &format!("{name}.skewness"));
    }
    // Pin literal ROADMAP values directly as a secondary, hard-coded assertion
    common::assert_close(num(&x20["kurtosis_excess_bias_false"]), 0.2765286546322532, 1e-10, 0.0, "x20.kurtosis_excess_bias_false (ROADMAP SC1 literal)");
}
```
For Phase 14: one test per series fixture (6 + constant), each loading `fx["expected"]` as a JSON object and iterating `extract_features(&values)` results against it (per RESEARCH.md Pattern 1's exact snippet); additionally pin the literal `sample_entropy ≈ 1.099` anchor named in CONTEXT.md's Specific Ideas, exactly as `stats_reference.rs` pins `x20`/`neg30` literals.

### `test/sql/ts_features_reference.test` (test, request-response)

**Analog:** `test/sql/ts_stats_reference.test` (full header + table-build pattern read above)

**Header pattern** (lines 1-8):
```
# name: test/sql/ts_stats_reference.test
# description: ts_stats moments/entropy/gap-count value assertions against reference fixtures (Phase 13 plan 13-01, STAT-01/STAT-03)
# group: [sql]
#
# Non-json-gated so it runs on every native build. Literals below are copied from
# validation/reference/fixtures/moments/scipy_moments.json (vectors x20, neg30) via:
#   python3 -c "import json; d=json.load(open(...)); print(d['vectors']['x20'])"

require anofox_forecast
```
**Table + assertion pattern** (lines 9-80):
```sql
statement ok
CREATE TABLE x20_tbl AS SELECT * FROM (VALUES
    (DATE '2024-01-01' + INTERVAL (0) DAY, 3.337),
    ...
) t(ds, v);

statement ok
CREATE TABLE x20_grp AS SELECT 'x20' AS id, ds, v FROM x20_tbl;

query I
SELECT abs(skewness - 0.30292879825084273) < 1e-10 AND abs(kurtosis - 0.2765286546322532) < 1e-10 AND abs(bimodality_coef - 0.2861213328957609) < 1e-10
```
For Phase 14: build one `VALUES` table per fixture series used (literal copy, per D-12 "~15 sampled features"), then `query I` rows calling `ts_features_by(...)`/`ts_features_agg(...)` and asserting a boolean `abs(col - literal) < tol` expression per sampled feature, same style. **Critical: no `require json`** — this must run on every native build (Phase 13 lesson, D-12).

## Shared Patterns

### Fixture provenance + loading
**Source:** `validation/reference/python/fixture_io.py` (`write_fixture`, `generator_path`, `tool_versions`, `encode`) and `crates/anofox-fcst-core/tests/common/mod.rs` (`load_fixture`, `num`, `f64_vec`, `tolerance`, `assert_close`)
**Apply to:** `gen_tsfresh_features.py` (writer side) and `tsfresh_features_reference.rs` (reader side) — reuse verbatim, no new helpers needed.

### Tolerance-from-fixture, never hard-coded
**Source:** `crates/anofox-fcst-core/tests/common/mod.rs::tolerance()` — "Tests must take tolerances from the fixture, never hard-code them."
**Apply to:** `tsfresh_features_reference.rs` default-tolerance reads; per-feature overrides (D-10) must live in `mapping.json`/fixture JSON, not in Rust literals (except the explicit literal-pin assertions for ROADMAP anchor values, which mirror `stats_reference.rs`'s x20/neg30 pattern).

### Non-json-gated SQL correctness test
**Source:** `test/sql/ts_stats_reference.test` header comment — "Non-json-gated so it runs on every native build."
**Apply to:** `test/sql/ts_features_reference.test` — must not include `require json`.

### Breaking-change documentation in CHANGELOG
**Source:** `CHANGELOG.md` `## [Unreleased] — v0.10.0 Statistical Correctness` → `### Breaking changes` bullet list style (each bullet: bolded one-line summary, then explanation of old vs new behavior and affected surfaces).
**Apply to:** Phase 14's CHANGELOG entry for all 117-feature value changes (D-04) — one bullet per changed feature group (PACF, mean_second_derivative_central, last_location_of_*, time_reversal_asymmetry, agg_linear_trend_rvalue, skewness/kurtosis, fft_coefficient/spectral_*, sample_entropy, binned_entropy, benford_correlation, permutation_entropy, lempel_ziv_complexity, autocorrelation_lag*), following the existing bolded-summary-then-detail format exactly.

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `validation/reference/fixtures/tsfresh_features/mapping.json` | config (data) | file-I/O | No prior per-feature calculator-mapping JSON file exists in the repo; schema must be designed fresh per D-01, informed by `fixture_io.write_fixture`'s provenance-key convention (role-match only, not exact) |
| New Rust functions with no prior Rust implementation at all (FFT-based `fft_coefficient`/`spectral_centroid`/`spectral_variance`, rewritten `lempel_ziv_complexity`) | utility | transform | The *names* exist today in `features.rs` but the target *algorithm* is a full replacement, not a same-shape edit; RESEARCH.md's quoted tsfresh Python source and the "Code Examples" Rust pseudocode are the only available port target — there is no closer in-repo analog than the function's own (wrong) current body |

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/`, `crates/anofox-fcst-core/tests/`, `validation/reference/python/`, `validation/reference/fixtures/`, `test/sql/`, `CHANGELOG.md`
**Files scanned:** `features.rs` (full read, targeted ranges), `fixture_io.py`, `gen_quality_reference.py` (head), `common/mod.rs`, `stats_reference.rs` (partial), `ts_stats_reference.test` (partial), `Cargo.toml`, `CHANGELOG.md` (head)
**Pattern extraction date:** 2026-10-11

## PATTERN MAPPING COMPLETE

**Phase:** 14 - tsfresh Feature Parity
**Files classified:** 9
**Analogs found:** 9 / 9 (7 exact/role-match with concrete line-level excerpts, 2 "no closer analog than the file's own current body / needs fresh schema design")

### Coverage
- Files with exact analog: 7 (`features.rs` self-edits, `Cargo.toml`, `gen_tsfresh_features.py`, 6-series fixtures, `tsfresh_features_reference.rs`, `ts_features_reference.test`, CHANGELOG pattern)
- Files with role-match analog: 1 (`mapping.json` — no prior mapping file, schema informed by `fixture_io` provenance convention)
- Files with no analog: 1 category (brand-new algorithm ports: FFT, Lempel-Ziv — RESEARCH.md's quoted source is the port target, not an in-repo file)

### Key Patterns Identified
- Every statistical-correctness fix in this phase is an in-place edit to `crates/anofox-fcst-core/src/features.rs`, following each function's own current body as the "before" and RESEARCH.md's verbatim tsfresh-source quotes as the "after" — there is no cross-file refactor.
- The fixture/test harness is a direct structural copy of Phase 13's `gen_quality_reference.py` → `write_fixture()` → `stats_reference.rs`/`common::load_fixture()` → non-json-gated `ts_stats_reference.test` pipeline; zero new infrastructure code needed, only new fixture content and a new mapping.json.
- Tolerances must always come from the fixture JSON (`common::tolerance()`), never hard-coded in Rust, except for the explicit ROADMAP-anchor literal-pin assertions that mirror `stats_reference.rs`'s x20/neg30 style.

### File Created
`/home/simonm/projects/duckdb/anofox-forecast/.planning/phases/14-tsfresh-feature-parity/14-PATTERNS.md`

### Ready for Planning
Pattern mapping complete. Planner can now reference analog patterns in PLAN.md files.
