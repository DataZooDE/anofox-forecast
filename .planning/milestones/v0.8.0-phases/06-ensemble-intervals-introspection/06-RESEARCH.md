# Phase 6: Ensemble Intervals & Introspection — Research

**Researched:** 2026-08-31
**Domain:** Ensemble conformal prediction intervals (EPI-01) + per-series member/weight introspection (INSP-01)
**Confidence:** HIGH — all findings verified by reading source files this session

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**EPI-01 — Ensemble prediction intervals:**
- Demonstrate + document the EXISTING conformal path — NO new interval code. The conformal machinery (`ts_cv_folds_by` + `ts_cv_forecast_by` + `ts_conformal_by` / `ts_conformal_calibrate`, split/adaptive/asymmetric/per-step) is model-agnostic. Phase 6 delivers a runnable `examples/*.sql` (+docs) showing learn→apply conformal intervals on an ensemble point forecast for BOTH the AutoEnsemble (`ts_forecast_by(...,'AutoEnsemble',...)`) and the explicit-member (`ts_forecast_ensemble_by`) surfaces, producing lower/upper bounds per horizon step.
- Verification: the example must show non-degenerate lower ≤ point ≤ upper bounds per step, produced by the existing conformal functions applied to backtested ensemble forecasts, run against the built extension.

**INSP-01 — Introspection scope (driven by crate 0.15.3 limits):**
- **Explicit-member ensemble:** FULL introspection — return each member's name + combination weight per series, for all six methods (via `Ensemble.weights()`). Weights must be consistent with the method (Mean → equal; WeightedMSE/InverseAIC/Stacking/HorizonAdaptive → crate-computed weights).
- **AutoEnsemble:** return the SELECTED member names + their in-sample MSE score (via `all_scores()[..model_count]`), plus combination weight only where trivially available (Mean → equal). Where the crate does not expose the weight (WeightedMSE/InverseAIC/Stacking/HorizonAdaptive on AutoEnsemble), return member+score with NULL weight. Document the gap as an upstream-crate limitation.
- Do NOT re-implement AutoEnsemble's selection to fabricate weights.

**INSP-01 — Surface & shape:**
- Dedicated function, long format: `(group_key, member_name, weight, rank_or_score)` — one row per member per series.
- Whether ONE or TWO functions (explicit-member vs AutoEnsemble) is Claude's discretion — pick the lower-complexity option.

### Claude's Discretion

- Exact introspection function name(s) and column names.
- Whether AutoEnsemble introspection is a separate function from explicit-member introspection.
- FFI shape for returning (name, weight, score) triples per series.
- Whether `rank` (1..k) and/or `score` (MSE) columns are both included.
- Whether to reuse the Phase 5 `forecast_explicit_ensemble` construction path (build the `Ensemble`, then read `.weights()`) as the introspection backend — recommended.

### Deferred Ideas (OUT OF SCOPE)

- AutoEnsemble inner-ensemble combination weights for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive — blocked by crate 0.15.3 (no accessor). File as a future enhancement / upstream request; do not re-implement selection.
- New conformal/interval algorithms (IDR/QRA/CQR/EnbPI/binned) — out of scope.
- Custom hand-supplied weights (ENS-F1) and panel/VAR ensembles (ENS-F2) — deferred beyond v0.8.0.
- Per-member parameters — still deferred.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| EPI-01 | User can attach distribution-free prediction intervals to an ensemble point forecast via the existing conformal path (learn + apply). | Conformal path confirmed model-agnostic; concrete SQL recipe for both surfaces documented below, with explicit-member limitation noted. |
| INSP-01 | User can inspect which member models an ensemble selected and their combination weights, per series. | `Ensemble::weights()` and `AutoEnsemble::all_scores()` accessor signatures verified from crate source. New FFI + C++ ScalarFunction + macro design below. |
</phase_requirements>

---

## Summary

Phase 6 has two distinct work items with very different sizes:

**EPI-01** is almost no-code for AutoEnsemble: `ts_cv_forecast_by` already accepts `method := 'AutoEnsemble'` (confirmed by reading `ts_cv_forecast_native.cpp`), so the full CV → conformal pipeline works out of the box. However, there is a **critical limitation**: `ts_cv_forecast_by` does NOT pass `top_k` or `combination_method` to the `ForecastOptions` struct — it zeroes those fields, causing the Rust core to use `top_k=3, method=Mean` regardless of what the user specifies in params. For the EPI-01 example this is fine (default AutoEnsemble with top_k=3/Mean is used for backtesting). For the **explicit-member** ensemble, `ts_forecast_ensemble_by` is a ScalarFunction, NOT a string-method dispatch — it cannot flow through `ts_cv_forecast_by` at all. The user must build CV folds manually via `ts_cv_folds_by`, then call `ts_forecast_ensemble_by` on each fold's train partition. This is the manual-fold workaround for EPI-01's explicit-member recipe.

**INSP-01** is genuine new code: a new Rust core function, a new FFI export, a new C++ ScalarFunction file, and a new SQL macro. The key design question (one function vs two) is answered by recommending **two separate functions** (`ts_ensemble_inspect_by` for explicit-member, `ts_auto_ensemble_inspect_by` for AutoEnsemble), because their inputs, backend calls, and output semantics differ materially. A mode-flag approach would require more complex bind/execute logic with no benefit.

The crate accessor signatures are confirmed [VERIFIED: ensemble/model.rs:202-219, ensemble/auto.rs:111-117]:
- `Ensemble::weights() -> &[f64]` — populated by `.fit()` for all six methods
- `Ensemble::model_count() -> usize`
- `Ensemble::horizon_weights() -> Option<&Vec<Vec<f64>>>` — only for HorizonAdaptive
- `AutoEnsemble::all_scores() -> &[(String, f64)]` — all candidates (name, MSE), sorted ascending by MSE
- `AutoEnsemble::model_count() -> usize` — the number of selected top-K members

**Primary recommendation:** Two-function INSP-01 design (explicit-member + AutoEnsemble), with explicit-member using the same `build_forecaster` + `Ensemble::new().fit()` + `.weights()` path as `forecast_explicit_ensemble`, and AutoEnsemble using `AutoEnsemble::with_config().fit()` + `.all_scores()[..model_count()]`.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| EPI-01: CV backtest for AutoEnsemble | SQL (existing `ts_cv_forecast_by`) | — | Already dispatches 'AutoEnsemble' via ForecastOptions.model string |
| EPI-01: CV backtest for explicit-member ensemble | SQL (manual fold loop over `ts_cv_folds_by`) | — | `ts_forecast_ensemble_by` is a ScalarFunction, cannot feed into `ts_cv_forecast_by` |
| EPI-01: Conformal calibration + apply | SQL (existing conformal macros) | — | Fully model-agnostic — just needs actual/yhat columns |
| INSP-01: Ensemble weight computation | Rust Core (`forecast.rs` new functions) | — | Builds Ensemble, calls .fit(), reads .weights() |
| INSP-01: AutoEnsemble score retrieval | Rust Core (`forecast.rs` new functions) | — | Builds AutoEnsemble, calls .fit(), reads .all_scores() |
| INSP-01: FFI (name+weight+score per-series) | Rust FFI (`lib.rs` new exports) | — | Two new exports, one per surface |
| INSP-01: C++ per-series ScalarFunction | C++ (new `.cpp` file per ScalarFunction precedent) | — | Follows `_ts_forecast_ensemble_native` shape exactly |
| INSP-01: SQL surface | SQL Macro (`ts_macros.cpp`) | — | Two new `_by` macros |

---

## CRUX A — EPI-01: Does the existing conformal path work on ensemble forecasts?

### AutoEnsemble — YES, with a top_k/method limitation

`ts_cv_forecast_by` accepts `method := 'AutoEnsemble'` — confirmed by reading the bind function [VERIFIED: src/table_functions/ts_cv_forecast_native.cpp:371-373]:

```cpp
bind_data->method = input.inputs[1].GetValue<string>();
```

The method string is stored and later copied to `opts.model` [VERIFIED: src/table_functions/ts_cv_forecast_native.cpp:671]:

```cpp
strncpy(opts.model, bind_data.method.c_str(), sizeof(opts.model) - 1);
```

The call is `anofox_ts_forecast(...)` which dispatches through the core `forecast()` function. At `ModelType::AutoEnsemble`, the core uses [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:777-781]:

```rust
ModelType::AutoEnsemble => forecast_auto_ensemble(
    values, horizon,
    if options.ensemble_top_k == 0 { 3 } else { options.ensemble_top_k },
    options.ensemble_method.as_deref(),
    period
),
```

**Critical limitation:** The CV native does NOT parse `top_k` or `combination_method` from params [VERIFIED: src/table_functions/ts_cv_forecast_native.cpp:380-388 — only parses `model`, `seasonal_period`, `confidence_level`, `window`, `seasonal_periods`, `model_pool`, `laplace_variant`, `laplace_seasonal_batch_init`]. The fields `ensemble_top_k` and `ensemble_method` in `ForecastOptions` are zeroed by `memset` [VERIFIED: src/table_functions/ts_cv_forecast_native.cpp:669], so the backtest always runs with `top_k=3, method=Mean` regardless of the user's `combination_method` parameter to `ts_cv_forecast_by`. This is fine for the EPI-01 example (use default AutoEnsemble for backtesting), but must be documented.

**The complete EPI-01 AutoEnsemble SQL recipe:**

```sql
-- Step 1: Create fold table
CREATE OR REPLACE TABLE ens_folds AS
SELECT * FROM ts_cv_folds_by('series_data', unique_id, ds, y,
    3,     -- n_folds
    12,    -- horizon
    MAP{});

-- Step 2: Backtest — 'AutoEnsemble' works directly in ts_cv_forecast_by
--         (uses top_k=3, method=Mean for backtesting — the default)
CREATE OR REPLACE TABLE ens_bt AS
SELECT * FROM ts_cv_forecast_by('ens_folds', unique_id, ds, y, 'AutoEnsemble', MAP{});

-- Step 3: Calibrate conformal quantile from backtest residuals
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('ens_bt', y, yhat, {alpha: 0.1});

-- Step 4: Generate new point forecasts (AutoEnsemble, same default params)
CREATE OR REPLACE TABLE ens_fcst AS
SELECT * FROM ts_forecast_by('series_data', unique_id, ds, y, 'AutoEnsemble', 12, '1d');

-- Step 5: Apply conformal bounds → lower/upper per horizon step
SELECT f.unique_id, f.ds, f.forecast_step, f.yhat,
       f.yhat - (SELECT conformity_score FROM calib) AS yhat_lower,
       f.yhat + (SELECT conformity_score FROM calib) AS yhat_upper
FROM ens_fcst f;

-- Or use ts_conformal_apply_by:
SELECT * FROM ts_conformal_apply_by('ens_fcst', unique_id, yhat,
    (SELECT conformity_score FROM calib));
```

**Verification assertion:**

```sql
-- All steps: lower <= yhat <= upper (non-degenerate intervals)
SELECT count(*) AS bad_rows
FROM ens_with_intervals
WHERE yhat_lower > yhat OR yhat > yhat_upper;
-- Must return 0
```

### Explicit-member ensemble — Manual fold workaround required

`ts_forecast_ensemble_by` is registered as a `ScalarFunction` (per Phase 5 SUMMARY: "_ts_forecast_ensemble_native registered via ScalarFunction per _ts_forecast_scalar precedent") [VERIFIED: src/table_functions/ts_forecast_ensemble_native.cpp:442-456]. It takes `LIST(DATE)`, `LIST(DOUBLE)`, `LIST(VARCHAR)` — not a fold table. The `ts_cv_forecast_by` function dispatches only via `ForecastOptions.model` string + `anofox_ts_forecast()` FFI, which has no path to the `anofox_ts_forecast_ensemble` FFI export.

**Conclusion:** The explicit-member ensemble CANNOT flow through `ts_cv_forecast_by`. The EPI-01 example must demonstrate the manual-fold pattern.

**The EPI-01 explicit-member SQL recipe (manual fold workaround):**

```sql
-- Step 1: Create folds
CREATE OR REPLACE TABLE exp_folds AS
SELECT * FROM ts_cv_folds_by('series_data', unique_id, ds, y, 3, 5, MAP{});

-- Step 2: Build residuals manually — for each fold's train data, forecast with the
--         explicit ensemble and compare to the test rows.
--         The fold structure: split='train' rows are training; split='test' rows have actuals.
--         We create one forecast per (unique_id, fold_id) on the train partition.

CREATE OR REPLACE TABLE exp_bt AS
WITH train_data AS (
    SELECT unique_id, fold_id, ds, y
    FROM exp_folds WHERE split = 'train'
),
test_data AS (
    SELECT unique_id, fold_id, ds, y AS actual,
           ROW_NUMBER() OVER (PARTITION BY unique_id, fold_id ORDER BY ds) AS step
    FROM exp_folds WHERE split = 'test'
),
-- Forecast per (unique_id, fold_id) on the train partition
ens_forecasts AS (
    SELECT unique_id, fold_id, forecast_step, yhat
    FROM (
        SELECT unique_id, fold_id,
               unnest(_ts_forecast_ensemble_native(
                   LIST(ds ORDER BY ds),
                   LIST(y::DOUBLE ORDER BY ds),
                   ['AutoARIMA', 'AutoETS', 'Theta'],
                   (SELECT max(step) FROM test_data t WHERE t.unique_id = d.unique_id AND t.fold_id = d.fold_id),
                   '1d',
                   '',     -- combination_method: '' → mean
                   0       -- seasonal_period
               ), recursive := true)
        FROM train_data d
        GROUP BY unique_id, fold_id
    )
)
-- Join forecast to actual test rows
SELECT t.unique_id, t.fold_id, t.ds, t.actual AS y, e.yhat
FROM test_data t
JOIN ens_forecasts e
    ON t.unique_id = e.unique_id AND t.fold_id = e.fold_id AND t.step = e.forecast_step;

-- Step 3: Calibrate
CREATE OR REPLACE TABLE exp_calib AS
SELECT * FROM ts_conformal_calibrate('exp_bt', y, yhat, {alpha: 0.1});

-- Step 4: New forecast
CREATE OR REPLACE TABLE exp_fcst AS
SELECT * FROM ts_forecast_ensemble_by('series_data', unique_id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d', combination_method := '');

-- Step 5: Apply
SELECT f.unique_id, f.ds, f.forecast_step, f.yhat,
       f.yhat - (SELECT conformity_score FROM exp_calib) AS yhat_lower,
       f.yhat + (SELECT conformity_score FROM exp_calib) AS yhat_upper
FROM exp_fcst f;
```

**NOTE FOR PLANNING:** The manual-fold recipe is more complex and requires the planner to document this as a "limitation" in the example file, not a workaround to hide. The EPI-01 example should be a single SQL file with two sections: Section 1 = AutoEnsemble (clean, uses `ts_cv_forecast_by`), Section 2 = explicit-member (manual fold recipe). The limitation should be clearly documented in the example header.

---

## CRUX B — INSP-01: New Introspection FFI + Function Design

### Crate Accessor Signatures (VERIFIED)

**`Ensemble` struct** [VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs:152-219]:

```rust
pub struct Ensemble {
    models: Vec<Box<dyn Forecaster>>,
    method: CombinationMethod,
    custom_weights: Option<Vec<f64>>,
    weights: Vec<f64>,              // populated by .fit() for all methods
    horizon_weights: Option<Vec<Vec<f64>>>,  // only for HorizonAdaptive
    fitted: Option<Vec<f64>>,
    residuals: Option<Vec<f64>>,
    is_fitted: bool,
}

impl Ensemble {
    pub fn weights(&self) -> &[f64]                        // always valid after .fit()
    pub fn horizon_weights(&self) -> Option<&Vec<Vec<f64>>> // Some only for HorizonAdaptive
    pub fn method(&self) -> CombinationMethod
    pub fn model_count(&self) -> usize
}
```

**Weight initialization and fit** [VERIFIED: model.rs:172-186, 575-624]:
- `Ensemble::new(models)` initializes `weights = vec![1.0 / n as f64; n]` — uniform weights.
- `.fit()` then recomputes weights for: `WeightedMSE` → `compute_mse_weights`, `InverseAIC` → `compute_aic_weights`, `Stacking` → `compute_stacking_weights`, `HorizonAdaptive` → `compute_horizon_adaptive_weights`.
- `Mean` and `Median` leave weights at the uniform initial value (no weight computation in `fit()`).
- After `.fit()`, `weights()` always returns a slice of length equal to `model_count()`, and for `WeightedMSE`/`InverseAIC`/`Stacking`, `sum(weights) == 1.0` (all three compute `w / sum` normalization).
- For `HorizonAdaptive`: `self.weights` is set to the AVERAGE of per-horizon weights (normalized); `self.horizon_weights` is `Some(Vec<Vec<f64>>)` with length `max_horizon` (capped at `min(20, 0.2*n)`).

**`AutoEnsemble` struct** [VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/auto.rs:79-126]:

```rust
pub struct AutoEnsemble {
    config: AutoEnsembleConfig,
    ensemble: Option<Ensemble>,
    scores: Vec<(String, f64)>,     // all candidates (name, MSE), sorted ascending
}

impl AutoEnsemble {
    pub fn all_scores(&self) -> &[(String, f64)]   // all fitted candidates; 0..model_count = selected
    pub fn model_count(&self) -> usize             // delegates to inner Ensemble.model_count()
}
```

**`AutoEnsembleConfig` default** [VERIFIED: auto.rs:25-33]:
```rust
impl Default for AutoEnsembleConfig {
    fn default() -> Self {
        Self {
            top_k: 3,
            combination_method: CombinationMethod::WeightedMSE,  // NOT Mean!
            seasonal_period: None,
        }
    }
}
```

**IMPORTANT:** The `AutoEnsembleConfig::default()` uses `WeightedMSE`, NOT `Mean`. This means `AutoEnsemble::new()` (no config) selects top-3 with WeightedMSE combination. The `parse_combination_method` in `forecast_auto_ensemble` overrides this if the user passes a method string. The inner `Ensemble` has weights computed for WeightedMSE — but we CANNOT access them because `AutoEnsemble.ensemble` is a private field (`Option<Ensemble>`). The only public accessor is `all_scores()` (candidates + MSE) and `model_count()`.

### INSP-01 Architecture Decision: TWO Functions

**Recommendation: Two separate functions** — `ts_ensemble_inspect_by` (explicit-member) and `ts_auto_ensemble_inspect_by` (AutoEnsemble).

Rationale:
1. **Inputs differ materially**: explicit-member takes `members VARCHAR[]` + `combination_method`; AutoEnsemble takes `top_k INTEGER` + `combination_method`. A mode-flag would require runtime branching on input presence.
2. **Backend calls differ**: explicit-member calls `inspect_explicit_ensemble` (builds `Ensemble`, reads `.weights()`); AutoEnsemble calls `inspect_auto_ensemble` (builds `AutoEnsemble`, reads `.all_scores()[..model_count]`).
3. **Output semantics differ**: explicit-member always has weight (no NULL); AutoEnsemble has score (MSE) and optional weight (NULL for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive).
4. **Two functions** mirrors the Phase 4/5 pattern: `ts_forecast_by` + `ts_forecast_ensemble_by` as separate surfaces.

### Rust Core — Two New Functions

**Function 1: `inspect_explicit_ensemble`**

```rust
// New function in crates/anofox-fcst-core/src/forecast.rs
// After forecast_explicit_ensemble (around line 2871)

/// Inspect combination weights for an explicit-member ensemble.
///
/// Builds the Ensemble from the named members, fits it on the series, then
/// returns the per-member weights. For Mean and Median, weights equal 1/k.
/// For WeightedMSE, InverseAIC, Stacking, HorizonAdaptive, weights are
/// crate-computed and sum to 1.0. HorizonAdaptive weights are the AVERAGE
/// of per-horizon weights (use horizon_weights() for per-step detail; that is
/// out of scope for v1).
///
/// Returns: Vec<(member_name: String, weight: f64)> — k entries.
pub fn inspect_explicit_ensemble(
    values: &[Option<f64>],
    member_names: &[String],
    method_str: Option<&str>,
    period: usize,
) -> Result<Vec<(String, f64)>> {
    use anofox_forecast::models::ensemble::Ensemble;

    if member_names.len() < 2 {
        return Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: member_names.len().to_string(),
            reason: "at least 2 members are required for an ensemble".to_string(),
        });
    }

    let combination_method = parse_combination_method(method_str)?;
    let clean_values: Vec<f64> = fill_nulls_interpolate(values);
    if clean_values.len() < 3 {
        return Err(ForecastError::InsufficientData {
            needed: 3,
            got: clean_values.len(),
        });
    }

    let member_period = if period > 1 { Some(period) } else { None };
    let mut members: Vec<anofox_forecast::models::BoxedForecaster> =
        Vec::with_capacity(member_names.len());
    for name in member_names {
        let model_type: ModelType = name.parse().map_err(|_| ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: name.clone(),
            reason: format!("unknown model name '{}'", name),
        })?;
        members.push(build_forecaster(model_type, member_period)?);
    }

    let ts = make_timeseries(&clean_values)?;
    let mut ens = Ensemble::new(members).with_method(combination_method);
    ens.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("Ensemble fit failed: {}", e))
    })?;

    let weights = ens.weights();
    Ok(member_names.iter().cloned().zip(weights.iter().copied()).collect())
}
```

**Function 2: `inspect_auto_ensemble`**

```rust
/// Inspect selected members and in-sample MSE scores for an AutoEnsemble.
///
/// Builds and fits an AutoEnsemble, then returns the selected top-K members
/// (the first model_count entries from all_scores()) with their MSE scores
/// and — for Mean combination only — equal combination weights.
///
/// Returns: Vec<(member_name: String, mse_score: f64, weight: Option<f64>)>
///   weight is Some(1/k) for Mean, None for all other combination methods
///   (WeightedMSE/InverseAIC/Stacking/HorizonAdaptive inner weights are not
///   accessible from the crate 0.15.3 public API — upstream limitation).
pub fn inspect_auto_ensemble(
    values: &[f64],
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
) -> Result<Vec<(String, f64, Option<f64>)>> {
    use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};

    let combination_method = parse_combination_method(method_str)?;
    let config = AutoEnsembleConfig {
        top_k,
        combination_method,
        seasonal_period: if period > 1 { Some(period) } else { None },
    };
    let ts = make_timeseries(values)?;
    let mut model = AutoEnsemble::with_config(config);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e))
    })?;

    let k = model.model_count();  // actual selected count (may be < top_k if fewer candidates fitted)
    let all = model.all_scores();  // all candidates sorted ascending by MSE; first k are selected
    let selected: Vec<_> = all.iter().take(k).collect();

    // Weight: Some(1/k) for Mean only; None for all other methods (crate 0.15.3 limitation)
    let weight_if_mean: Option<f64> = match combination_method {
        anofox_forecast::models::ensemble::CombinationMethod::Mean => {
            if k > 0 { Some(1.0 / k as f64) } else { None }
        }
        _ => None,
    };

    Ok(selected.iter().map(|(name, score)| {
        (name.clone(), *score, weight_if_mean)
    }).collect())
}
```

### FFI Surface — Two New Exports

Both new FFI exports follow the `anofox_ts_forecast_ensemble` precedent (null-delimited member buffer, catch_unwind).

**Key design decision: returning variable (name, weight, score) rows**

The best approach for variable-length per-member rows is **parallel arrays** — a packed approach consistent with the `ForecastResult` pattern (parallel `point_forecasts`, `lower_bounds`, `upper_bounds` arrays). We return:
- `member_names_buf: *mut c_char` — all member names concatenated null-delimited
- `member_names_buf_len: size_t` — byte length of the buffer
- `weights: *mut c_double` — one weight per member (k entries), or null for AutoEnsemble-with-non-Mean
- `scores: *mut c_double` — one MSE score per member (k entries), or null for explicit-member
- `count: size_t` — number of members (k)

This is simpler than a packed struct because C++ already knows how to split null-delimited buffers (it builds them in Phase 5).

**Export 1: `anofox_ts_ensemble_inspect`** (explicit-member)

```c
// crates/anofox-fcst-ffi/src/lib.rs — new export
bool anofox_ts_ensemble_inspect(
    const double*  values,
    const uint64_t* validity,
    size_t         length,
    const char*    members_buf,      // null-delimited member names
    size_t         members_buf_len,
    size_t         members_count,
    const char*    combination_method, // NULL or ""  → "mean"
    int            seasonal_period,
    EnsembleInspectResult* out_result,
    AnofoxError*   out_error
) -> bool
```

**Export 2: `anofox_ts_auto_ensemble_inspect`** (AutoEnsemble)

```c
bool anofox_ts_auto_ensemble_inspect(
    const double*  values,
    const uint64_t* validity,
    size_t         length,
    int            top_k,              // 0 → default 3
    const char*    combination_method, // NULL or "" → "weighted_mse" (crate default)
    int            seasonal_period,
    EnsembleInspectResult* out_result,
    AnofoxError*   out_error
) -> bool
```

**New FFI struct: `EnsembleInspectResult`** (must be `#[repr(C)]`)

```rust
#[repr(C)]
pub struct EnsembleInspectResult {
    pub count: size_t,                 // number of members
    pub member_names_buf: *mut c_char, // null-delimited names
    pub member_names_buf_len: size_t,  // byte length of names buffer
    pub weights: *mut c_double,        // len=count; or null if no weights
    pub scores: *mut c_double,         // len=count MSE scores; or null for explicit-member
}

// Companion free function:
void anofox_free_ensemble_inspect_result(EnsembleInspectResult* result)
```

**After adding these to `lib.rs`, run `make header`** to regenerate `src/include/anofox_fcst_ffi.h`.

### C++ Surface — New ScalarFunction File

Following the `ts_forecast_ensemble_native.cpp` precedent: a new file for INSP-01 that registers TWO ScalarFunctions (one per surface) in a single file to avoid a third new `.cpp`. This keeps the change count bounded.

**New file: `src/table_functions/ts_ensemble_inspect_native.cpp`**

This file registers:
1. `_ts_ensemble_inspect_native` — explicit-member introspection
2. `_ts_auto_ensemble_inspect_native` — AutoEnsemble introspection

Both return `LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))` — the score column is NULL for explicit-member, the weight column is NULL for AutoEnsemble/non-Mean.

**_ts_ensemble_inspect_native ScalarFunction signature:**

```
_ts_ensemble_inspect_native(
    values     LIST(DOUBLE),
    members    LIST(VARCHAR),
    combination_method VARCHAR,
    seasonal_period    INTEGER
) -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
```

**_ts_auto_ensemble_inspect_native ScalarFunction signature:**

```
_ts_auto_ensemble_inspect_native(
    values             LIST(DOUBLE),
    top_k              INTEGER,
    combination_method VARCHAR,
    seasonal_period    INTEGER
) -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
```

Both follow the `TsForecastEnsembleNativeExecute` shape: receive per-row LIST arguments, extract, build a null-delimited buffer, call the FFI, unpack result into a DuckDB LIST(STRUCT).

**CMakeLists.txt:** Add `src/table_functions/ts_ensemble_inspect_native.cpp` to `EXTENSION_SOURCES` [VERIFIED: CMakeLists.txt:162-210 — explicit list, no glob].

**Registration:** One new header `src/include/ts_ensemble_inspect_native.hpp` declaring `RegisterTsEnsembleInspectNativeFunction(loader)`. Add `#include + call` to `src/anofox_forecast_extension.cpp`.

### SQL Macros — Two `_by` Wrappers

Following the `ts_forecast_ensemble_by` macro pattern [VERIFIED: src/macros/ts_macros.cpp:607-632]:

**`ts_ensemble_inspect_by`** (explicit-member):

```sql
-- Macro in ts_macros.cpp
{"ts_ensemble_inspect_by",
 {"source", "group_col", "date_col", "target_col", "members", nullptr},
 {{"combination_method", "''"}, {"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, member_name, weight, score
FROM (
    SELECT group_col,
           unnest(_ts_ensemble_inspect_native(
               LIST(target_col::DOUBLE ORDER BY date_col),
               members,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
...}
```

**`ts_auto_ensemble_inspect_by`** (AutoEnsemble):

```sql
{"ts_auto_ensemble_inspect_by",
 {"source", "group_col", "date_col", "target_col", nullptr},
 {{"top_k", "3"}, {"combination_method", "''"}, {"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, member_name, weight, score
FROM (
    SELECT group_col,
           unnest(_ts_auto_ensemble_inspect_native(
               LIST(target_col::DOUBLE ORDER BY date_col),
               top_k,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
...}
```

**Output column names:** `(group_col, member_name, weight, score)` — simple, descriptive, JOIN-friendly.

---

## CRUX C — DoD Internal Consistency for INSP-01

### Weight Properties Verified from Source

**Mean and Median** [VERIFIED: model.rs:172-186, 621-623]: `Ensemble::new(models)` sets `weights = vec![1.0 / n as f64; n]`. For Mean and Median, `.fit()` does NOT recompute weights (the `match` arms for `Mean | Median` do nothing). So after `.fit()`, `weights()[i] == 1.0/k` for all i.

**WeightedMSE** [VERIFIED: model.rs:324-350]: Computes inverse-MSE weights, normalized: `w_i = (1/MSE_i) / sum(1/MSE_j)`. Sum of weights is 1.0 exactly (by construction).

**InverseAIC** [VERIFIED: model.rs:352-390]: Computes Akaike weights: `w_i = exp(-0.5*(AIC_i - AIC_min)) / sum`. Falls back to equal weights if all AIC values fail. Sum of weights is 1.0.

**Stacking** [VERIFIED: model.rs:392-475]: Projected gradient descent on simplex constraint. `nnls_simplex` projection enforces `sum(w)=1, w_i>=0`. Sum of weights is 1.0 after projection.

**HorizonAdaptive** [VERIFIED: model.rs:477-572]: `self.weights` = average of per-horizon weight vectors, then normalized: `s = sum(avg_w); for wj: *wj /= s`. Sum of weights is 1.0.

**All methods: `sum(weights()) == 1.0`** and `weights().len() == model_count()`.

### DoD Assertion SQL

```sql
-- 1. Mean method: weights are exactly equal (1/k per member)
WITH inspect AS (
    SELECT * FROM ts_ensemble_inspect_by('test_series', id, ds, y,
        ['AutoARIMA', 'AutoETS', 'Theta'],
        combination_method := 'mean', seasonal_period := 0)
)
SELECT
    -- Each weight = 1/3 within floating point tolerance
    count(*) FILTER (WHERE abs(weight - 1.0/3.0) > 1e-10) AS unequal_mean_weights,
    -- Weights sum to 1.0 per group
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-10) AS bad_sum
FROM (
    SELECT id, weight, SUM(weight) OVER (PARTITION BY id) AS sum_w FROM inspect
);
-- Both counts must be 0

-- 2. WeightedMSE: weights sum to 1.0 per group, all positive
WITH inspect AS (
    SELECT * FROM ts_ensemble_inspect_by('test_series', id, ds, y,
        ['AutoARIMA', 'AutoETS', 'Theta'],
        combination_method := 'weighted_mse', seasonal_period := 0)
)
SELECT
    count(*) FILTER (WHERE weight < 0) AS negative_weights,
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-6) AS bad_sum
FROM (
    SELECT id, weight, SUM(weight) OVER (PARTITION BY id) AS sum_w FROM inspect
);
-- Both counts must be 0

-- 3. AutoEnsemble: model_count rows per series, score > 0, weight NULL for non-Mean
WITH inspect AS (
    SELECT * FROM ts_auto_ensemble_inspect_by('test_series', id, ds, y,
        top_k := 3, combination_method := '', seasonal_period := 0)
)
SELECT
    count(*) FILTER (WHERE score <= 0 OR score IS NULL) AS bad_scores,
    count(*) FILTER (WHERE weight IS NOT NULL) AS non_null_weights  -- should be 0 for default WeightedMSE
FROM inspect;

-- 4. AutoEnsemble Mean: weight = 1/k, not NULL
WITH inspect AS (
    SELECT * FROM ts_auto_ensemble_inspect_by('test_series', id, ds, y,
        top_k := 3, combination_method := 'mean', seasonal_period := 0)
)
SELECT
    count(*) FILTER (WHERE weight IS NULL) AS null_weights,       -- must be 0
    count(*) FILTER (WHERE abs(weight - 1.0/3.0) > 1e-10) AS unequal_weights  -- must be 0
FROM inspect;
```

---

## Standard Stack

No new crate dependencies. All needed APIs are in `anofox-forecast` 0.15.3.

### Files Changed or Created

| Component | Action | Location |
|-----------|--------|----------|
| `forecast.rs` | Add `inspect_explicit_ensemble` + `inspect_auto_ensemble` (after `forecast_explicit_ensemble` at ~line 2871) | `crates/anofox-fcst-core/src/forecast.rs` |
| `lib.rs` (core) | Export `inspect_explicit_ensemble` and `inspect_auto_ensemble` | `crates/anofox-fcst-core/src/lib.rs` |
| `lib.rs` (FFI) | Add `EnsembleInspectResult` struct, `anofox_ts_ensemble_inspect`, `anofox_ts_auto_ensemble_inspect`, `anofox_free_ensemble_inspect_result` | `crates/anofox-fcst-ffi/src/lib.rs` |
| `anofox_fcst_ffi.h` | Regenerate via `make header` | `src/include/anofox_fcst_ffi.h` |
| `ts_ensemble_inspect_native.hpp` | NEW header | `src/include/ts_ensemble_inspect_native.hpp` |
| `ts_ensemble_inspect_native.cpp` | NEW — two ScalarFunctions (`_ts_ensemble_inspect_native` and `_ts_auto_ensemble_inspect_native`) | `src/table_functions/ts_ensemble_inspect_native.cpp` |
| `CMakeLists.txt` | Add `ts_ensemble_inspect_native.cpp` to `EXTENSION_SOURCES` | `CMakeLists.txt` |
| `anofox_forecast_extension.cpp` | `#include "ts_ensemble_inspect_native.hpp"` + `RegisterTsEnsembleInspectNativeFunction(loader)` | `src/anofox_forecast_extension.cpp` |
| `ts_macros.cpp` | Add `ts_ensemble_inspect_by` and `ts_auto_ensemble_inspect_by` macros | `src/macros/ts_macros.cpp` |
| `examples/forecasting/ensemble_intervals.sql` | NEW — EPI-01 DoD | `examples/forecasting/ensemble_intervals.sql` |
| `examples/forecasting/ensemble_inspect.sql` | NEW — INSP-01 DoD | `examples/forecasting/ensemble_inspect.sql` |
| `docs/...` | NEW/Updated reference docs + API entries | `docs/api/`, `docs/reference/models/ensemble/` |

---

## Architecture Patterns

### System Architecture Diagram

```
EPI-01 (AutoEnsemble):
  ts_cv_folds_by → ts_cv_forecast_by(method='AutoEnsemble') → ts_conformal_calibrate
  → ts_forecast_by(method='AutoEnsemble') → ts_conformal_apply_by → lower/upper

EPI-01 (explicit-member, manual fold):
  ts_cv_folds_by → [user SQL loop per fold] → _ts_forecast_ensemble_native per fold
  → ts_conformal_calibrate → ts_forecast_ensemble_by → ts_conformal_apply_by → lower/upper

INSP-01 (explicit-member):
  SQL: ts_ensemble_inspect_by('data', id, ds, y, ['A','B','C'], combination_method:='mean')
    │
    ▼ ts_macros.cpp: ts_ensemble_inspect_by → GROUP BY → _ts_ensemble_inspect_native(...)
    │
    ▼ src/table_functions/ts_ensemble_inspect_native.cpp (ScalarFunction)
      extract LIST(DOUBLE) values, LIST(VARCHAR) members, build null-delimited buf
      call anofox_ts_ensemble_inspect(values, members_buf, ..., out_result)
    │
    ▼ crates/anofox-fcst-ffi/src/lib.rs: anofox_ts_ensemble_inspect
      parse members → Vec<String>; call inspect_explicit_ensemble(values, members, method, period)
    │
    ▼ crates/anofox-fcst-core/src/forecast.rs: inspect_explicit_ensemble
      build_forecaster per member → Ensemble::new(members).with_method(method).fit(&ts)
      ens.weights() → Vec<(name, weight)>
    │
    ▼ EnsembleInspectResult { count, member_names_buf, weights, scores=null }
    │
    ▼ ScalarFunction unpacks → LIST(STRUCT(member_name, weight, score=NULL))
    │
    ▼ SQL: unnest(..., recursive:=true) → (group_col, member_name, weight, score)

INSP-01 (AutoEnsemble):
  Same path but uses anofox_ts_auto_ensemble_inspect → inspect_auto_ensemble
  → AutoEnsemble::with_config(top_k, method, period).fit(&ts)
  → .all_scores()[..model_count()] → Vec<(name, mse, Option<weight>)>
  → weights=Some(1/k) for Mean, None for all other methods
```

### Recommended Project Structure

```
crates/anofox-fcst-core/src/forecast.rs     ← +inspect_explicit_ensemble, +inspect_auto_ensemble
crates/anofox-fcst-core/src/lib.rs          ← export both new functions
crates/anofox-fcst-ffi/src/lib.rs           ← +EnsembleInspectResult, +2 exports, +free fn
src/include/anofox_fcst_ffi.h               ← regenerate (make header)
src/include/ts_ensemble_inspect_native.hpp  ← NEW
src/table_functions/ts_ensemble_inspect_native.cpp  ← NEW (2 ScalarFunctions in one file)
CMakeLists.txt                              ← add ts_ensemble_inspect_native.cpp
src/anofox_forecast_extension.cpp           ← #include + Register call
src/macros/ts_macros.cpp                    ← +ts_ensemble_inspect_by, +ts_auto_ensemble_inspect_by
examples/forecasting/ensemble_intervals.sql ← NEW (EPI-01 DoD)
examples/forecasting/ensemble_inspect.sql   ← NEW (INSP-01 DoD)
docs/api/07-forecasting.md                  ← add ts_ensemble_inspect_by, ts_auto_ensemble_inspect_by entries
docs/api/11-conformal-prediction.md         ← add ensemble conformal example section
docs/reference/models/ensemble/ensemble_inspect.md  ← NEW
```

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Combination weight computation | Custom weight formulas | `Ensemble::new(members).with_method(method).fit(&ts).weights()` | Crate already implements all 6 methods with normalization |
| AutoEnsemble member selection | Custom top-K selection logic | `AutoEnsemble::with_config(top_k, method, period).fit(&ts).all_scores()[..model_count()]` | Crate handles candidate fitting, MSE scoring, and selection |
| Conformal intervals | New interval algorithm | Existing `ts_conformal_calibrate` + `ts_conformal_apply_by` | These are model-agnostic; just feed ensemble backtest residuals |
| Manual CV for explicit-member | Skip CV; use in-sample residuals | Manual fold loop via `ts_cv_folds_by` + per-fold `_ts_forecast_ensemble_native` | Out-of-sample residuals give calibrated, honest coverage |
| AutoEnsemble inner weights | Reverse-engineer WeightedMSE from scores | Return NULL weight with documented crate limitation | Fabricated weights would drift from crate internals across versions |

---

## Common Pitfalls

### Pitfall 1: `ts_cv_forecast_by` silently uses top_k=3/Mean for AutoEnsemble

**What goes wrong:** User expects `ts_cv_forecast_by('folds', id, ds, y, 'AutoEnsemble', MAP{'top_k': '5', 'combination_method': 'weighted_mse'})` to use top_k=5 and WeightedMSE for backtesting. The CV native does NOT parse these params — they are ignored.

**Why it happens:** [VERIFIED: ts_cv_forecast_native.cpp:380-388] The CV native only recognizes `model`, `seasonal_period`, `confidence_level`, `window`, `seasonal_periods`, `model_pool`, `laplace_variant`, `laplace_seasonal_batch_init`. The fields `ensemble_top_k` and `ensemble_method` in `ForecastOptions` are zeroed by `memset`, causing `forecast()` to use the `top_k==0 → 3` fallback with `method_str=None → Mean`.

**How to avoid:** Document this limitation in the EPI-01 example. The EPI-01 example should use plain `'AutoEnsemble'` (default top_k=3, Mean) for both backtesting and the final forecast, for internal consistency.

**Warning sign:** If the user sets `combination_method` in the CV params and expects that method in the intervals, the intervals will be calibrated from a different model than the final forecast — miscoverage risk.

### Pitfall 2: `Ensemble::new()` initializes weights BEFORE fit — don't call `.weights()` pre-fit

**What goes wrong:** `Ensemble::new(models).weights()` called before `.fit()` returns uniform `1/k` regardless of method.

**Why it happens:** [VERIFIED: model.rs:172-186] The constructor always sets `weights = vec![1.0 / n as f64; n]`. Weights are only updated in `.fit()`.

**How to avoid:** The introspection functions always call `.fit(&ts)` before `.weights()`. The C++ ScalarFunction should enforce that values.len() >= 3 before calling the FFI.

### Pitfall 3: HorizonAdaptive returns per-step weights MATRIX — `weights()` is the average

**What goes wrong:** For `combination_method := 'horizon_adaptive'`, the user expects `ts_ensemble_inspect_by` to return per-step weights. The `Ensemble::weights()` scalar accessor [VERIFIED: model.rs:202-204] returns the AVERAGE weight across horizon steps, not the per-step matrix.

**Why it happens:** [VERIFIED: model.rs:550-569] `compute_horizon_adaptive_weights` stores the average of per-horizon weight vectors in `self.weights`; the full per-step matrix is in `self.horizon_weights`. The `horizon_weights()` accessor [VERIFIED: model.rs:207-209] returns `Option<&Vec<Vec<f64>>>`, but this is a 2D structure — complex to surface in SQL long format.

**Decision:** Introspection for HorizonAdaptive returns `Ensemble::weights()` (the average scalar weights). The per-step weight matrix is out of scope for INSP-01 (too complex to represent in the long-format (group, member, weight) schema). Document this in the function's docstring and the reference doc.

### Pitfall 4: AutoEnsemble candidates — `all_scores()` contains ALL fitted candidates, not just selected

**What goes wrong:** `AutoEnsemble.all_scores()` returns ALL candidates (AutoARIMA, AutoETS, AutoTheta — up to 3 in the current crate), sorted ascending by MSE [VERIFIED: auto.rs:111-112, 197-201]. The selected top-K are the first `model_count()` entries. If `top_k=3` and all three candidates fit successfully, `all_scores().len() == 3 == model_count()`. But if one candidate fails to fit (e.g., AutoARIMA convergence failure), `all_scores().len()` may be 2 and `model_count()` will be min(2, top_k).

**How to avoid:** The `inspect_auto_ensemble` function correctly takes `all_scores().iter().take(model_count())` — only the selected entries. Never take `all_scores()` unchecked.

**Warning sign:** If `model_count() < top_k`, the returned row count < top_k is normal and expected. Document this.

### Pitfall 5: CMakeLists.txt — new .cpp must be added to EXTENSION_SOURCES

**What goes wrong:** If `ts_ensemble_inspect_native.cpp` is not added to `EXTENSION_SOURCES`, it does not compile into the extension. The extension builds successfully but `ts_ensemble_inspect_by` is absent at runtime.

**How to avoid:** Add to `EXTENSION_SOURCES` BEFORE the first build [VERIFIED: CMakeLists.txt:162-210 — explicit list, no glob].

**Warning sign:** `ts_ensemble_inspect_by(...)` throws "Function _ts_ensemble_inspect_native not found".

### Pitfall 6: `make header` after adding new FFI exports

**What goes wrong:** `EnsembleInspectResult`, `anofox_ts_ensemble_inspect`, `anofox_ts_auto_ensemble_inspect`, and `anofox_free_ensemble_inspect_result` are not visible to C++ until `make header` regenerates `src/include/anofox_fcst_ffi.h`.

**How to avoid:** Run `make header` after editing `lib.rs` (FFI). Verify all four symbols appear in the regenerated header.

### Pitfall 7: EPI-01 AutoEnsemble residuals — `yhat_lower`/`yhat_upper` in `ts_cv_forecast_by` output are NULL

**What goes wrong:** The user expects `ts_cv_forecast_by` with `'AutoEnsemble'` to provide interval columns for the backtest. The CI-skip guard [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:792-794] emits `lower=[], upper=[]` for AutoEnsemble, so the backtest output has NULL `yhat_lower`/`yhat_upper`. This is correct — the EPI-01 recipe ignores those and uses `ts_conformal_calibrate` on the backtest residuals (actual - yhat).

**How to avoid:** In the conformal recipe, use `y` and `yhat` columns from the CV output — NOT `yhat_lower`/`yhat_upper`. The conformal quantile is learned from point forecast residuals.

### Pitfall 8: Conformal `ts_conformal_calibrate` needs `group_col` for per-series calibration vs global

**What goes wrong:** The `ts_conformal_calibrate` macro [VERIFIED: src/macros/ts_macros.cpp:1618-1644] does NOT have a `group_col` parameter — it operates globally across all residuals in the backtest table, producing a SINGLE conformity score. If the series have different variance profiles, a global quantile may over- or under-cover for individual series.

**Impact for EPI-01:** The example uses a global quantile (acceptable for a demonstration). For per-series calibration, users should use `ts_conformal_by` which groups by `group_col`. Document this distinction in the example.

---

## Code Examples

### EPI-01 AutoEnsemble — Runnable Pattern

```sql
-- Source: verified pattern from ts_macros.cpp conformal surface
-- (ts_cv_forecast_by + ts_conformal_calibrate + ts_conformal_apply_by)

-- Setup: synthetic series
CREATE OR REPLACE TABLE series AS
SELECT i AS id, (TIMESTAMP '2020-01-01' + INTERVAL (row_number() OVER ()) DAY) AS ds,
       10.0 + row_number() OVER () * 0.5 + random() * 2 AS y
FROM range(60) t(i);

-- 1. Folds
CREATE OR REPLACE TABLE folds AS
SELECT * FROM ts_cv_folds_by('series', id, ds, y, 3, 5, MAP{});

-- 2. Backtest with AutoEnsemble (top_k=3, Mean — CV native default)
CREATE OR REPLACE TABLE bt AS
SELECT * FROM ts_cv_forecast_by('folds', id, ds, y, 'AutoEnsemble', MAP{});

-- 3. Calibrate (global quantile across all series/folds)
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('bt', y, yhat, {alpha: 0.1});

-- 4. Final forecast
CREATE OR REPLACE TABLE fcst AS
SELECT * FROM ts_forecast_by('series', id, ds, y, 'AutoEnsemble', 5, '1d');

-- 5. Apply intervals
SELECT f.id, f.ds, f.forecast_step, f.yhat,
       f.yhat - c.conformity_score AS yhat_lower,
       f.yhat + c.conformity_score AS yhat_upper
FROM fcst f CROSS JOIN calib c;

-- Verification: lower <= yhat <= upper (non-degenerate)
-- SELECT count(*) FROM above WHERE yhat_lower > yhat OR yhat > yhat_upper;
-- → must return 0
```

### INSP-01 — Runnable Pattern (after Phase 6 ships)

```sql
-- Explicit-member: inspect weights after fitting on series
SELECT * FROM ts_ensemble_inspect_by('series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse', seasonal_period := 0);
-- Returns: (id, member_name, weight, score)  — weight sums to 1.0 per group

-- DoD assertion: weights sum to 1.0
SELECT id, SUM(weight) AS w_sum FROM ts_ensemble_inspect_by(...)
GROUP BY id HAVING abs(w_sum - 1.0) > 1e-6;
-- Must return 0 rows

-- AutoEnsemble: inspect selected members + MSE scores
SELECT * FROM ts_auto_ensemble_inspect_by('series', id, ds, y,
    top_k := 3, combination_method := 'mean', seasonal_period := 0);
-- Returns: (id, member_name, weight=1/k, score=MSE)

-- AutoEnsemble with WeightedMSE: weight is NULL (crate limitation documented)
SELECT * FROM ts_auto_ensemble_inspect_by('series', id, ds, y,
    top_k := 3, combination_method := 'weighted_mse', seasonal_period := 0);
-- Returns: (id, member_name, weight=NULL, score=MSE)
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Backtest auto function (`ts_backtest_auto_by`) | Two-step CV: `ts_cv_folds_by` → `ts_cv_forecast_by` | v0.7.x | **Removed** — use the two-step workflow |
| Per-series metric `_by` table macros | Scalar + GROUP BY | Current | Deprecated; 2400× slower; use `ts_mae(LIST(...))` etc. |

**Deprecated/outdated:**
- `ts_backtest_auto_by`: REMOVED — use `ts_cv_folds_by` + `ts_cv_forecast_by` (SKILL.md verified)
- Ensemble interval via `Ensemble::predict_with_intervals()`: The crate does expose `predict_with_intervals()` for `Ensemble` (widest-envelope: min(lowers), max(uppers) across members), but this is NOT the EPI-01 path. EPI-01 uses conformal, which is distribution-free and more reliable.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `fill_nulls_interpolate(values)` is the correct function to interpolate &[Option<f64>] — matches `forecast_explicit_ensemble` | Rust core code sketch | Compile error; trivially fixable by reading forecast_explicit_ensemble |
| A2 | `make_timeseries(&clean_values)` accepts `&[f64]` (not `&[Option<f64>]`) — matches the `forecast_auto_ensemble` path | Rust core code sketch | Compile error; fix by passing a concrete `Vec<f64>` |
| A3 | `CombinationMethod::Mean` is accessible via `anofox_forecast::models::ensemble::CombinationMethod` (re-exported from the private `model` sub-module) — same import path fixed in Phase 4 SUMMARY | FFI weight decision | Compile error; already fixed in Phase 4: use `anofox_forecast::models::ensemble::CombinationMethod` |
| A4 | `EnsembleInspectResult` can be `#[repr(C)]` with raw pointer members — standard FFI struct pattern | FFI design | If Rust rejects the layout, use a different ownership model; likely trivially fixable |

---

## Open Questions

1. **`Ensemble::predict_with_intervals()` for explicit-member ensemble — should EPI-01 mention it?**
   - What we know: `Ensemble::predict_with_intervals(horizon, level)` is public [VERIFIED: model.rs per Phase 5 SUMMARY note] and returns widest-envelope bounds (min of lowers, max of uppers).
   - What's unclear: Whether the reference doc should mention this as a model-native alternative to conformal.
   - Recommendation: Mention it briefly in the doc as "Ensemble::predict_with_intervals() returns a widest-envelope bound (model-native). For distribution-free, coverage-guaranteed intervals, use the conformal path shown here." This adds value without new code.

2. **`ts_conformal_by` vs `ts_conformal_calibrate` + `ts_conformal_apply_by` for the example**
   - What we know: Both work for EPI-01. `ts_conformal_by` is one-step (calibrate + apply from backtest). `ts_conformal_calibrate` + `ts_conformal_apply_by` is modular (reuse calibration across multiple forecast rounds).
   - Recommendation: Use the modular approach for the EPI-01 example — it's more educational and matches the pattern in the SKILL.md "Full backtest → conformal pipeline."

3. **Whether the rank column (1..k by ascending MSE) is needed alongside score**
   - What we know: `all_scores()` returns sorted ascending by MSE [VERIFIED: auto.rs:198]. The rank is implicit from position.
   - Recommendation: Include a `rank` column (derived as `ROW_NUMBER() OVER (PARTITION BY group ORDER BY score)`) in the macro output — cheap, useful for filtering "top-1 member", and makes the output self-documenting. No FFI changes needed; the macro CTE can add it.

---

## Environment Availability

Step 2.6: SKIPPED (no new external dependencies — all needed APIs are in `anofox-forecast` 0.15.3, already a dependency).

---

## Validation Architecture

Per-project convention: no Nyquist/pytest framework. DoD = runnable `examples/*.sql` verified against the built extension.

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | File |
|--------|----------|-----------|------|
| EPI-01 | AutoEnsemble backtest via `ts_cv_forecast_by` → conformal → non-degenerate lower/upper | SQL e2e | `examples/forecasting/ensemble_intervals.sql` Section 1 |
| EPI-01 | Explicit-member ensemble manual-fold conformal → non-degenerate lower/upper | SQL e2e | `examples/forecasting/ensemble_intervals.sql` Section 2 |
| EPI-01 | `lower <= yhat <= upper` for all horizon steps | SQL assertion | Both sections, assertion query |
| INSP-01 | Explicit-member Mean: weights equal (1/k), sum to 1 | SQL assertion | `examples/forecasting/ensemble_inspect.sql` Section 1 |
| INSP-01 | Explicit-member WeightedMSE: weights positive, sum to 1 | SQL assertion | `examples/forecasting/ensemble_inspect.sql` Section 2 |
| INSP-01 | AutoEnsemble (Mean): weight=1/k, score=MSE, non-NULL | SQL assertion | `examples/forecasting/ensemble_inspect.sql` Section 3 |
| INSP-01 | AutoEnsemble (WeightedMSE): weight=NULL, score=MSE | SQL assertion | `examples/forecasting/ensemble_inspect.sql` Section 4 |

---

## Security Domain

No new auth, session, or cryptography concerns. All inputs are validated:
- Member names via whitelist (`ModelType::from_str` — 36 known names).
- `top_k` clamped in `AutoEnsembleConfig::with_top_k(k.max(1))`.
- Null-delimited buffer uses `members_buf_len` bound (T-05-01 pattern reused).
- New `EnsembleInspectResult` must have a companion `anofox_free_ensemble_inspect_result` to avoid memory leaks.

---

## Sources

### Primary (HIGH confidence — source files read this session)

- `crates/anofox-fcst-core/src/forecast.rs` (lines 2560-2581, 2813-2871, 777-781) — `forecast_auto_ensemble`, `forecast_explicit_ensemble`, `AutoEnsemble` dispatch with `top_k==0 → 3` fallback [VERIFIED]
- `~/.cargo/registry/.../anofox-forecast-0.15.3/src/models/ensemble/model.rs` (lines 152-219, 262-624, 575-646) — `Ensemble` struct fields, `weights()`, `horizon_weights()`, `method()`, `model_count()` accessors, `Ensemble::new()` weight initialization, `fit()` weight computation for all 6 methods [VERIFIED]
- `~/.cargo/registry/.../anofox-forecast-0.15.3/src/models/ensemble/auto.rs` (lines 16-126, 134-221) — `AutoEnsembleConfig::default()` (WeightedMSE), `AutoEnsemble::all_scores()` signature, `AutoEnsemble::model_count()`, `fit()` stores scores in ascending MSE order [VERIFIED]
- `src/table_functions/ts_cv_forecast_native.cpp` (lines 380-388, 667-715) — CV native parses only a subset of params, does NOT parse `top_k`/`combination_method`; calls `anofox_ts_forecast` with zeroed ensemble fields [VERIFIED]
- `src/macros/ts_macros.cpp` (lines 596-632, 827-855, 1554-1644) — `ts_forecast_ensemble_by` macro shape, `ts_cv_forecast_by` macro shape, conformal macro shapes [VERIFIED]
- `src/table_functions/ts_forecast_ensemble_native.cpp` (lines 145-460) — ScalarFunction shape, member extraction pattern, null-delimited buffer construction, `RegisterTsForecastEnsembleNativeFunction` [VERIFIED]
- `CMakeLists.txt` (lines 162-210) — explicit EXTENSION_SOURCES list with `ts_forecast_ensemble_native.cpp` [VERIFIED]
- `.planning/phases/05-explicit-member-ensemble/05-01-SUMMARY.md` — confirmed Phase 5 shipped: `ScalarFunction` dispatch, header in `src/include/`, inline date helpers, `Ensemble implements Forecaster` at model.rs:575 [VERIFIED]
- `.planning/phases/05-explicit-member-ensemble/05-02-SUMMARY.md` — confirmed Phase 5-02 complete DoD, PR #230 doc verification rule [VERIFIED]
- `.planning/phases/04-autoensemble-surface-combination-methods/04-01-SUMMARY.md` — confirmed `parse_combination_method`, `forecast_auto_ensemble`, `ForecastOptions` ensemble fields, `CombinationMethod` re-exported from `anofox_forecast::models::ensemble` [VERIFIED]

---

## RESEARCH COMPLETE
