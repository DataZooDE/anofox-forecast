# GARCH

> Generalized Autoregressive Conditional Heteroskedasticity — conditional volatility forecasting

## IMPORTANT: forecast_value is VOLATILITY, not variance

`forecast_value` (and `yhat`) returned by GARCH is **conditional volatility (standard deviation)**
= `sqrt(forecast_variance(h))`, **NOT** the conditional variance.

This is the standard convention for financial risk applications: volatility (σ) in the same
units as the returns, not variance (σ²). If you need variance, square the output: `yhat * yhat`.

## Signature

```sql
-- Multiple series (grouped), GARCH(1,1) default
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'GARCH', horizon, frequency
);

-- GARCH with explicit p, q orders
SELECT * FROM ts_forecast_by(
    'table', group_col, date_col, value_col,
    'GARCH', horizon, frequency,
    params := MAP{'garch_p': '1', 'garch_q': '1'}
);
```

## Description

GARCH(p, q) models the **conditional variance** of a time series as a function of past squared
innovations (ARCH terms, order q) and past conditional variances (GARCH terms, order p). It
captures **volatility clustering**: periods of high volatility tend to be followed by high
volatility, and low by low.

**Use GARCH on returns (first differences), not raw price levels.** On raw prices, MLE may
produce nearly non-stationary parameters (α+β→1) and diverging variance forecasts at long
horizons. Compute returns as `y[t] - y[t-1]` before calling `ts_forecast_by`.

The `forecast_variance(h)` path is used internally (not `predict()`). `predict()` returns
seeded simulated innovations — noisy and seed-dependent. The analytical variance forecast
is deterministic and converges to the long-run variance `ω/(1-α-β)` as h→∞.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step between observations (e.g., `'1d'`, `'1h'`) |
| `garch_p` | INTEGER | No | 1 | GARCH order p (number of lagged conditional variance terms) |
| `garch_q` | INTEGER | No | 1 | ARCH order q (number of lagged squared innovation terms) |

All params are passed via the `params` MAP argument:
```sql
params := MAP{'garch_p': '1', 'garch_q': '1'}
```

## Minimum Observations

GARCH(p, q) requires at least **p + q + 10** valid observations after null removal.
For the default GARCH(1,1): **12 minimum** observations.

Series with fewer observations are **skipped** (emitted as zero rows) — not errors.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Conditional **volatility** (std-dev = sqrt(variance)) |
| `model_name` | VARCHAR | `'GARCH(p,q)'` e.g. `'GARCH(1,1)'` |

Note: `yhat_lower` and `yhat_upper` are not available for GARCH in v1 (prediction intervals deferred).

## SQL Example (verified end-to-end)

```sql
-- GARCH(1,1) default — conditional volatility on financial returns
CREATE OR REPLACE TABLE returns AS
    SELECT 'Asset_A' AS asset_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           0.5 * SIN(i * 0.7) + 0.3 * COS(i * 0.3) AS y
    FROM range(40) t(i);  -- 40 obs > 12 minimum for GARCH(1,1)

SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d')
ORDER BY asset_id, forecast_step;
```

Expected output shape: 7 rows, `model_name = 'GARCH(1,1)'`, `yhat` values
are positive and **mean-revert** toward the unconditional volatility as horizon grows.

```sql
-- GARCH(1,1) — explicit parameters via params MAP (same result as default)
SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by(
    'returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'}
)
ORDER BY asset_id, forecast_step;
```

## Typical Workflow: Returns → GARCH Volatility

```sql
-- Step 1: Compute log returns from price levels
CREATE OR REPLACE TABLE price_returns AS
    SELECT
        ticker,
        ds,
        LN(price) - LAG(LN(price)) OVER (PARTITION BY ticker ORDER BY ds) AS y
    FROM price_history;

-- Step 2: GARCH volatility forecast (14 days ahead)
SELECT
    ticker,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS volatility,   -- conditional std-dev (annualize: yhat * SQRT(252))
    model_name
FROM ts_forecast_by('price_returns', ticker, ds, y, 'GARCH', 14, '1d')
ORDER BY ticker, forecast_step;
```

## Model Details

- **Default:** GARCH(1,1) — the most widely used volatility model; suitable for most financial returns.
- **Estimation:** MLE via Nelder-Mead optimizer with multiple restart points (upstream implementation).
- **Stationarity:** MLE enforces the stationarity constraint α+β<1. If the series requires α+β≈1
  (integrated GARCH / IGARCH), the optimizer will clip; results may not be meaningful.
- **Forecast convergence:** GARCH variance forecasts converge to the unconditional variance
  `ω/(1-α-β)` monotonically as h→∞.

## Common Pitfalls

| Pitfall | Problem | Solution |
|---------|---------|---------|
| Raw price levels | α+β→1, diverging variance | Compute returns (first differences) first |
| Short series | InsufficientData → zero rows | Ensure ≥ p+q+10 observations |
| Expecting variance | `yhat` is std-dev, not variance | Square output: `yhat * yhat` for variance |
| Long-horizon precision | Forecasts converge to unconditional variance | Use rolling refits for long-term use |

## Benchmark

Behavioral parity confirmed against `arch` package (Kevin Sheppard, v8.0.0):
- Mean volatility ratio (anofox/arch): **0.897** on M4 Daily returns (100 series)
- Verdict: **PASS** (target: 0.1–10.0)
- Exact numeric match not expected (different MLE initialization strategies)

See `benchmark/m4/garch_benchmark/` for committed results.

## Reference

- Bollerslev (1986), "Generalized Autoregressive Conditional Heteroskedasticity"
- Engle (1982), "Autoregressive Conditional Heteroscedasticity with Estimates of the Variance of United Kingdom Inflation"
