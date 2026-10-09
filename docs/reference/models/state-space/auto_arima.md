# AutoARIMA

> Automatic ARIMA model selection with optional exogenous variables

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoARIMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoARIMA', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'AutoARIMA', horizon, params);
```

## Description

Automatic ARIMA (AutoRegressive Integrated Moving Average) model selection. Automatically determines the optimal (p, d, q) orders and seasonal components. Supports exogenous variables (ARIMAX) for incorporating external regressors.

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`,
> using the first detected period; 1 if none found). Passing `seasonal_period` explicitly via
> `ts_detect_periods_by` is still recommended for control and reproducibility — `ts_forecast_agg`
> does NOT auto-detect.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period (e.g., 7 for weekly, 12 for monthly); auto-detected when omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are currently a heuristic `point ± z·SD(history)·√h` band, not a
model-based interval and not statistically calibrated. For calibrated coverage, use conformal
prediction intervals (see `docs/api/11-conformal-prediction.md`) on top of the point forecast.
(being corrected in v0.10.0, Phase 10)

## SQL Example

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('daily_sales', product_id, date, quantity, MAP{});

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoARIMA',
    12,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- With exogenous variables (ARIMAX)
SELECT * FROM ts_forecast_exog_by(
    'sales',
    product_id,
    date,
    amount,
    ['temperature', 'promotion'],
    'future_exog',
    date,
    ['temperature', 'promotion'],
    '1d',
    'AutoARIMA',
    12
);
```

## Best For

- Time series with complex autocorrelation structures
- When you need to incorporate external regressors (promotions, weather, etc.)
- Stationary or differenced stationary data
- Alternative to ETS when data shows ARIMA-like patterns
