# ARIMA

> AutoRegressive Integrated Moving Average with explicit order specification

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ARIMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ARIMA', horizon, params);
```

## Description

AutoRegressive Integrated Moving Average with explicit order specification. Unlike AutoARIMA, you specify the exact (p, d, q) orders. For seasonal ARIMA, also specify (P, D, Q, s).

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `p` | INTEGER | **Yes** | — | AR (autoregressive) order |
| `d` | INTEGER | **Yes** | — | Differencing order |
| `q` | INTEGER | **Yes** | — | MA (moving average) order |
| `P` | INTEGER | No | 0 | Seasonal AR order |
| `D` | INTEGER | No | 0 | Seasonal differencing order |
| `Q` | INTEGER | No | 0 | Seasonal MA order |
| `s` | INTEGER | No | 0 | Seasonal period |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## ARIMA Order Selection Guide

| Pattern | Suggested Order | Description |
|---------|-----------------|-------------|
| Random walk | (0,1,0) | First difference only |
| AR(1) | (1,0,0) | Single lag autocorrelation |
| MA(1) | (0,0,1) | Single lag moving average |
| ARIMA(1,1,1) | (1,1,1) | Common general-purpose model |
| Seasonal | (1,1,1)(1,1,1)s | With seasonal components |

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
-- Simple ARIMA(1,1,1)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'ARIMA',
    14,
    {'p': 1, 'd': 1, 'q': 1}
);

-- Seasonal ARIMA(1,1,1)(1,1,1)7
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'ARIMA',
    28,
    {'p': 1, 'd': 1, 'q': 1, 'P': 1, 'D': 1, 'Q': 1, 's': 7}
);
```

## Best For

- Expert users who understand ARIMA methodology
- When you have prior knowledge of the appropriate orders
- Box-Jenkins methodology practitioners
- Comparison with AutoARIMA results
