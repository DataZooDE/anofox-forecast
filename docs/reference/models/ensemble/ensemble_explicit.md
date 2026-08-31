# Explicit-Member Ensemble (`ts_forecast_ensemble_by`)

> User-named ensemble: specify exactly which member models to fit and how to combine them

## Signature

```sql
-- Default: combination_method='mean', seasonal_period=0 (non-seasonal)
SELECT * FROM ts_forecast_ensemble_by(
    'table', group_col, date_col, value_col,
    members,        -- VARCHAR[] of model names, e.g. ['AutoARIMA','AutoETS','Theta']
    horizon,        -- INTEGER: periods to forecast
    frequency       -- VARCHAR: time step, e.g. '1d', '1mo'
);

-- With explicit combination method and seasonal period
SELECT * FROM ts_forecast_ensemble_by(
    'table', group_col, date_col, value_col,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    12, '1mo',
    combination_method := 'weighted_mse',
    seasonal_period := 12
);
```

## Description

`ts_forecast_ensemble_by` fits each named member model independently on every series (via
DuckDB `GROUP BY`) and combines the out-of-sample forecasts using the specified
`combination_method`. Unlike `AutoEnsemble` — which fixes the candidates to
AutoARIMA / AutoETS / AutoTheta and auto-selects by in-sample MSE — the explicit-member
form gives you full control over which models contribute to the blend.

Each series call: for every group, the function fits all named members on the same training
series with the same `seasonal_period`, then combines their horizon-step forecasts using the
chosen method. If any member raises a construction error (e.g. a blocked model name or an
unknown name), the entire call fails with a descriptive `InvalidParameter` error naming the
offending member.

### Minimum requirements

- **At least 2 members** — an ensemble of one is degenerate and raises `InvalidParameter`.
- **Duplicate members are allowed** — e.g. `['AutoARIMA', 'AutoARIMA', 'AutoETS']` weights
  `AutoARIMA` twice. The crate handles duplicates and applies method weights per instance.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `source` | VARCHAR | Yes | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Yes | — | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Yes | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Yes | — | Target value column (unquoted) |
| `members` | VARCHAR[] | Yes | — | List of 2+ model names, e.g. `['AutoARIMA','AutoETS']` |
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step between observations (e.g., `'1d'`, `'1mo'`) |
| `combination_method` | VARCHAR | No | `''` (= `'mean'`) | How to blend member forecasts (see table below) |
| `seasonal_period` | INTEGER | No | `0` | Seasonal period shared by all members; `0` = non-seasonal; `p > 1` = seasonal |

### combination_method strings

| SQL string(s) | Combination strategy |
|---|---|
| `''` (empty), `'mean'` | Unweighted arithmetic mean of member forecasts (default) |
| `'median'` | Coordinate-wise median of member forecasts; robust to outlier members |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weighting; members with lower in-sample MSE receive higher weight |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weighting; rewards parsimony alongside fit quality |
| `'stacking'`, `'stack'` | Ridge-regularised stacking; weights fitted from in-sample holdout residuals |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-horizon weights estimated from rolling-origin errors; each step gets independent weights |

All strings are case-insensitive. The string `'custom'` is **not** accepted (ENS-F1, deferred).
Passing an unrecognised string raises `InvalidParameter`.

### seasonal_period behaviour

When `seasonal_period > 1`, all named members that accept a period are configured with that
value. When `seasonal_period = 0` (the default), non-seasonal constructors are used for
`Auto*` and baseline models. **Seasonal models** (`SeasonalNaive`, `HoltWinters`,
`SeasonalES`, `SeasonalESOptimized`, `SeasonalWindowAverage`) require `seasonal_period > 1`
to behave seasonally — when `seasonal_period = 0` they fall back to `period = 12` internally.
Always pass an explicit `seasonal_period > 1` when using seasonal members.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `forecast_step` | INTEGER | Forecast horizon step (1, 2, …, horizon) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast (combined from all named members) |
| `yhat_lower` | DOUBLE | `NULL` in Phase 5 (see Limitations) |
| `yhat_upper` | DOUBLE | `NULL` in Phase 5 (see Limitations) |
| `model_name` | VARCHAR | `'Ensemble'` |

## Supported Members (26 models)

The `members` list accepts any name from the three tiers below. These are the same
model-name strings used in `ts_forecast_by` (`method` parameter).

### Tier 1 — Auto-selection candidates (best for robust ensembles)

| Member name | Description |
|-------------|-------------|
| `AutoARIMA` | Auto-selected ARIMA order |
| `AutoETS` | Auto-selected ETS specification |
| `AutoTheta` | Auto-selected Theta variant |

### Tier 2 — Seasonal-aware models

| Member name | Description | Notes |
|-------------|-------------|-------|
| `Theta` | Standard Theta method | `seasonal_period` optional |
| `OptimizedTheta` | Optimized Theta | `seasonal_period` optional |
| `DynamicTheta` | Dynamic Theta | `seasonal_period` optional |
| `DynamicOptimizedTheta` | Dynamic Optimized Theta (DOTM) | `seasonal_period` optional |
| `HoltWinters` | Holt-Winters seasonal smoothing | Falls back to `period=12` if `seasonal_period=0` |
| `SeasonalES` | Seasonal Exponential Smoothing | Falls back to `period=12` if `seasonal_period=0` |
| `SeasonalESOptimized` | Optimized Seasonal ES | Falls back to `period=12` if `seasonal_period=0` |
| `SeasonalNaive` | Last-season-repeated baseline | Falls back to `period=12` if `seasonal_period=0` |
| `SeasonalWindowAverage` | Seasonal window average | Falls back to `period=12` if `seasonal_period=0` |
| `ETS` | Error-Trend-Seasonal model | `seasonal_period` optional |
| `Kalman` | Local-level Kalman smoother | No period |

### Tier 3 — Non-seasonal baselines (always valid)

| Member name | Description |
|-------------|-------------|
| `Naive` | Last-value repeated |
| `SES` | Simple Exponential Smoothing (alpha=0.3) |
| `SESOptimized` | Optimized SES |
| `RandomWalkDrift` | Random walk with drift |
| `Holt` | Holt's linear trend method |
| `SMA` | Simple Moving Average (window=5) |
| `CrostonClassic` | Croston's intermittent-demand method |
| `CrostonOptimized` | Optimized Croston |
| `CrostonSBA` | Syntetos-Boylan Approximation Croston |
| `ADIDA` | Aggregate-Disaggregate IDA |
| `IMAPA` | Intermittent Multiple Aggregation Prediction Approach |
| `TSB` | Teunter-Syntetos-Babai method |

## Blocked Members (10 models — raise InvalidParameter)

The following model names from `ts_forecast_by` are **not** accepted in a `members` list.
Each raises `InvalidParameter` naming the offending model and suggesting an alternative.

| Blocked name | Reason | Suggested alternative |
|---|---|---|
| `GARCH` | `GARCH.predict()` returns simulated innovations, not level forecasts | Use `AutoARIMA` or `AutoETS` |
| `Laplace` | Variant-dependent construction; not expressible as a single shared-period instance | Use `AutoARIMA`, `AutoETS`, or `AutoTheta` |
| `ARIMA` | Requires explicit `(p, d, q)` parameters not compatible with shared `seasonal_period` | Use `AutoARIMA` |
| `MFLES` | Requires `seasonal_periods[]` array; single period not sufficient | Use `AutoETS` or `ETS` |
| `AutoMFLES` | Same — multi-seasonal array input required | Use `AutoETS` |
| `MSTL` | Requires `seasonal_periods[]` array | Use `AutoETS` or `ETS` |
| `AutoMSTL` | Same — multi-seasonal array input required | Use `AutoETS` |
| `TBATS` | Requires `seasonal_periods[]` array | Use `AutoETS` |
| `AutoTBATS` | Same — multi-seasonal array input required | Use `AutoETS` |
| `AutoEnsemble` | Nested ensemble of ensemble — not supported in v1 | Use explicit member names directly |

## SQL Examples (verified end-to-end)

### Canonical Mean cross-check (DoD invariant)

The following example verifies that `ts_forecast_ensemble_by([...], 'mean')` produces exactly
the arithmetic mean of each member's independent `ts_forecast_by` forecast (within 1e-6).
All values are verified against the built extension.

```sql
-- 60-observation linear series (deterministic Auto* model selection)
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- Ensemble with mean combination (canonical ENS-02 member set)
CREATE OR REPLACE TABLE ens_result AS
SELECT * FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := 'mean',
    seasonal_period := 0
);

SELECT * FROM ens_result ORDER BY id, forecast_step;
```

Expected output: `yhat_lower` and `yhat_upper` are `NULL` (point forecasts only in Phase 5);
`model_name` is `'Ensemble'`.

### Six combination methods

All six `combination_method` strings produce finite non-NULL `yhat` (verified):

```sql
-- mean — default unweighted arithmetic mean
SELECT forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'mean', seasonal_period := 0)
ORDER BY forecast_step;

-- median — robust to outlier members
SELECT forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'median', seasonal_period := 0)
ORDER BY forecast_step;

-- weighted_mse — inverse-MSE weighting
SELECT forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'weighted_mse', seasonal_period := 0)
ORDER BY forecast_step;

-- stacking — holdout-fitted weights
SELECT forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'stacking', seasonal_period := 0)
ORDER BY forecast_step;

-- horizon_adaptive — per-step weights
SELECT forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'horizon_adaptive', seasonal_period := 0)
ORDER BY forecast_step;
```

### Seasonal ensemble

Seasonal members require `seasonal_period > 1`:

```sql
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['SeasonalNaive', 'HoltWinters'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 12)
ORDER BY forecast_step;
```

### Intermittent-demand ensemble

Croston-family and IDA models work as ensemble members (non-seasonal):

```sql
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['CrostonClassic', 'ADIDA', 'IMAPA'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 0)
ORDER BY forecast_step;
```

## Choosing a combination_method

| When | Recommended method |
|------|--------------------|
| Starting out / unsure | `'mean'` (default) — simple, interpretable, rarely worst |
| Diverging members on volatile series | `'median'` — less sensitive to one outlier member |
| Members have clearly different fit quality | `'weighted_mse'` or `'inverse_aic'` — better members drive the forecast |
| Sufficient history for holdout calibration | `'stacking'` — weights fitted from in-sample residuals |
| Different accuracy at different forecast horizons | `'horizon_adaptive'` — each step gets independent weights |

## Error Examples

These statements raise `InvalidParameter` — shown here so you know what to expect.

**Unknown member name:**
```sql
-- Raises: "Invalid parameter 'members' = 'NotAModel': unknown model name 'NotAModel';
--          use the same names as ts_forecast_by"
SELECT * FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'NotAModel'], 3, '1d');
```

**Fewer than 2 members:**
```sql
-- Raises: "ts_forecast_ensemble_by: at least 2 members are required. Got 1."
SELECT * FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA'], 3, '1d');
```

**Blocked model (GARCH):**
```sql
-- Raises: "Invalid parameter 'members' = 'GARCH': GARCH is not supported as an
--          ensemble member: Forecaster::predict() returns simulated innovations,
--          not level forecasts; use AutoARIMA or AutoETS instead"
SELECT * FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'GARCH'], 3, '1d');
```

## Limitations (Phase 5)

- **Prediction intervals:** `yhat_lower` and `yhat_upper` are `NULL`. Ensemble conformal
  prediction intervals are planned for Phase 6 (EPI-01).
- **Member introspection:** Per-member forecasts, weights, and per-member MSE scores are not
  returned in Phase 5. Introspection is planned for Phase 6 (INSP-01).
- **Per-member parameters:** All members share the same `seasonal_period`. Per-member
  parameter maps (e.g., different windows for each SMA member) are deferred to a future
  milestone.
- **`'custom'` combination:** `CombinationMethod::Custom` (user-supplied weights) is not
  accepted. Passing `combination_method='custom'` raises `InvalidParameter` (ENS-F1, deferred).
- **Blocked models:** 10 model types from `ts_forecast_by` are not accepted as members (see
  Blocked Members table). Each raises `InvalidParameter` naming the model and suggesting an
  alternative.

## Internal-consistency invariant (Mean combination)

With `combination_method='mean'`, `ts_forecast_ensemble_by`'s point forecast equals the
arithmetic mean of each named member's independent `ts_forecast_by` forecast — provided all
members converge and the same `seasonal_period` is used. This invariant is verified in
`examples/forecasting/ensemble_explicit.sql` (Section 1) and holds to within floating-point
tolerance (< 1e-6).

## Reference

- `examples/forecasting/ensemble_explicit.sql` — full DoD example: Mean cross-check,
  six-method smoke test, member-allowlist sample, and error-path demonstrations
- [AutoEnsemble reference](autoensemble.md) — automatic candidate selection (AutoARIMA/AutoETS/AutoTheta)
- [07-forecasting.md](../../../api/07-forecasting.md) — top-level forecasting API reference
