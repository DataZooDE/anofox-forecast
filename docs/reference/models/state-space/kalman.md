# Kalman Filter

> State-space smoothing and h-step ahead forecasting via Kalman filter

## Signature

```sql
-- Local level (default) — random walk + noise
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'Kalman', horizon, frequency
);

-- Local linear trend — level + trend state-space
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'Kalman', horizon, frequency,
    params := MAP{'kalman_model': 'local_linear_trend'}
);
```

## Description

`KalmanForecaster` applies a linear Kalman filter to a univariate time series and produces
h-step ahead point forecasts. Two structural state-space specifications are supported:

| Spec | Key | When to Use |
|------|-----|-------------|
| Local level | `'local_level'` (default) | Series with no clear trend; random walk + measurement noise |
| Local linear trend | `'local_linear_trend'` | Trended series; level + slope state, both evolve over time |

**Output is h-step ahead forecasts** (not in-sample fitted values). The filter runs on the
historical data and the final state is projected forward `horizon` steps.

## State-Space Models

### Local Level

```
Observation: y_t = μ_t + ε_t,   ε_t ~ N(0, σ_obs²)
State:        μ_t = μ_{t-1} + η_t,  η_t ~ N(0, σ_level²)
```

The optimal h-step forecast under local level is a **flat line** at the filtered level μ_T.

### Local Linear Trend

```
Observation: y_t = μ_t + ε_t
Level:       μ_t = μ_{t-1} + ν_{t-1} + η_t
Slope:       ν_t = ν_{t-1} + ζ_t
```

Forecast is **linear** (level + slope × h), growing or shrinking as captured in the filtered slope ν_T.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step between observations (e.g., `'1d'`, `'1h'`) |
| `kalman_model` | VARCHAR | No | `'local_level'` | State-space spec: `'local_level'` or `'local_linear_trend'` |

The `kalman_model` param is passed via the `params` MAP:
```sql
params := MAP{'kalman_model': 'local_linear_trend'}
```

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast (h-step ahead) |
| `model_name` | VARCHAR | `'Kalman'` |

Note: `yhat_lower` and `yhat_upper` are not available for Kalman in v1 (prediction intervals deferred).

## SQL Examples (verified end-to-end)

### Local Level (default)

```sql
-- Kalman local_level — flat forecast at the filtered level
CREATE OR REPLACE TABLE sales AS
    SELECT 'Product_X' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.8 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM range(30) t(i);

SELECT
    product_id,
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 7, '1d')
ORDER BY product_id, forecast_step;
```

### Local Linear Trend

```sql
-- Kalman local_linear_trend — forecasts capture the trend slope
SELECT
    product_id,
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'Kalman', 7, '1d',
    params := MAP{'kalman_model': 'local_linear_trend'}
)
ORDER BY product_id, forecast_step;
```

Expected: `local_level` produces a flat forecast; `local_linear_trend` produces an
increasing or decreasing sequence capturing the trend direction.

## Choosing Between Specs

| Condition | Recommended Spec |
|-----------|-----------------|
| No visible trend; series oscillates around a level | `local_level` |
| Clear upward or downward trend | `local_linear_trend` |
| Uncertain | Run both; compare MSE on a held-out window |

## Model Details

- **Fixed variance params:** `obs_var = 1.0`, `level_var = 0.1` (not MLE-estimated in v1).
  This simplification produces reasonable forecasts but may not minimize MSE for all series.
  statsmodels `UnobservedComponents` with `disp=False` estimates these via MLE for comparison.
- **Minimum observations:** No hard minimum beyond 1; but fewer than 10 observations produce
  unreliable filtered states.
- **No seasonality support:** Kalman does not model seasonal components. For seasonal series,
  use MSTL or ETS instead.

## Benchmark

Behavioral parity confirmed against `statsmodels.tsa.statespace.structural.UnobservedComponents`:
- Local level: mean forecast ratio (anofox/statsmodels): **1.000** on 50 M4 Daily series (PASS)
- Local linear trend: mean forecast ratio: **0.992** (PASS)
- Target: 0.5–2.0; exact match not expected (anofox uses fixed variance; statsmodels uses MLE)

See `benchmark/m4/kalman_benchmark/` for committed results.

## Reference

- Kalman (1960), "A New Approach to Linear Filtering and Prediction Problems"
- Harvey (1990), "Forecasting, Structural Time Series Models and the Kalman Filter"
