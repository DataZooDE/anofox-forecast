# SESOptimized

> Simple Exponential Smoothing with automatically optimized alpha parameter

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SESOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SESOptimized', horizon, frequency, params);
```

## Description

Optimized Simple Exponential Smoothing. Same as SES but automatically finds the optimal alpha value that minimizes forecast error on historical data.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the model's own prediction interval (crate
`predict_with_intervals`) at `confidence_level` (default 0.90), widening with `h` per
Hyndman's class-1 `ETS(A,N,N)` analytic variance since anofox-forecast 0.16.0. See
[Prediction intervals, fitted values and input handling](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling);
for calibrated coverage use the conformal functions.

**R `forecast` parity (FCST-09, Phase 17 plan 17-03):** `SESOptimized` (= `ETS 'ANN'`)
matches R `ses()` within 1e-6 relative on AirPassengers, the seeded random walk and the
random-walk-with-drift series. On a zero-inflated intermittent-demand series it is a known
gap: the crate's own SES optimiser converges to a different `(alpha, l0)` than R's — root
cause `RC-04` (`validation/reference/README.md`'s "R-parity gap root causes" section),
with an upstream issue drafted and not yet filed.

## SQL Example

```sql
-- Basic usage (alpha automatically optimized)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SESOptimized',
    14,
    '1d',
    MAP{}
);

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SESOptimized',
    8,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Data with no trend or seasonality
- When you don't want to manually tune the alpha parameter
- Slightly better accuracy than fixed-alpha SES
- Production forecasting where manual tuning is impractical
