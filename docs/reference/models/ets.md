# ETS

> Error-Trend-Seasonal state space model with explicit specification

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ETS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ETS', horizon, params);
```

## Description

Error-Trend-Seasonal State Space Model with explicit specification. Unlike AutoETS, you specify the exact model structure using a 3-4 character code (e.g., 'AAA', 'MNM', 'AAdN').

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `model` | VARCHAR | No | 'AAA' | ETS model specification (see below) |
| `seasonal_period` | INTEGER | **Yes*** | — | Seasonal period (required for seasonal models) |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## ETS Model Specification

Format: `[Error][Trend][Seasonal]` or `[Error][Trend][Damped][Seasonal]`

| Position | Component | Values |
|----------|-----------|--------|
| 1st | Error | `A` (Additive), `M` (Multiplicative) |
| 2nd | Trend | `N` (None), `A` (Additive), `M` (Multiplicative) |
| 3rd (optional) | Damped | `d` (damped trend) |
| Last | Seasonal | `N` (None), `A` (Additive), `M` (Multiplicative) |

*Required when using seasonal models (AAA, MAM, etc.). Not needed for non-seasonal models (ANN, AAN, AAdN).

**Common Models:**

| Spec | Name | Use Case |
|------|------|----------|
| `ANN` | Simple Exponential Smoothing | No trend, no seasonality |
| `AAN` | Holt's Linear Method | Trend, no seasonality |
| `AAA` | Additive Holt-Winters | Trend + additive seasonality |
| `MAM` | Multiplicative Holt-Winters | Trend + multiplicative seasonality |
| `AAdN` | Damped Trend | Dampening trend, no seasonality |
| `MAdM` | Damped Multiplicative | Damped trend + multiplicative seasonal |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower prediction interval |
| `upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Additive Holt-Winters (default)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'ETS',
    14,
    {'seasonal_period': 7}
);

-- Multiplicative Holt-Winters
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'ETS',
    12,
    {'model': 'MAM', 'seasonal_period': 12}
);

-- Damped trend, no seasonality
SELECT * FROM ts_forecast_by(
    'quarterly_sales',
    region_id,
    date,
    sales,
    'ETS',
    4,
    {'model': 'AAdN'}
);
```

## Best For

- When you know the exact model structure needed
- Multiplicative seasonality (seasonal amplitude proportional to level)
- Damped trends that taper off over time
- Expert users who understand ETS taxonomy
