---
phase: 02-global-panel-models
reviewed: 2026-08-21T00:00:00Z
depth: standard
files_reviewed: 21
files_reviewed_list:
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/Cargo.toml
  - crates/anofox-fcst-ffi/cbindgen.toml
  - src/table_functions/ts_forecast_panel_native.cpp
  - src/include/ts_forecast_panel_native.hpp
  - src/include/anofox_fcst_ffi.h
  - src/anofox_forecast_extension.cpp
  - src/macros/ts_macros.cpp
  - CMakeLists.txt
  - examples/forecasting/global_panel_forecasting_examples.sql
  - benchmark/configs/global_ets.py
  - benchmark/configs/statsforecast_global.py
  - benchmark/m4/global_benchmark/run.py
  - benchmark/src/common/anofox_runner.py
  - benchmark/src/common/benchmark_runner.py
  - docs/api/07-forecasting.md
  - docs/reference/models/exponential-smoothing/global_ets.md
  - docs/reference/models/theta/global_theta.md
  - docs/reference/models/intermittent/global_croston.md
  - .claude/skills/anofox-forecast-models/SKILL.md
findings:
  critical: 3
  warning: 4
  info: 2
  total: 9
status: issues_found
---

# Phase 02: Code Review Report

**Reviewed:** 2026-08-21
**Depth:** standard
**Files Reviewed:** 21
**Status:** issues_found

## Summary

The panel forecasting path (GlobalETS / GlobalTheta / GlobalCroston) is well-structured
and follows the established project patterns. The Rust FFI boundary has good null-pointer
checks, `catch_unwind` panic containment, and a correct `anofox_free_panel_forecast_result`
paired with `alloc_double_array`. The macro SQL pattern (subselect TABLE-arg) is sound and
consistent with the rest of the macro layer.

Three blockers were found:

1. **`model_pool` is parsed but silently dropped** — the C++ layer stores it in `bind_data`
   but never forwards it to `anofox_ts_forecast_panel`; `None` is always passed, making the
   `model_pool: 'Complete'` parameter a no-op even though the API advertises it.

2. **All-dropped panel silently returns zero rows** — when every series in the panel is
   dropped as too-short, the `n_kept == 0` early-exit path returns `FINISHED` before the
   already-queued `DROPPED` sentinel rows in `gstate.results` are emitted.  Users receive an
   empty result set with no indication that any series was dropped.

3. **Arithmetic overflow on large panels** — `n_series * series_len` and `n_series * horizon`
   are bare `usize` multiplications with no overflow guard in either the C++ flat-matrix
   allocation or the Rust output-buffer path.  A panel with >500 M cells silently wraps to
   a small allocation, causing an out-of-bounds write (UB) rather than an allocation error.

---

## Structural Findings (fallow)

No structural pre-pass was provided for this phase.

---

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: `model_pool` parameter parsed but never forwarded to FFI

**File:** `src/table_functions/ts_forecast_panel_native.cpp:231,591-600`

**Issue:** `ParseStringFromPanelParams` reads `model_pool` from the SQL params MAP and stores
it in `bind_data->model_pool` (line 231).  But the single `anofox_ts_forecast_panel` call at
lines 591-600 passes `nullptr` as the variant/model-pool argument — the parameter accepted by
`forecast_panel_impl` as `model_pool_str`.  The Rust function signature (lib.rs:7001) confirms
it always receives `None`:

```rust
forecast_panel_impl(flat, n_series, series_len, method_str, horizon, seasonal_period, None, variant_str)
//                                                                                     ^^^^
//                                                                     model_pool_str always None
```

The C++ FFI call does not even have a slot for it; the current 9-argument signature of
`anofox_ts_forecast_panel` includes only `variant` (for Croston SBA), not `model_pool`.

**Impact:** `MAP {'model_pool': 'Complete'}` is silently ignored. GlobalETS always uses the
Reduced pool (8 candidates) regardless of what the user requests. The docs and examples
advertise `model_pool: 'Complete'` (19 candidates) as a supported option.

**Fix:** Two coordinated changes are needed.

1. Add `model_pool` to the FFI call in the C++ layer:

```cpp
// Pass bind_data.model_pool as the 8th argument:
bool ok = anofox_ts_forecast_panel(
    flat_matrix.data(), n_kept, grid_len,
    bind_data.method.c_str(),
    static_cast<size_t>(bind_data.horizon),
    static_cast<size_t>(bind_data.seasonal_period),
    bind_data.croston_variant.empty() ? nullptr : bind_data.croston_variant.c_str(),
    bind_data.model_pool.empty()      ? nullptr : bind_data.model_pool.c_str(),   // ADD
    &panel_result, &error
);
```

2. Add the `model_pool` parameter to `anofox_ts_forecast_panel`'s Rust signature and
   forward it to `forecast_panel_impl`:

```rust
pub unsafe extern "C" fn anofox_ts_forecast_panel(
    ...
    model_pool: *const c_char,   // ADD
    ...
) {
    let model_pool_str: Option<&str> = if model_pool.is_null() { None } else {
        CStr::from_ptr(model_pool).to_str().ok().filter(|s| !s.is_empty())
    };
    forecast_panel_impl(..., model_pool_str, variant_str)
}
```

Update the C header and cbindgen output accordingly.

---

### CR-02: All-dropped panel silently returns empty result set

**File:** `src/table_functions/ts_forecast_panel_native.cpp:558-563`

**Issue:** When every series is short (fewer than 10 valid observations), DROPPED sentinel
rows are pushed into `gstate.results` during the loop over `group_order`.  But immediately
after the loop, the `n_kept == 0` branch returns `OperatorFinalizeResultType::FINISHED` with
`output.SetCardinality(0)` — before falling through to the output-emission block at line 671+
that iterates over `gstate.results`.  All the already-queued DROPPED rows are permanently
abandoned.

The same issue exists at the `n_kept < 3` path: the exception is thrown before DROPPED rows
are emitted.

**Concrete scenario:** A panel where all three series have 8 observations each returns exactly
0 rows instead of 3×horizon DROPPED rows; the 1–3-series case throws an exception that also
discards any already-queued rows.

```cpp
// CURRENT (buggy):
if (n_kept == 0) {
    gstate.processed = true;
    output.SetCardinality(0);
    return OperatorFinalizeResultType::FINISHED;  // gstate.results dropped silently
}
```

**Fix:** Set `gstate.processed = true` before either early exit to fall through to the output
loop — the output loop already handles the case where `gstate.results` is non-empty:

```cpp
// FIXED:
if (n_kept == 0) {
    gstate.processed = true;
    // fall through — output loop at line 671 will emit DROPPED rows from gstate.results
    // (or emit nothing if results is truly empty from the all_dates_set.empty() path)
    goto emit_output;  // or restructure with a flag
}

if (n_kept < 3) {
    // emit queued DROPPED rows before throwing
    gstate.processed = true;
    // ... re-structure to emit first, then throw as a deferred error
}
```

The cleanest fix is to restructure Finalize so that early exits set `processed=true` and fall
through to the existing output-batching block rather than returning early.

---

### CR-03: Integer overflow on large panel dimensions

**File:** `src/table_functions/ts_forecast_panel_native.cpp:577` and
          `crates/anofox-fcst-ffi/src/lib.rs:6998,7008`

**Issue:** Three bare multiplications have no overflow guard:

- C++ (line 577): `vector<double> flat_matrix(n_kept * grid_len)` — if `n_kept=10000` and
  `grid_len=5000` the product is 50 M doubles (400 MB) which is within range, but with M4's
  4227 series at avg 2357 obs the product is ~10 B, which overflows a 32-bit usize on some
  targets and wraps on 64-bit to a huge-but-wrong allocation.
- Rust (lib.rs:6998): `std::slice::from_raw_parts(values, n_series * series_len)` — overflow
  here creates a slice view larger than the actual allocation, causing immediate UB on access.
- Rust (lib.rs:7008): `let total = n_series * horizon` — unchecked; if `n_series * horizon`
  overflows the allocation is under-sized and writes past the end of the buffer.

On 64-bit Linux `usize` is 64-bit so practical overflow is extremely unlikely for normal
workloads. However it is a provable UB path on any target where the multiplication overflows,
and the Rust `slice::from_raw_parts` case is sound-unsafe.

**Fix:**

```rust
// lib.rs:6998
let len = n_series.checked_mul(series_len)
    .ok_or_else(|| PanelForecastError::InvalidModel(
        "Panel dimensions overflow (n_series * series_len > usize::MAX)".into()
    ))?;
let flat = std::slice::from_raw_parts(values, len);

// lib.rs:7008
let total = n_series.checked_mul(horizon)
    .filter(|&t| t > 0)
    .unwrap_or(0);
```

```cpp
// ts_forecast_panel_native.cpp:577 — add guard before allocation
if (n_kept > 0 && grid_len > std::numeric_limits<size_t>::max() / n_kept) {
    throw InvalidInputException(
        "ts_forecast_panel_by: panel too large (n_series=%zu x grid_len=%zu overflows size_t)",
        n_kept, grid_len);
}
vector<double> flat_matrix(n_kept * grid_len);
```

---

## Warnings

### WR-01: WASM `free` stub uses wrong Layout — potential heap corruption

**File:** `crates/anofox-fcst-ffi/src/lib.rs:44-53` (also `crates/anofox-fcst-ffi/src/allocation.rs:22-28`)

**Issue:** The WASM shim for `free` calls `dealloc` with `Layout::from_size_align(1, 8)` —
a 1-byte, 8-byte-aligned layout — regardless of the actual allocation size and type.  The
Rust global allocator's `dealloc` is required to receive the same `Layout` used by the
corresponding `alloc` call; passing a mismatched layout is undefined behaviour per the
allocator contract and can cause silent heap corruption or a runtime abort in debug builds.

The in-code comment "This is safe because DuckDB manages the actual memory" is incorrect: on
WASM the Rust allocator is used for these buffers (not DuckDB's allocator), so the layout
mismatch is a real concern.

**Fix:** Track size and alignment at allocation time by wrapping each allocation in a small
header, or replace the WASM malloc/free pair with a bumping arena sized to the known request
lifetime. At minimum, the comment should be corrected and the UB risk acknowledged until a
proper fix lands.

---

### WR-02: `n_kept < 3` check uses a hard-coded minimum without documentation, and throws before emitting DROPPED rows

**File:** `src/table_functions/ts_forecast_panel_native.cpp:565-571`

**Issue:** The 3-series minimum is separately tracked from `MIN_VALID_COUNT` (10) and is only
enforced after the drop loop; it is not exposed as a configurable threshold.  More importantly
(as noted in CR-02), this throw discards all already-accumulated DROPPED rows.  A 2-series
panel where one series is kept and the other is short produces an exception rather than the
expected result (1 forecast row + 1 DROPPED row) and loses the DROPPED sentinel entirely.

**Fix:** Emit DROPPED rows before throwing. Consider whether the 3-series minimum is the
right contract or if a 1-series minimum (or no minimum) makes more semantic sense for
GlobalTheta/GlobalCroston where the pooled parameter may degenerate gracefully on fewer series.

---

### WR-03: Benchmark series selection is non-deterministic across Python invocations

**File:** `benchmark/src/common/benchmark_runner.py:87`

**Issue:** `all_ids = train_df['unique_id'].unique()` followed by `all_ids[:max_series]`
takes the first 500 unique IDs in the order they appear in the DataFrame — which is insertion
order from a Parquet file. Across different Parquet readers, pandas versions, or re-sorted
input files this ordering can differ, making the subset non-reproducible between environments.
The anofox and statsforecast sides apply the same slicing independently (lines 87 and 156),
so within a single run they use the same subset; but cross-run comparisons can be comparing
different populations.

**Fix:** Sort before slicing to guarantee a stable subset:

```python
selected_ids = sorted(all_ids)[:max_series]
```

Both the anofox and statsforecast sides should apply the same `sorted()` call.

---

### WR-04: `date = 0` emitted for DROPPED rows with `grp.dates.empty()` — will produce epoch timestamp in output

**File:** `src/table_functions/ts_forecast_panel_native.cpp:477`

**Issue:** For groups whose `dates` vector is empty (no rows were ingested for that series),
DROPPED rows are emitted with `row.date = 0`.  When the output column type is `DATE` or
`TIMESTAMP`, this 0 is converted to the epoch (1970-01-01 for DATE; 1970-01-01 00:00:00 for
TIMESTAMP).  Callers filtering on date validity or joining on the date column will silently
receive a semantically wrong date instead of NULL.

The same path also sets `row.group_value = grp.group_value`, but for a group that was never
ingested (i.e., a race where `group_order` contains a key not present in `groups`), this
would be a default-constructed `Value` — however in practice this path is only reachable if a
group_key is in `group_order` but `grp.dates` is empty after collection, which requires an
edge case where only null date rows were seen for that group.

**Fix:** Emit `NULL` for the date column on DROPPED rows instead of epoch:

```cpp
row.date = std::numeric_limits<int64_t>::min();  // sentinel for NULL
// and in the output loop, set the column as NULL rather than converting the sentinel
output.data[2].SetValue(i, Value(bind_data.date_logical_type));  // NULL value
```

---

## Info

### IN-01: `model_pool` documented as `'Reduced'` (default) vs code default of `""` (empty string)

**File:** `docs/api/07-forecasting.md:363` and `src/table_functions/ts_forecast_panel_native.cpp:42`

**Issue:** The docs say the default for `model_pool` is `'Reduced'`.  The code stores `""`
(empty string) as the default and `forecast_panel_impl` maps `None` to `ModelPool::Reduced`
in Rust (lib.rs:6904-6907).  This is semantically correct but the docs could clarify that
omitting `model_pool` and specifying `model_pool: 'Reduced'` both select the Reduced pool,
while `model_pool: 'Complete'` is the only non-default value.  Currently (per CR-01) neither
`'Reduced'` nor `'Complete'` is forwarded, so the distinction is moot until CR-01 is fixed.

---

### IN-02: `anofox_runner.py` re-computes forecast dates in Python, masking panel date-alignment behaviour

**File:** `benchmark/src/common/anofox_runner.py:296-308`

**Issue:** For `TS_FORECAST_PANEL_BY` the benchmark discards the `ds` column returned by the
function and recomputes dates from `last_train_date + forecast_step days` (lines 304-305).
This silently masks any date-arithmetic bugs in the panel function's output (e.g., the epoch
issue in WR-04 would never be observed in benchmark output).  It also hard-codes `days` as
the unit regardless of the `freq` parameter, so monthly or hourly panels would produce
incorrect benchmark dates.

The re-computation is documented as necessary because the shared-grid anchors forecast dates
at the last date in the panel rather than per-series last date.  This is a valid design
trade-off, but the approach means the benchmark cannot verify that the extension produces
correct calendar-aware dates, which is a known project failure mode (per MEMORY.md:
"Verify SQL doc examples end-to-end").

---

_Reviewed: 2026-08-21_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
