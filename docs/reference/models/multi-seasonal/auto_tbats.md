# AutoTBATS

> AutoTBATS with automatic model selection for complex seasonality

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoTBATS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoTBATS', horizon, frequency, params);
```

## Description

TBATS (Trigonometric seasonality, Box-Cox transformation, ARMA errors, Trend and Seasonal components) with automatic model selection. A sophisticated model for complex seasonal patterns using trigonometric representation of seasonality.

> **Important:** Seasonality is NOT auto-detected. Use `ts_detect_periods_by` first to detect seasonal periods, then pass them explicitly via `seasonal_periods`.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | **Yes** | — | Array of seasonal periods (e.g., `'[24, 168]'` for daily+weekly) |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('hourly_demand', region_id, timestamp, demand, MAP{});
-- Returns: periods = [24, 168] (daily and weekly)

-- Step 2: Forecast with detected seasonal periods
SELECT * FROM ts_forecast_by(
    'hourly_demand',
    region_id,
    timestamp,
    demand,
    'AutoTBATS',
    168,
    '1h',
    {'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- When MSTL doesn't capture the seasonality well
- Data requiring Box-Cox transformation for variance stabilization
