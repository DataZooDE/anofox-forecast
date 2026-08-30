# AutoEnsemble

> Auto-fit ARIMA, ETS, and Theta; rank by in-sample MSE; combine the top-K members

## Signature

```sql
-- Default: top_k=3, combination_method='mean', non-seasonal
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'AutoEnsemble', horizon, frequency
);

-- With explicit ensemble params
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'AutoEnsemble', horizon, frequency,
    params := {top_k: 3, combination_method: 'median', seasonal_period: 7}
);
```

## Description

`AutoEnsemble` automatically fits three candidate forecasting models (AutoARIMA, AutoETS,
AutoTheta) on each series, ranks them by in-sample MSE (ascending), selects the top-K best
performers, and combines their out-of-sample forecasts using the specified
`combination_method`.

The selection and combination happen inside the crate — there is no SQL-level loop across
model calls. Each `ts_forecast_by('AutoEnsemble', ...)` call runs the full fit-rank-combine
pipeline per series, in parallel across series (via DuckDB GROUP BY).

### Candidate models and ranking

| Candidate | Selection criteria |
|-----------|--------------------|
| `AutoARIMA` | Auto-selected ARIMA order; ranked by in-sample MSE |
| `AutoETS` | Auto-selected ETS spec; ranked by in-sample MSE |
| `AutoTheta` | Auto-selected Theta variant; ranked by in-sample MSE |

After fitting all three, the candidates are sorted by MSE (ascending). The top-K (default 3)
are passed to the ensemble combiner. If fewer than K candidates converge, whatever fitted
successfully is used (`min(top_k, fitted_count)`). Only when **no** model fits does
`AutoEnsemble` raise a `ConvergenceFailure` exception.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step between observations (e.g., `'1d'`, `'1mo'`) |
| `top_k` | INTEGER | No | `3` | Number of top-ranked candidates to combine (≥1) |
| `combination_method` | VARCHAR | No | `'mean'` | How to blend member forecasts (see table below) |
| `seasonal_period` | INTEGER | No | `0` | Seasonal period shared across all three candidate models. `0` = non-seasonal; `p>1` = seasonal |

### combination_method strings

| SQL string(s) | Combination strategy |
|---|---|
| `''` (empty), `'mean'` | Unweighted arithmetic mean of member forecasts (default) |
| `'median'` | Coordinate-wise median of member forecasts; robust to outlier members |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weighting; members with lower in-sample MSE receive higher weight |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weighting; rewards parsimony alongside fit quality |
| `'stacking'`, `'stack'` | Ridge-regularised stacking; weights fitted from in-sample holdout residuals (2-fold, fixed; not user-configurable in v1) |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-horizon weights estimated from rolling-origin errors; each step gets independent weights |

All strings are case-insensitive. The string `'custom'` is **not** accepted in v1 (deferred,
ENS-F1). Passing an unrecognised string raises `InvalidParameter`.

### seasonal_period behaviour

When `seasonal_period > 1`, all three candidate models (AutoARIMA, AutoETS, AutoTheta) are
configured to consider that period. When `seasonal_period = 0` (the default), all three fit
non-seasonally. The same value is used for all candidates — there is no per-model period
configuration in Phase 4.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast (combined from top-K members) |
| `yhat_lower` | DOUBLE | `NULL` in Phase 4 (see Limitations) |
| `yhat_upper` | DOUBLE | `NULL` in Phase 4 (see Limitations) |
| `model_name` | VARCHAR | `'AutoEnsemble'` |

## SQL Examples (verified end-to-end)

### Default usage — mean combination, non-seasonal

```sql
-- 60-observation linear series for reliable convergence of all three members
CREATE OR REPLACE TABLE sales AS
SELECT
    'Product_A' AS product_id,
    '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
    10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

SELECT
    product_id,
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    yhat_lower,
    yhat_upper,
    model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'AutoEnsemble', 5, '1d')
ORDER BY product_id, forecast_step;
```

Expected output: `yhat_lower` and `yhat_upper` are `NULL` (point forecasts only in Phase 4);
`model_name` is `'AutoEnsemble'`.

### Median combination — robust to diverging members

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {combination_method: 'median', top_k: 3, seasonal_period: 0}
)
ORDER BY product_id, forecast_step;
```

### WeightedMSE combination — error-weighted blending

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {combination_method: 'weighted_mse', top_k: 3}
)
ORDER BY product_id, forecast_step;
```

### InverseAIC combination — parsimony-weighted blending

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {combination_method: 'inverse_aic', top_k: 3}
)
ORDER BY product_id, forecast_step;
```

### Stacking combination — holdout-fitted weights

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {combination_method: 'stacking', top_k: 3}
)
ORDER BY product_id, forecast_step;
```

### HorizonAdaptive combination — per-step weights

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {combination_method: 'horizon_adaptive', top_k: 3}
)
ORDER BY product_id, forecast_step;
```

### Seasonal ensemble — weekly data

```sql
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'weekly_sales', product_id, ds, y, 'AutoEnsemble', 8, '1w',
    params := {top_k: 3, combination_method: 'weighted_mse', seasonal_period: 52}
)
ORDER BY product_id, forecast_step;
```

## Choosing a combination_method

| When | Recommended method |
|------|--------------------|
| Starting out / unsure | `'mean'` (default) — simple, interpretable, rarely worst |
| Diverging members on skewed/volatile series | `'median'` — less sensitive to one outlier member |
| Members have clearly different fit quality | `'weighted_mse'` or `'inverse_aic'` — better members drive the forecast |
| Sufficient history for holdout calibration | `'stacking'` — weights fitted from in-sample residuals |
| Different accuracy at different forecast horizons | `'horizon_adaptive'` — each step gets independent weights |

## Limitations (Phase 4)

- **Prediction intervals:** `yhat_lower` and `yhat_upper` are `NULL`. Ensemble conformal
  prediction intervals are planned for Phase 6 (EPI-01).
- **Candidate families:** Fixed to AutoARIMA, AutoETS, AutoTheta. User-specified member lists
  (Phase 5, ENS-02) and explicit member ensembles are not yet available.
- **Stacking folds:** The number of cross-validation folds used by the stacking weight
  estimator is fixed internally and not exposed as a SQL parameter in v1.
- **Member introspection:** Weights, per-member forecasts, and per-member MSE scores are not
  returned in Phase 4. Introspection is planned for Phase 6 (INSP-01).
- **`'custom'` combination:** `CombinationMethod::Custom` (user-supplied weights) is not
  accepted. Passing `combination_method='custom'` raises `InvalidParameter` (ENS-F1, deferred).

## Internal-consistency invariant (Mean combination)

With `top_k=3` and `combination_method='mean'`, AutoEnsemble's point forecast equals the
arithmetic mean of the three independent member forecasts — provided all three Auto* models
converge and the same `seasonal_period` is used. This invariant is verified in
`examples/forecasting/autoensemble.sql` (Section 1) and holds to within floating-point
tolerance (< 1e-6).

## Reference

- Bates & Granger (1969), "The Combination of Forecasts" — theoretical foundation for Mean/Median ensemble
- Timmermann (2006), "Forecast Combinations" — survey of weighting methods (MSE, AIC, stacking)
- `examples/forecasting/autoensemble.sql` — runnable six-method smoke test and demonstrability proof
- [07-forecasting.md](../../../api/07-forecasting.md) — top-level forecasting API reference
