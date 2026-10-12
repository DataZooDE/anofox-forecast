# Phase 16: Conformal & Bootstrap Correctness - Pattern Map

**Mapped:** 2026-10-12
**Files analyzed:** 10 (new + modified)
**Analogs found:** 10 / 10

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/macros/ts_macros.cpp` (4 conformal macro bodies rewritten) | macro / SQL template | request-response (CRUD-shaped GROUP BY aggregation) | same file, `ts_cv_hydrate_by` / `ts_cv_folds_by` bodies (join-ordered, step-column-aware patterns) | exact (same file, adjacent macros already do step-ordered GROUP BY joins) |
| `src/scalar_functions/conformal.cpp` (error.code inspection, difficulty params, strategy removal) | scalar function (C++/FFI marshal) | request-response | `anofox_ts_conformal_predict_per_step` call site in same file (line ~1184) — already does `error.code == INVALID_INPUT` → throw | exact (same file, one function already implements D-06's target pattern) |
| `src/scalar_functions/bootstrap.cpp` | scalar function (C++/FFI marshal) | request-response | same file, no change needed (Rust-only fix); analog is itself for structure confirmation | exact (no-op change, structure unchanged) |
| `crates/anofox-fcst-core/src/conformal.rs` (quantile clamp, asymmetric target, adaptive calibration, JackknifePlus removal) | service / core statistical logic | transform (batch) | same file — fix in place; `compute_quantile` (already-correct, reused) | exact (in-place fix, no new file) |
| `crates/anofox-fcst-core/src/bootstrap.rs` (sign swap) | service / core statistical logic | transform (batch) | same file — one-line fix at two call sites | exact |
| `crates/anofox-fcst-core/tests/conformal_reference.rs` (NEW) | test | fixture-driven assertion / transform | `crates/anofox-fcst-core/tests/metrics_reference.rs` (closest: fixture-case iteration + R/Rust cross-check) and `tests/stl_reference.rs` for Monte Carlo seeding style | role-match, exact structurally |
| `crates/anofox-fcst-core/tests/common/mod.rs` (reused, no change) | utility (test fixture loader) | file-I/O | itself (`load_fixture`, `num`, `f64_vec`, `tolerance` already generic) | exact — no change needed |
| `validation/reference/python/gen_conformal_reference.py` (NEW) | utility (reference generator) | batch / file-I/O | `validation/reference/python/gen_moments.py` (scipy-based, `fixture_io` helpers, tolerance dict, `describe()` pattern) | exact |
| `test/sql/ts_conformal_reference.test` (NEW) | test (SQL, non-json-gated) | request-response | `test/sql/ts_metrics_reference.test` (non-json-gated `.test`, literal values pasted from a `validation/reference/fixtures/*.json` file, provenance comment header) | exact |
| `docs/api/11-conformal-prediction.md`, `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md`, `CHANGELOG.md` | docs / config | N/A | existing doc/skill content for conformal — edit in place | exact (edit-in-place, no analog search needed) |

## Pattern Assignments

### `src/macros/ts_macros.cpp` (macro, request-response)

**Analog:** same file, adjacent macros' GROUP BY / `ORDER BY` discipline, plus research's own illustrative Pattern 1.

**Current buggy pattern to replace** (`ts_conformal_by`, lines 1720-1762):
```sql
residuals AS (
    SELECT group_col AS _grp, (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
    WHERE actual_col IS NOT NULL AND forecast_col IS NOT NULL
),
point_forecasts AS (
    SELECT group_col AS _grp,
        LIST(point_forecast_col::DOUBLE ORDER BY point_forecast_col) AS _forecasts   -- BUG: value-ordered
    FROM query_table(backtest_results::VARCHAR)
    WHERE point_forecast_col IS NOT NULL
    GROUP BY group_col
),
```

**`ts_conformal_apply_by`** (lines 1810-1831) has the same value-order bug:
```sql
ts_conformal_intervals(
    LIST(forecast_col::DOUBLE ORDER BY forecast_col),   -- BUG
    conformity_score::DOUBLE
) AS interval_result
```

**`ts_interval_width_by`** (lines 1836-1851) has the worst variant — independently sorted lower/upper, breaking row pairing entirely:
```sql
ts_mean_interval_width(
    LIST(lower_col::DOUBLE ORDER BY lower_col),   -- BUG: sorted independently from upper
    LIST(upper_col::DOUBLE ORDER BY upper_col)
)
```

**Fix pattern — thread a required step/date column through every `LIST()` in all four macros, same `ORDER BY <step_col>` on every paired array:**
```sql
-- Model: same style already used by this file's other _by macros that join
-- on (group, date) — see ts_cv_hydrate_by's JOIN ON cv.__cv_grp = src.__src_grp
-- AND cv.__cv_dt = src.__src_dt immediately above ts_conformal_by in this file.
WITH residuals AS (
    SELECT group_col AS _grp, step_col AS _step,
           (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
),
per_group AS (
    SELECT _grp,
        LIST(residual ORDER BY _step) AS _residuals,
        LIST(point_forecast_col ORDER BY _step) AS _forecasts
    FROM residuals
    GROUP BY _grp
)
```
Apply identically to `ts_conformal_apply_by` (order `forecast_col` by the new step param) and `ts_interval_width_by` (order BOTH `lower_col` and `upper_col` by the SAME step param in the SAME `LIST()`'s implicit row order — i.e. one `GROUP BY` producing both lists from one ordered source, not two independently-ordered `LIST()` calls).

**`ts_conformal_calibrate`** (lines 1783-1803) — add required `group_col` param (D-03), default to `GROUP BY group_col`, opt out via `params := {'pooled': true}`:
```sql
-- current (no group_col at all, pools everything):
{"ts_conformal_calibrate", {"backtest_results", "actual_col", "forecast_col", "params", nullptr}, ...}
-- replace coverage computation (currently nominal, not empirical — D-02):
1.0 - (SELECT _alpha FROM _params) AS coverage,   -- BUG: nominal
-- with a call into the already-existing Rust-backed ts_conformal_coverage/evaluate
-- after applying, per research's "Don't Hand-Roll" table.
```

**Output columns to add across all four** (per D-01/D-02/D-06): `nominal_level`, `n_calibration`, and (for the two prediction-producing macros) `calibration_insufficient`.

---

### `src/scalar_functions/conformal.cpp` (scalar function, request-response)

**Analog — the one function in this file that already does the target error-handling pattern (D-06):**
```cpp
// Source: src/scalar_functions/conformal.cpp:~1180-1188 (anofox_ts_conformal_predict_per_step call site)
if (!success) {
    if (error.code == INVALID_INPUT) {
        throw InvalidInputException("ts_conformal_predict_per_step: %s", error.message);
    }
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```
**Current anti-pattern to fix at the other ~11 `SetNull`-only sites** (e.g. `TsConformalQuantileFunction`, lines 32/49; and the sites listed in the audit at 251-254, 373-376, 544-547, 710-713, 842-845, 952-955):
```cpp
// Source: src/scalar_functions/conformal.cpp:44-51 (current, buggy — discards error.code)
if (!success) {
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```
Apply the per-step function's `if (error.code == INVALID_INPUT) throw InvalidInputException(...)` guard to every conformal scalar function that takes a user-supplied `alpha` or `strategy` string, before the fallback `SetNull`.

**Strategy-string parsing to make strict (D-09):**
```cpp
// src/scalar_functions/conformal.cpp:464-473, ParseConformalStrategy — currently
// silently returns SPLIT for any unrecognized string including jackknife+ aliases
// and typos. Must raise/throw for the 4 jackknife+ aliases specifically (and
// arguably any truly unrecognized string, per the audit's independent note),
// not silently default.
```

**Difficulty-column threading (D-08):** `ListDoubleReader` is the established pattern for every list-typed argument in this file (used throughout, e.g. `residuals_reader`/`forecasts_reader` above in `bootstrap.cpp`'s analog below). Add a new `ListDoubleReader difficulty_cal_reader(...)` / `difficulty_pred_reader(...)` pair the same way existing readers are constructed, and pass `.data()`/validity through to the FFI call exactly as `residuals_reader` is passed today.

---

### `src/scalar_functions/bootstrap.cpp` (scalar function, request-response)

**No C++ changes needed** — confirmed by reading `TsBootstrapIntervalsFunction` (lines 1-40+): it only marshals `ListDoubleReader`/`ScalarArgReader` into the FFI call and unpacks a 4-field struct result. The entire bootstrap sign fix (D-10) is isolated to the Rust layer; this file's structure is the "no change" analog — confirms no `fit()` call or sign logic lives in C++.

---

### `crates/anofox-fcst-core/src/conformal.rs` (service, transform)

**Fix in place** — four bugs, each with an exact line-anchored fix per RESEARCH.md's Code Examples section (already fully detailed there: clamp→sentinel at 133-139 + duplicates at 773-774/788-789/821-822/830-831/844-845; asymmetric signed-quantile rewrite at 380-410/796-836; adaptive calibration-time difficulty new param on `conformal_predict_adaptive` (currently 301-306/315-349) plus `conformal_apply`'s batch-mean removal at 932-938; JackknifePlus hard-error in `FromStr` at 518-528, removing the `conformal_learn`/`conformal_apply` duplicate-formula branches at 765-779/941-983).

**Reuse, do not reimplement:** `compute_quantile` (lines 429-448) is already correct (type-7-like interpolation) — only the quantile *level* fed into it changes in 3 of 4 methods. `conformal_coverage`/`conformal_evaluate` already exist and are correct (empirical coverage, Winkler score) — the SQL macros should call these, not recompute `1 - alpha`.

---

### `crates/anofox-fcst-core/src/bootstrap.rs` (service, transform)

**Analog:** itself, one-line argument-order fix at two call sites (`bootstrap_intervals` lines 74-75, `bootstrap_quantiles` lines 135-136):
```rust
// Source: crates/anofox-fcst-core/src/bootstrap.rs:73-76 (current, buggy)
let zeros: Vec<f64> = vec![0.0; residuals.len()];
let result = predictor.fit(&zeros, residuals).map_err(...)?;   // BUG: internal residual = -residuals[i]

// Fix:
let result = predictor.fit(residuals, &zeros).map_err(...)?;
```

---

### `crates/anofox-fcst-core/tests/conformal_reference.rs` (NEW, test)

**Analog:** `crates/anofox-fcst-core/tests/metrics_reference.rs` for the fixture-loading/case-iteration skeleton, plus the seeded-RNG style already established in `bootstrap.rs`'s own `#[test] fn test_bootstrap_intervals_basic` (`Some(42)` seed).

**Imports pattern** (copy from `metrics_reference.rs` lines 1-18):
```rust
mod common;

use anofox_fcst_core::conformal::{conformal_quantile, conformal_predict_asymmetric, /* ... */};
use common::{f64_vec, load_fixture, tolerance};
use serde_json::Value;
```

**Core pattern** (copy case-iteration + assertion shape from `metrics_reference.rs`'s `r_mase_fixture_matches_mase`-style test):
```rust
#[test]
fn conformal_reference_split_quantile_matches_numpy() {
    let fx = load_fixture("conformal/cases.json");
    let abs_tol = tolerance(&fx, "quantile_abs");
    let cases = fx["cases"].as_object().expect("'cases' is not an object");
    for (name, case) in cases {
        // ... compute via conformal_quantile, compare to case["expected"], report `name` on failure
    }
}
```

**Monte Carlo coverage test (new structural element, D-07/D-13 requirement)** — no existing exact analog; model on `bootstrap.rs`'s seeded test plus a plain loop over simulated draws, asserting `(empirical_coverage - nominal).abs() <= 0.02`.

**Fixture loader — reuse unchanged:** `common::load_fixture`, `common::num`, `common::f64_vec`, `common::tolerance` in `crates/anofox-fcst-core/tests/common/mod.rs` (lines 1-60+) are already fully generic; no modification needed, just `mod common;` + `use common::...` as every other `*_reference.rs` file does.

---

### `validation/reference/python/gen_conformal_reference.py` (NEW)

**Analog:** `validation/reference/python/gen_moments.py` — scipy-based generator using shared `fixture_io` helpers.

**Imports pattern** (copy lines 1-18 of `gen_moments.py`):
```python
from __future__ import annotations

import numpy as np
from scipy import stats

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {"abs": 1e-12, "rel": 1e-12}
```

**Core pattern** — a `describe()`-shaped function per method (split/asymmetric/adaptive quantile, bootstrap sign), returning a dict written via `write_fixture` (same as `gen_moments.py`'s `describe()` at lines 29-45), plus a seeded Monte Carlo coverage simulation block (new, following the same numpy-only, no-timestamp/no-hostname determinism rule stated in `fixture_io.py`'s module docstring, lines 1-6).

**Provenance helper reuse (no modification needed):** `fixture_io.generator_path`, `fixture_io.tool_versions`, `fixture_io.encode` (lines 24-60 of `fixture_io.py`) already handle numpy-type and non-finite-float JSON encoding — reuse directly, do not reimplement NaN/Inf string encoding.

---

### `test/sql/ts_conformal_reference.test` (NEW, non-json-gated SQL test)

**Analog:** `test/sql/ts_metrics_reference.test` — non-json-gated `.test` file with literal values pasted from a fixture JSON, documented provenance comment.

**Header pattern** (copy structure from lines 1-11 of `ts_metrics_reference.test`):
```
# name: test/sql/ts_conformal_reference.test
# description: Conformal/bootstrap correctness assertions against independent
#   numpy references (Phase 16, RUST-04/CONF-01..05). Non-json-gated so it runs
#   on every native build. Literals below are copied from
#   validation/reference/fixtures/conformal/cases.json via:
#   python3 -c "import json; d=json.load(open('validation/reference/fixtures/conformal/cases.json')); print(d['cases']['<name>'])"
# group: [sql]

require anofox_forecast
```

**Assertion pattern** (copy the `query I` / `ABS(... - expected) < tol` / `----` / `true` shape used throughout `ts_metrics_reference.test`, e.g. lines 13-22).

## Shared Patterns

### Error-code-gated throw vs. silent NULL (D-06)
**Source:** `src/scalar_functions/conformal.cpp` (the `anofox_ts_conformal_predict_per_step` call site, ~line 1184) and `src/table_functions/ts_forecast_native.cpp:733-734` (project-wide established pattern).
**Apply to:** Every conformal scalar function in `conformal.cpp` taking `alpha` or a `strategy`/`method` string.
```cpp
if (!success) {
    if (error.code == INVALID_INPUT) {
        throw InvalidInputException(string(error.message));
    }
    FlatVector::SetNull(result, row_idx, true);   // keep NULL for non-error cases (e.g. D-05 insufficiency)
    continue;
}
```

### Non-json-gated reference test + fixture provenance (TEST-01 harness)
**Source:** `validation/reference/python/fixture_io.py` + `test/sql/ts_metrics_reference.test` + `crates/anofox-fcst-core/tests/common/mod.rs`.
**Apply to:** `gen_conformal_reference.py`, `conformal_reference.rs`, `ts_conformal_reference.test` — all three must follow the existing three-way contract: Python generator writes a fixture JSON with `tool_versions`/`generator_path` provenance and an explicit tolerance dict; Rust test loads it via `common::load_fixture` and asserts via `common::num`/`tolerance`; SQL test hardcodes literals copied from the same JSON with a `python3 -c "..."` extraction comment for traceability.

### Step-ordered pairing via GROUP BY + single ORDER BY (D-01, D-05)
**Source:** `src/macros/ts_macros.cpp`'s `ts_cv_hydrate_by`/adjacent macros (join-based grouping already in the file) and the research's own Pattern 1/Pattern 2 (per-step conformal as the "Rust-validated pairing, not SQL-assumed pairing" reference, `anofox_ts_conformal_predict_per_step` FFI signature validating `len % horizon == 0` in Rust).
**Apply to:** All four conformal/bootstrap macros — every `LIST()` that must stay index-aligned with another `LIST()` must share the exact same `ORDER BY <step_col>` clause, and the new step-column parameter is required (not optional) on all four macro signatures.

## No Analog Found

None — all 10 files/changes have a concrete existing analog in the codebase (several are in-place fixes to the same file, which is itself the "analog" for structure-preservation purposes).

## Metadata

**Analog search scope:** `src/macros/`, `src/scalar_functions/`, `crates/anofox-fcst-core/src/`, `crates/anofox-fcst-core/tests/`, `validation/reference/python/`, `test/sql/`
**Files scanned:** ~25 (targeted reads/greps; no full-repo scan needed given RESEARCH.md's exhaustive line-level audit)
**Pattern extraction date:** 2026-10-12

## PATTERN MAPPING COMPLETE
