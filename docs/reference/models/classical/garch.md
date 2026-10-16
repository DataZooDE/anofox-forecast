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
| `yhat_lower` / `yhat_upper` | DOUBLE | Crate `GARCH::predict_with_intervals` bounds (see caveat below) |
| `model_name` | VARCHAR | `'GARCH(p,q)'` e.g. `'GARCH(1,1)'` |

> **Interval centring (D-01; fixed upstream in 0.16.0, UPST-05):** `yhat_lower`/`yhat_upper`
> are centred on the fitted conditional mean (`mean ∓ z·sqrt(forecast_variance(h))`) —
> previously they were centred on a seed-dependent simulated-innovation draw, which could
> sit several standard deviations away from the series' actual distribution (measured
> empirical 95% coverage against a true GARCH(1,1) DGP: 63.3% before the fix, 94.8-96.3%
> after, across h=1..12). `yhat` itself is unchanged: the analytical, deterministic
> conditional standard deviation (`sqrt(forecast_variance(h))`). GARCH is not individually
> R-parity-tested (FCST-09, Phase 17 plan 17-03) — see
> [07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
> for the models that are. For calibrated volatility bands, the conformal prediction
> functions (`docs/api/11-conformal-prediction.md`) on top of `yhat` remain an option.

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

```sql
-- Interval bounds are now returned, but do not bracket yhat (see caveat above) --
-- yhat_lower/yhat_upper center a band on a simulated innovation path, not on yhat itself.
SELECT
    asset_id,
    forecast_step,
    ROUND(yhat, 6) AS yhat,
    ROUND(yhat_lower, 6) AS yhat_lower,
    ROUND(yhat_upper, 6) AS yhat_upper,
    (yhat BETWEEN yhat_lower AND yhat_upper) AS brackets_yhat
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d')
ORDER BY asset_id, forecast_step;
-- yhat_lower/yhat_upper are non-NULL; brackets_yhat is false for at least one row,
-- confirming the bounds are centered on a different (simulated) point forecast.
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
-- Step 0: Synthetic daily price levels (2 tickers, 60 days)
CREATE OR REPLACE TABLE price_history AS
    SELECT ticker,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 * EXP(0.0005 * i + 0.02 * SIN(i * 0.5)) AS price
    FROM range(0, 60) t(i)
    CROSS JOIN (SELECT unnest(['AAPL', 'MSFT']) AS ticker);

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
