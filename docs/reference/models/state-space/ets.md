# ETS

> Error-Trend-Seasonal state space model; runs AutoETS selection when no spec is given

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ETS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ETS', horizon, frequency, params);
```

## Description

Error-Trend-Seasonal State Space Model. With an explicit `model` spec, you specify the exact
structure using a 3-4 character code (e.g., 'AAA', 'MNM', 'AAdN').

With **no** `model` spec given, `'ETS'` runs the crate's own `AutoETS` selection — the SAME
construction a standalone `AutoETS` call would make — and `model_name` reports the chosen
spec, e.g. `ETS(A,N,A)`. This is not a hand-picked `'AAA'`/HoltWinters/Holt/SES default: it is
the real AutoETS search, just reported under the `ETS` name. With an explicit spec, the name
is reported with no double-wrap, e.g. `model: 'AAA'` → `model_name = "ETS(A,A,A)"`.

This exact rule — standalone-without-spec selection equals standalone `AutoETS` — also holds
for ensemble members: an `['ETS','ETS']` explicit-member ensemble produces the SAME per-member
fit as a standalone `'ETS'` call (FCST-07: standalone == ensemble member).

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `model` | VARCHAR | No | runs crate `AutoETS` selection (see above) | ETS model specification (see below) |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when omitted (required only for an explicit seasonal `model` spec if detection fails) |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## ETS Model Specification

Format: `[Error][Trend][Seasonal]` or `[Error][Trend][Damped][Seasonal]`

| Position | Component | Values |
|----------|-----------|--------|
| 1st | Error | `A` (Additive), `M` (Multiplicative) |
| 2nd | Trend | `N` (None), `A` (Additive), `M` (Multiplicative) |
| 3rd (optional) | Damped | `d` (damped trend) |
| Last | Seasonal | `N` (None), `A` (Additive), `M` (Multiplicative) |

A seasonal `model` spec (AAA, MAM, etc.) needs a seasonal period; `seasonal_period` is
auto-detected when omitted, so you only need to pass it explicitly if detection fails or you
want a specific period. Not needed at all for non-seasonal models (ANN, AAN, AAdN).

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
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `ETS`/`AutoETS` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for the known seasonal-ETS interval issue (half-width grows `σ√h`/`σ√⌈h/m⌉` rather than
Hyndman's analytic state-space variance — flatter than it should be, fix planned for v0.10.0, Phase 11/17).

## SQL Example

```sql
CREATE OR REPLACE TABLE daily_sales AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       50.0 + product_id * 5.0 + 6.0 * sin(2 * pi() * i / 7.0) AS quantity
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- No model spec: ETS runs the crate's own AutoETS selection and reports the chosen spec
SELECT DISTINCT model_name
FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'ETS',
    14,
    '1d',
    MAP{'seasonal_period': '7'}
);
-- model_name = 'ETS(A,N,A)' (the chosen spec; a standalone 'AutoETS' call picks the same one)

-- Explicit multiplicative Holt-Winters spec — reported with no double-wrap
SELECT DISTINCT model_name FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'ETS',
    12,
    '1mo',
    MAP{'model': 'MAM', 'seasonal_period': '12'}
);
-- model_name = 'ETS(M,A,M)'

-- Explicit damped trend, no seasonality
SELECT DISTINCT model_name FROM ts_forecast_by(
    'quarterly_sales',
    region_id,
    date,
    sales,
    'ETS',
    4,
    '3mo',
    MAP{'model': 'AAdN'}
);
-- model_name = 'ETS(A,Ad,N)'
```

## Best For

- When you know the exact model structure needed (pass `model`)
- When you want AutoETS-quality selection but reported under the `ETS` name (omit `model`)
- Multiplicative seasonality (seasonal amplitude proportional to level)
- Damped trends that taper off over time
- Expert users who understand ETS taxonomy
