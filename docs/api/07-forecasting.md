# Forecasting

> Point forecasts and prediction intervals for time series

## Overview

The extension provides 36 forecasting models ranging from simple baselines to sophisticated state-space methods, classical volatility models, and multivariate Vector Autoregression.

**Use this document to:**
- Generate point forecasts and prediction intervals for single or multiple series
- Choose from 36 models: baselines (Naive, SMA), exponential smoothing (ETS, Holt-Winters), state-space (ARIMA, Kalman), classical (GARCH), ensemble (AutoEnsemble), multi-seasonal (MSTL, TBATS), intermittent demand (Croston, TSB), distributional (Laplace), and multivariate (VAR)
- Use automatic model selection (AutoETS, AutoARIMA, AutoTheta, AutoEnsemble) when unsure
- Incorporate exogenous variables with supported models
- Understand the detect-then-forecast workflow for seasonal data

---

## Quick Start

Generate forecasts for multiple series with a single call:

```sql
CREATE OR REPLACE TABLE forecast_sales_data AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       100.0 + product_id * 10.0 + 0.5 * i + 15.0 * sin(2 * pi() * i / 7.0) AS revenue
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- Forecast 14 days ahead for all products using HoltWinters with weekly seasonality
SELECT * FROM ts_forecast_by(
    'forecast_sales_data',              -- source: table name (quoted string)
    product_id,                -- group_col: series identifier (unquoted)
    date,                      -- date_col: timestamp column (unquoted)
    revenue,                   -- target_col: value to forecast (unquoted)
    'HoltWinters',             -- method: forecasting model (seasonal)
    14,                        -- horizon: periods to forecast
    '1d',                      -- frequency: time step between observations
    MAP{'seasonal_period': '7'}  -- params: weekly seasonality (required for seasonal models)
);
```

Compare multiple models:

```sql
-- Naive baseline (no params needed)
SELECT *, 'Naive' AS model FROM ts_forecast_by('forecast_sales_data', product_id, date, revenue, 'Naive', 7, '1d', MAP{})
UNION ALL
-- HoltWinters with weekly seasonality
SELECT *, 'HoltWinters' AS model FROM ts_forecast_by('forecast_sales_data', product_id, date, revenue, 'HoltWinters', 7, '1d', MAP{'seasonal_period': '7'});
```

### Handling Seasonality

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`, plus the scalar paths) auto-detects the seasonal period internally
> via `detect_seasonality`. For models that can run non-seasonally (`ETS`, `AutoETS`,
> `ARIMA`/`AutoARIMA`, the Theta family, `MFLES`), a period that can't be detected falls back
> to non-seasonal (period 1) — detection is a convenience, not a requirement. For models that
> **require** a seasonal period to exist at all (`SeasonalNaive`, `SeasonalWindowAverage`,
> `HoltWinters`, `SeasonalES`, `SeasonalESOptimized`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS`),
> a period that can't be detected is a hard SQL error naming the model — there is no silent
> default of 12 or 2. `ts_forecast_agg` follows the same rule — it auto-detects when both
> `seasonal_period` and `seasonal_periods` are omitted, and a period-requiring model with no
> detectable period is the same SQL error (confirmed: `ts_forecast_agg(..., 'HoltWinters', ...,
> MAP{})` succeeds on a seasonal series and errors naming the model on a 24-point linear ramp).
> Passing `seasonal_period` explicitly is still recommended below — it gives you control and
> reproducibility over what auto-detection would otherwise pick silently.

**Step 1: Detect seasonality**

```sql
CREATE OR REPLACE TABLE daily_sales AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       50.0 + product_id * 5.0 + 6.0 * sin(2 * pi() * i / 7.0) AS value
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

SELECT product_id, round(primary_period) AS primary_period
FROM ts_detect_periods_by('daily_sales', product_id, date, value, MAP{});
-- Returns: primary_period = 7 (weekly pattern detected)

-- Equivalent GROUP BY aggregate form, useful inline in a larger aggregate query:
SELECT product_id, round(ts_detect_periods_agg(date, value, 'fft').primary_period) AS primary_period
FROM daily_sales GROUP BY product_id;
-- Returns: primary_period = 7 (weekly pattern detected)
```

**Step 2: Use detected period in forecasting**
```sql
-- For forecasting
SELECT * FROM ts_forecast_by(
    'daily_sales', product_id, date, value,
    'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'}  -- Pass detected period explicitly
);

-- For backtesting (two-step cross-validation)
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('daily_sales', product_id, date, value, 5, 7, MAP{});
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, value,
    'AutoETS', MAP{'seasonal_period': '7'}
);
```

**Combined workflow** (detect and forecast in one query):
```sql
WITH detected AS (
    SELECT round(ts_detect_periods_agg(date, value, 'fft').primary_period) AS season
    FROM daily_sales
    LIMIT 1
)
SELECT * FROM ts_forecast_by(
    'daily_sales', product_id, date, value,
    'HoltWinters', 14, '1d',
    MAP{'seasonal_period': (SELECT season FROM detected)::VARCHAR}
);
```

**Why explicit?** Although `ts_forecast_by` auto-detects when you omit seasonal params, that
auto-detection can produce unexpected results. By separating detection from forecasting, you can:
- Validate detected periods make business sense (e.g., 7 = weekly, 12 = monthly, 365 = yearly)
- Use domain knowledge to override detection
- Apply the same period consistently across models

### Complete Forecasting Workflow

End-to-end example showing data preparation through forecasting:

```sql
-- Step 1: Check data quality (ts_stats_by returns flat columns, not a STRUCT)
SELECT product_id, length, n_nulls
FROM ts_stats_by('daily_sales', product_id, date, revenue, '1d');
-- Review: check for NULLs, gaps, and series length

-- Step 2: Detect seasonality (explicit control; ts_detect_periods_by's _by form
-- returns NULL on this data due to a known detection bug -- see the Handling
-- Seasonality note above -- so use the aggregate form)
SELECT product_id, round(ts_detect_periods_agg(date, revenue, 'fft').primary_period) AS period
FROM daily_sales GROUP BY product_id;
-- Example output: period = 7 (weekly pattern)

-- Step 3: Forecast with detected period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id, date, revenue,
    'HoltWinters', 14, '1d',
    MAP{'seasonal_period': '7'}
);
```

---

## Model Selection Guide

**For beginners:** Start with `Naive` or `SES` to establish baselines, then try `AutoETS` or `AutoEnsemble` for automatic model selection.

| Data Characteristics | Recommended Models |
|---------------------|-------------------|
| No trend, no seasonality | `Naive`, `SES`, `SESOptimized` |
| Trend, no seasonality | `Holt`, `Theta`, `RandomWalkDrift` |
| Seasonality (single period) | `SeasonalNaive`, `HoltWinters`, `SeasonalES`, `Laplace` |
| Multiple seasonalities | `MSTL`, `MFLES`, `TBATS` |
| Intermittent demand (many zeros) | `CrostonClassic`, `CrostonSBA`, `TSB`, `Laplace` (with `auto_aid`) |
| Unknown characteristics | `AutoETS`, `AutoARIMA`, `AutoTheta`, `AutoEnsemble`, `Laplace` |
| Ensemble of ARIMA/ETS/Theta | `AutoEnsemble` |
| Streaming / distributional | `Laplace` |

## Supported Models (36 Models)

`GARCH` and `Kalman` (below, under "Classical Models") and `AutoEnsemble` (below, under
"Ensemble Forecasting") are also part of this 36-model `ts_forecast_by` surface; they are
documented in their own sections because of their distinct parameter/output shape.

### Automatic Selection Models (7)
| Model | Description | Optional Params |
|-------|-------------|-----------------|
| `AutoETS` | Automatic ETS model selection | *seasonal_period* |
| `AutoARIMA` | Automatic ARIMA model selection | *seasonal_period* |
| `AutoTheta` | Automatic Theta method selection | *seasonal_period* |
| `AutoEnsemble` | Auto-fit ARIMA/ETS/Theta, rank by MSE, combine top-K | *top_k*, *combination_method*, *seasonal_period* |
| `AutoMFLES` | Currently an alias for `MFLES` (same code path) | *seasonal_periods[]* |
| `AutoMSTL` | Currently an alias for `MSTL` (same code path) | *seasonal_periods[]* |
| `AutoTBATS` | Automatic TBATS selection | *seasonal_periods[]* |

### Basic Models (7)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Naive` | Last value repeated | — | — |
| `SMA` | Simple Moving Average | — | *window* (default: 5) |
| `SeasonalNaive` | Last season repeated | — | *seasonal_period* (auto-detected if omitted; errors if none found) |
| `SeasonalWindowAverage` | Average of same seasonal position over a sliding window | — | *seasonal_period* (auto-detected if omitted; errors if none found) |
| `SES` | Simple Exponential Smoothing | — | — (fixed alpha, not SQL-configurable) |
| `SESOptimized` | Optimized SES | — | — |
| `RandomWalkDrift` | Random walk with drift | — | — |

### Exponential Smoothing Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Holt` | Holt's linear trend method | — | — (fixed smoothing, not SQL-configurable) |
| `HoltWinters` | Holt-Winters seasonal method | — | *seasonal_period* (auto-detected if omitted; errors if none found); fixed smoothing, not SQL-configurable |
| `SeasonalES` | Seasonal Exponential Smoothing | — | *seasonal_period* (auto-detected if omitted; errors if none found); fixed smoothing, not SQL-configurable |
| `SeasonalESOptimized` | Optimized Seasonal ES | — | *seasonal_period* (auto-detected if omitted; errors if none found) |

### Theta Methods (5)
| Model | Description | Optional |
|-------|-------------|----------|
| `Theta` | Standard Theta method | *seasonal_period* (auto-detected if omitted) — no `theta` key accepted |
| `OptimizedTheta` | Optimized Theta method | *seasonal_period* (auto-detected if omitted) |
| `DynamicTheta` | Dynamic Theta method | *seasonal_period* (auto-detected if omitted) — no `theta` key accepted |
| `DynamicOptimizedTheta` | Dynamic Optimized Theta | *seasonal_period* (auto-detected if omitted) |
| `AutoTheta` | Automatic Theta selection | *seasonal_period* (auto-detected if omitted) |

### State Space & ARIMA Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `ETS` | Error-Trend-Seasonal model. Without a `model` spec, runs the crate's own `AutoETS` selection (not a hand-picked `'AAA'` default) and reports the chosen spec, e.g. `ETS(A,A,M)` — the same selection a standalone `AutoETS` call would make | — | *seasonal_period*, *model* |
| `AutoETS` | Automatic ETS selection; a crate fit/predict failure or panic is a hard error naming `AutoETS` — never a silent HoltWinters/Holt/SES substitution | — | *seasonal_period* (auto-detected if omitted) |
| `ARIMA` | Real crate `ARIMA`/`SARIMA` fit at an explicit order (not a heuristic) | **arima_order** `[p,d,q]` | *seasonal_order* `[P,D,Q]` (+ `seasonal_period`); `p,q,P,Q ≤ 10`, `d,D ≤ 2`; `model_name` appends `" with drift"` / `" with non-zero mean"` when the crate's own AICc comparison keeps a non-zero intercept |
| `AutoARIMA` | Automatic ARIMA/SARIMA order search | — | *seasonal_period* (auto-detected if omitted) |

`ARIMA` without `arima_order` is a hard SQL error naming `AutoARIMA` as the alternative:

```sql
SELECT * FROM ts_forecast_by('forecast_sales_data', product_id, date, revenue, 'ARIMA', 7, '1d', MAP{});
-- Invalid Input Error: Invalid input: ARIMA requires arima_order=[p,d,q] (e.g. [1,1,1]);
--                       use 'AutoARIMA' for automatic order selection
```

```sql
-- ARIMA(0,1,1)(0,1,1)[12] -- the AirPassengers "airline model" -- via an explicit order
CREATE OR REPLACE TABLE air AS
SELECT 'A' AS id,
       ('1949-01-01'::DATE + (i || ' months')::INTERVAL)::DATE AS ds,
       112.0 + 5.0 * i + 40.0 * sin(2 * pi() * i / 12.0) AS y
FROM range(0, 60) t(i);

SELECT forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_by(
    'air', id, ds, y, 'ARIMA', 3, '1mo',
    MAP{'arima_order': '[0,1,1]', 'seasonal_order': '[0,1,1]', 'seasonal_period': '12'}
)
ORDER BY forecast_step;
-- model_name = 'ARIMA(0,1,1)(0,1,1)[12]'
```

### Multiple Seasonality Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `MFLES` | Multiple Frequency LES | — | *seasonal_periods[]* (auto-detected single period if omitted; non-seasonal if none found) |
| `AutoMFLES` | Currently an alias for `MFLES` (same code path) | — | *seasonal_periods[]* (auto-detected single period if omitted; non-seasonal if none found) |
| `MSTL` | Multiple Seasonal-Trend Loess | — | *seasonal_periods[]* (auto-detected single period if omitted; **errors if none found** — no default `[12]`) |
| `AutoMSTL` | Currently an alias for `MSTL` (same code path) | — | *seasonal_periods[]* (auto-detected single period if omitted; **errors if none found**) |
| `TBATS` | Trigonometric BATS | — | *seasonal_periods[]* (auto-detected single period if omitted; **errors if none found** — no default `[12]`); no `use_box_cox` key accepted |
| `AutoTBATS` | Automatic TBATS | — | *seasonal_periods[]* (auto-detected single period if omitted; **errors if none found**) |

### Intermittent Demand Models (6)
| Model | Description | Optional |
|-------|-------------|----------|
| `CrostonClassic` | Classic Croston's method | — |
| `CrostonOptimized` | Optimized Croston's method | — |
| `CrostonSBA` | Syntetos-Boylan Approximation | — |
| `ADIDA` | Aggregate-Disaggregate IDA | — |
| `IMAPA` | Intermittent Multiple Aggregation | — |
| `TSB` | Teunter-Syntetos-Babai method | — (fixed smoothing, no `alpha_d`/`alpha_p` keys accepted) |

### Distributional Models (1)

Streaming likelihood-weighted mixture of leaves (EMA / drift / AR(1) / damped-Holt + optional seasonal / distribution-family leaves). Point + interval forecasts; the full mixture parameters will be exposed by the upcoming `ts_forecast_dist_by` (PR B).

| Model | Description | Optional |
|-------|-------------|----------|
| `Laplace` | Streaming distributional forecaster with three zero-config selectors (`auto` / `auto_aid` / `skaters`) | *seasonal_period*, *laplace_variant*, *laplace_seasonal_batch_init* |

Laplace's own distributional intervals pass through unchanged — see
["Prediction intervals, fitted values and input handling"](#prediction-intervals-fitted-values-and-input-handling)
below.

**Variants** (`laplace_variant`):
- `auto` (default) — balanced for smooth / continuous series with adequate history
- `auto_aid` — AID-based distribution-family selection; best for retail SKU / intermittent counts
- `skaters` — fuller skaters ensemble (multi-h scoring, stacking, larger leaf set); slower, more robust

**Batch init** (`laplace_seasonal_batch_init: 1`) initialises the seasonal-EMA phase levels from the last training cycle. Safe on stationary or amplitude-declining seasonal series; avoid on growing amplitude or phase-shifted seasonality (the softmax abandons the seasonal-EMA leaf and the forecast collapses to flat).

See [reference/models/distributional/laplace.md](../reference/models/distributional/laplace.md) for the full trade-off table and worked examples.

---

## Prediction intervals, fitted values and input handling

This section applies to every forecast surface (`ts_forecast_by`, `ts_forecast_exog_by`,
`ts_forecast_agg`, the scalar paths, `ts_cv_forecast_by`) unless a surface's own section
says otherwise — see [`ts_forecast_agg` specifics](#ts_forecast_agg-specifics) below for
where that surface's own behaviour differs or needs elaboration.

### Prediction intervals

`yhat_lower` / `yhat_upper` are each model's **own crate-native prediction interval** at the
requested `confidence_level` (default **0.90**) — never a fabricated `point ± z·SD·√h` band.
The confidence-level-to-quantile mapping is computed inside the crate itself, per model; for
some models that is an approximation to the inverse-normal CDF rather than an exact lookup
(documented per row below).

**Models with no native interval implementation** return `NULL` lower/upper bounds —
`ts_forecast_panel_by` (point-only, v1) and `MSTL`/`AutoMSTL` **when called with
regressors** (`ts_forecast_exog_by`) are the two cases on the current surface. Use the
conformal prediction functions ([11-conformal-prediction.md](11-conformal-prediction.md)) on
top of the point forecast for calibrated coverage in these cases, or whenever a model's own
interval below is flagged as miscalibrated.

**R `forecast` parity (FCST-09, Phase 17 plan 17-03).** Point forecasts and 95% intervals
for the ten core models are checked against R `forecast` 9.0.2 on AirPassengers plus three
seeded simulated series (a driftless random walk, an intermittent-demand series, and a
random walk with drift) — 36 checks total, asserted by
`crates/anofox-fcst-core/tests/r_forecast_parity.rs` against
`validation/reference/fixtures/r_forecast/{air_passengers,simulated_series}.json`
`tolerance.parity_checks`. 25 of 36 checks are in parity; the other 11 are documented known
gaps, each with an identified root cause (D-12) — see `validation/reference/README.md`'s
"r_forecast parity tolerances" and "R-parity gap root causes" sections for the full table
and per-check numbers.

| Model(s) | R parity | Root cause (when a known gap) |
|---|---|---|
| `Naive`, `SeasonalNaive`, `RandomWalkDrift`, `CrostonClassic` | Parity (point exact; bounds within the crate's A&S 26.2.23 z-approximation / drift-interval-formula tolerance) | — |
| `SESOptimized` / `ETS 'ANN'` | Parity on AirPassengers / rw / trend | — (the intermittent series is a known gap, see below) |
| `SESOptimized` on the intermittent series | Known gap | `RC-04`: the crate's own SES optimiser converges to a different `(alpha, l0)` than R's on this zero-inflated series — upstream draft filed |
| `Holt` / `ETS 'AAN'` | Known gap, but tightened: `'Holt'` now dispatches to the crate's `ETS(A,A,N)`, R `holt()`'s own definition (D-12/D-13 fix) | `RC-01` (the fix) plus `RC-02` (the residual — see next row); parity on `trend` after the fix |
| `HoltWinters` / `ETS 'AAA'` / `ETS 'MAM'` | Known gap on AirPassengers | `RC-02`: R's own optimiser reaches a looser, in-bounds fit than the crate's on these specs — not a defect |
| `AutoETS` | Parity on `rw`/`trend` (selects R's spec); known gap on AirPassengers | `RC-03`: the crate's optimiser scores R's selected `ETS(M,Ad,M)` worse than R does, so `AutoETS` picks `ETS(M,A,M)` instead — upstream draft filed |
| `AutoARIMA` | Parity (order + point + 95% interval) on every series, including AirPassengers (D-15; D-16's prior exception resolved, see below) | — |
| `Theta` | Parity (point within 1e-5, 95% bounds within 5e-4 on AirPassengers; 1e-6 on rw/trend) | — |

Interval-formula changes 0.16.0 made for models not covered by the R-parity suite above (no
R-parity claim for these; see the crate's own CHANGELOG, PR #225 for TBATS/MSTL/seasonal-ES
and PR #211/pre-0.16 history for GARCH):

| Model(s) | 0.16.0 change |
|---|---|
| `TBATS`, `AutoTBATS` | Innovations state-space variance (previously 3–8% of a `statsforecast` reference width) |
| `MSTL`, `AutoMSTL` | Delegates interval computation to its trend forecaster |
| `SeasonalES`, `SeasonalESOptimized` | Per-slot `ETS(A,N,N)` variance |
| `GARCH` | `yhat_lower`/`yhat_upper` are centred on the conditional mean (previously centred on the simulated point forecast only) |

**`'SES'` is a fixed `alpha=0.3` model**, not R's `ses()` — it is a deliberately simple,
non-optimised baseline. R `ses()` parity is checked against `'SESOptimized'` / `ETS 'ANN'`
above, not `'SES'`.

**`AutoARIMA` and R `auto.arima` (D-15, since anofox-forecast 0.16.1, PR #223; interval parity
since 0.16.2, PR #230/#229).** `AutoARIMA` now ports R's `stats::arima` estimation path and the
Hyndman-Khandakar stepwise search with R defaults, so it selects the SAME order R `auto.arima`
selects — `AutoARIMA(2,1,1)(0,1,0)[12]` on AirPassengers, `AutoARIMA(0,1,0)` on both seeded
non-seasonal series (R's drift on the random-walk-with-drift series is evidenced by the point
forecast — a drift-free `(0,1,0)` would be flat). A seeded selection-frequency simulation (30
random walks + 30 AR(0.7) series) measured 0 share deviation from R and 60/60 exact `(p,d,q)`
agreement. Selected models differ from pre-0.16.1 releases of this extension (see Breaking
changes in the CHANGELOG). The one former interval exception is resolved: on AirPassengers,
`AutoARIMA`'s 95% interval used to be up to ~18% off R's on 0.16.1 (the point forecast and
order were always exact) — the crate's seasonal-ARIMA prediction-variance computation differed
from R `stats::arima`'s `KalmanForecast` at longer horizons, tracked as `RC-05`. anofox-forecast
0.16.2 fixed it upstream; the interval now re-measures at `4.592775e-3` relative deviation
(down from `0.1846662`), inside the parity tier on every series.

### Fitted values, residuals, and MSE

`fitted_values` / `residuals` / MSE come from the crate model itself (`fitted_values()` /
`residuals()`) — there is no SES(α=0.3) or any other stand-in surrogate for any model.

- Positions the crate doesn't produce a fitted value for (warmup rows — e.g. ARIMA/SARIMA
  differencing order `d`/`D`, or the leading rows trimmed per "Input handling" below) are
  `NULL`, not zero or an interpolated guess.
- Interior rows that were linearly interpolated before fitting (see below) keep a fitted
  value but their **residual is `NULL`** — an interpolated point has no real observation to
  compare against.
- **MSE** is the mean of `residual²` over every **non-interpolated, non-`NULL`** residual
  position — interpolated and warmup rows are excluded from the average.
- When a crate model's fitted/residual array length does not match the (trimmed) input
  series length (ARIMA/AutoARIMA differencing can drop more rows than a straight NULL-head
  alignment would), the exported `insample_fitted` array is **empty** rather than
  mis-aligned to the wrong row — a safety rule, not a bug.
- As of `anofox-forecast` 0.16.0 (PR #222), `Theta`'s in-sample fitted values use the
  optimised per-series `alpha`, not a fixed `0.1` — so `Theta`'s fitted/residuals/MSE no
  longer coincide with `CrostonClassic`'s (which is still a fixed-`alpha=0.1` recursion).
  Pinned by `crates/anofox-fcst-core/tests/theta_croston_fitted.rs`
  (`theta_optimised_alpha_diverges_from_croston_classic_on_positive_series`); `Theta`'s
  point-forecast R parity (FCST-09) is in the prediction-intervals section above.

```sql
CREATE OR REPLACE TABLE fitted_demo AS
SELECT 'A' AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
       50.0 + i * 0.3 + 5.0 * sin(i * 0.3) AS y
FROM range(60) t(i);

-- AutoETS has no differencing warmup on this series: the fitted array is full-length.
SELECT (fcst).insample_fitted[1:5] AS fitted_head, (fcst).model_name
FROM (SELECT ts_forecast_agg(ds, y, 'AutoETS', 3, MAP{}) AS fcst FROM fitted_demo);
```

### Input handling

- `NULL` and `NaN` are both treated as **missing** — there is no behavioral difference
  between a SQL `NULL` and a floating-point `NaN` in the target column.
- **Interior** missing values are linearly interpolated before fitting (so the model always
  sees a dense series); their residual positions are `NULL` as described above.
- **Leading/trailing** missing values are **trimmed** before fitting — but the forecast
  horizon still starts from the period immediately after the series' ORIGINAL last row
  (trailing NULLs do not pull the forecast origin backward to the last valid value).
- A series that is **all-missing**, or has **fewer than 3** valid values after trimming, is a
  hard error — isolated per group (one bad group does not fail the whole call).
- `±Inf` anywhere in the target column is a hard error naming the index.

```sql
-- A daily series y = 0..29 followed by two trailing NULL rows. forecast_step=1 still
-- lands on the day after the ORIGINAL last row (2020-02-02), not the last valid row.
CREATE OR REPLACE TABLE trailing_null_demo AS
SELECT 'A' AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
       CASE WHEN i >= 30 THEN NULL ELSE i::DOUBLE END AS y
FROM range(32) AS t(i);

SELECT ds, yhat FROM ts_forecast_by('trailing_null_demo', id, ds, y, 'RandomWalkDrift', 3, '1d', MAP{})
WHERE forecast_step = 1;
-- ds = 2020-02-02, yhat = 32.0
```

### Former silent fallbacks — now hard errors

Every one of the following used to be absorbed silently (a different model ran under the
requested name, or a default was substituted); every one is now an SQL error that names the
offending model/parameter:

- `'ARIMA'` without `arima_order` (suggests `AutoARIMA`)
- A model that **requires** a seasonal period (`SeasonalNaive`, `SeasonalWindowAverage`,
  `HoltWinters`, `SeasonalES`, `SeasonalESOptimized`, `MSTL`, `AutoMSTL`, `TBATS`,
  `AutoTBATS`) when none can be auto-detected and none is given explicitly
- `AutoETS` crate fit/predict failure or panic (used to silently substitute
  HoltWinters/Holt/SES under the `AutoETS` name)
- An unrecognized `kalman_model` value
- An unrecognized panel `croston_variant` or `model_pool` value
- An unparsable `seasonal_periods` entry (non-integer, or `< 2`)
- Invalid UTF-8 in any option string (model name, `kalman_model`, `croston_variant`, …)
- Regressors passed to a model that doesn't support them (see
  ["ts_forecast_exog_by"](#ts_forecast_exog_by) above for the exog-capable model list)

### `ts_forecast_agg` specifics

- Params are validated against the **same closed key set** as `ts_forecast_by`, and every
  `ts_forecast_by` param (`kalman_model`, `garch_p`/`garch_q`, `model_pool`, `window`,
  `seasonal_periods`, `laplace_variant`, `laplace_seasonal_batch_init`, `top_k`,
  `combination_method`, `arima_order`/`seasonal_order`, `confidence_level`) is honoured.
  An invalid model or parameter value (e.g. an unrecognized `kalman_model`) raises the
  **same SQL error** `ts_forecast_by` raises for the identical input; any other per-group
  failure (e.g. too little data after trimming) is reported in that group's
  `error_message` field instead — confirmed: a 2-row group with `'Naive'`/`MAP{}` returns
  `error_message = 'Insufficient data: need at least 3 observations, got 2'` and
  `len(point_forecast) = 0` rather than aborting the whole query.
- Bounds live in fields `lower_<pct>` / `upper_<pct>`, where `pct = round(100 *
  confidence_level)` — default `lower_90` / `upper_90`. `confidence_level` determines
  these field names and is fixed at bind time, so it **must be a constant expression**; a
  per-row/per-group value that disagrees with the bound one is an SQL error.

  ```sql
  CREATE OR REPLACE TABLE agg_confidence_demo AS
  SELECT 'A' AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, (50.0 + i)::DOUBLE AS y
  FROM range(30) t(i);

  SELECT (fcst).lower_95, (fcst).upper_95
  FROM (SELECT ts_forecast_agg(ds::TIMESTAMP, y, 'Naive', 3, MAP{'confidence_level': '0.95'}) AS fcst
        FROM agg_confidence_demo);
  ```

- **Input handling matches `ts_forecast_by`** (Phase 12, D-11): `ts_forecast_agg` applies
  the exact same NULL policy — interior `NULL`s are linearly interpolated (not spliced
  out), and a `NULL`-valued trailing row still leaves the forecast origin anchored to the
  series' ORIGINAL last row, exactly as `ts_forecast_by` does. Confirmed on the
  `trailing_null_demo` fixture above: `ts_forecast_agg` forecasts from `2020-02-02`
  (`yhat = 32.0`), identical to `ts_forecast_by`. Only a `NULL` **timestamp** drops a row
  (there is nothing to anchor it to); `NaN` still follows the shared NULL-equivalence
  rule. Forecast dates also come from the same Rust frequency-detection + calendar rule
  `ts_forecast_by` uses (see "Calendar stepping" above), with the frequency auto-detected
  from the observed dates instead of a declared `frequency` argument.

---

## Table Macros

### ts_forecast_by

Generate forecasts for multiple time series grouped by an identifier. This is the **primary forecasting function**.

**Signature:**
```sql
ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, frequency, params?) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `table_name` | VARCHAR | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Target value column (unquoted) |
| `method` | VARCHAR | Forecasting method (case-sensitive) |
| `horizon` | INTEGER | Number of periods to forecast |
| `frequency` | VARCHAR | Time step between observations (e.g., `'1d'`, `'1h'`, `'1mo'`) |
| `params` | MAP or STRUCT | Model parameters (optional) |

**Calendar stepping (D-12).** For `'1mo'`/`'3mo'`/`'1q'`/`'1y'` (monthly/quarterly/yearly)
frequencies, forecast dates are computed with a calendar-aware rule, not fixed-microsecond
arithmetic: if every observed date in the series falls on the last day of its month, every
forecast date is also a month-end (e.g. a series ending `2023-11-30` forecasts
`2023-12-31`, `2024-01-31`, `2024-02-29` — no drift into a 30-day month); otherwise the
last observed day-of-month is kept, clamped to the target month's length (e.g. a series
ending `2023-04-30` forecasts `2023-05-30`, `2023-06-30`, `2023-07-30`). Quarterly and
yearly frequencies apply the same rule with 3- and 12-month steps. Each step is computed
fresh from the last observation, never cumulatively. `ts_forecast_agg` uses the identical
rule (with frequency auto-detected from the data), and so do every other forecast
surface — `ts_forecast_exog_by`, `ts_forecast_ensemble_by`, `ts_forecast_panel_by`, and
`ts_forecast_var_by` all compute future dates through the same Rust calendar rule (RUST-09),
so every public forecast surface agrees on dates (not just values) for the same input.

**Selected `params` keys** (full per-model list in the ["Supported Models"](#supported-models-36-models) tables above):

| Key | Type | Applies to | Description |
|---|---|---|---|
| `arima_order` | `[p,d,q]` (JSON-array string in MAP form) | `ARIMA` | **Required** for `ARIMA`; `p,q ≤ 10`, `d ≤ 2` |
| `seasonal_order` | `[P,D,Q]` (JSON-array string in MAP form) | `ARIMA` | Optional; requires `seasonal_period` too; `P,Q ≤ 10`, `D ≤ 2` |
| `seasonal_period` | INTEGER | seasonal-capable models | Single seasonal period; see per-model tables above for auto-detect/required behavior |
| `seasonal_periods` | `[p1, p2, ...]` (JSON-array string in MAP form) | `MSTL`, `TBATS`, `MFLES` + Auto variants | Multiple seasonal periods |
| `confidence_level` | DOUBLE | all models | Interval confidence level; default `0.90`, range `(0, 1)` |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` come from each model's own crate-native prediction interval at
`confidence_level` — see
["Prediction intervals, fitted values and input handling"](#prediction-intervals-fitted-values-and-input-handling)
below for the known per-model calibration caveats, which models return `NULL` bounds, and how
to get calibrated coverage via conformal prediction
([11-conformal-prediction.md](11-conformal-prediction.md)).

**Examples:**
```sql
-- HoltWinters with weekly seasonality (guaranteed seasonal model)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'HoltWinters', 12, '1d',
    MAP{'seasonal_period': '7'});

-- AutoETS considers seasonal models when seasonal_period provided
-- (may still select non-seasonal if it fits better)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'AutoETS', 12, '1d',
    MAP{'seasonal_period': '7'});

-- MSTL with multiple seasonal periods (array as JSON string)
SELECT * FROM ts_forecast_by('sales', product_id, date, val, 'MSTL', 30, '1d',
    MAP{'seasonal_periods': '[7, 14]'});

-- Naive baseline (no seasonal_period needed)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'Naive', 12, '1d', MAP{});
```

---

### ts_forecast_exog_by

Multi-series forecasting with exogenous variables. This is the **primary exogenous forecasting function**.

**Signature:**
```sql
ts_forecast_exog_by(table_name, group_col, date_col, target_col, x_cols, future_table, future_date_col, future_x_cols, frequency, model, horizon, params?) → TABLE
```

**Exog-capable models** — every model below routes through the crate's own
exogenous-regressor support (`CalendarAnnotations` + `predict_with_exog_intervals`); there
is no separate `X`-suffixed model name. The reported `model_name` is the base model's own
name (including any auto-selected order/spec) with `" with regressors"` appended exactly
once — e.g. `AutoARIMA(3,0,1)(0,1,0)[9] with regressors`, `ETS(A,A,N) with regressors`,
`AutoTheta(DOTM) with regressors`:

| Exog-capable models |
|---|
| `Naive`, `ARIMA`, `AutoARIMA`, `Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta`, `ETS`, `AutoETS`, `MFLES`, `MSTL` |

A model NOT in this list (e.g. `GARCH`, `Kalman`, `SES`, `HoltWinters`, any Croston variant)
given a regressor is a hard SQL error naming the model and listing the exog-capable set.
`ARIMA` in this list still requires `arima_order` (same rule as the non-exog surface).

**Calendar-correct future dates (RUST-09, D-12).** Future forecast dates are computed
through the same Rust calendar rule as `ts_forecast_by` (see "Calendar stepping" above) —
a `'1mo'`/`'3mo'`/`'1q'`/`'1y'` series that ends on a month-end forecasts month-ends with no
drift into a 30-day month. `ts_forecast_ensemble_by`, `ts_forecast_panel_by`, and
`ts_forecast_var_by` share the identical rule, so every forecast surface agrees on dates
for the same input.

**Regressor semantics:**
- A regressor that is **constant throughout history** (e.g. a holiday flag never observed as
  1 during training) is dropped before fitting — it gets a 0 effect by construction, and the
  reported `model_name` carries NO `" with regressors"` suffix (the forecast is identical to
  calling the model with no regressors at all).
- `NULL`/`NaN` historical regressor values: **interior** gaps are linearly interpolated;
  **leading/trailing (edge)** gaps are filled with the NEAREST observed value (constant edge
  extension, never extrapolated — D-14, G26). An all-`NULL` historical regressor is a hard
  error. Any `NULL` in a FUTURE regressor value is a hard error naming the regressor index and
  horizon step — future regressor values must be fully known.
- `confidence_level` defaults to **0.90** on the exog surface too (not the old 0.95).
- Every `ts_forecast_by` option (`seasonal_period`, `ets_spec`, `garch_p`/`garch_q`,
  `kalman_model`, `arima_order`/`seasonal_order`, …) is honoured identically via the same
  `params` argument — the exog surface is not a reduced parameter set.

**Example (verified end-to-end):**
```sql
-- Create historical data with exogenous variable
CREATE OR REPLACE TABLE sales_history AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       100.0 + product_id * 10.0 + 0.5 * i + 15.0 * temp + 30.0 * promo AS revenue,
       temp AS temperature
FROM (
    SELECT i, 0.8 + (i % 5) * 0.02 AS temp,
           CASE WHEN i % 9 = 0 THEN 1 ELSE 0 END AS promo
    FROM range(0, 60) t(i)
) d
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- Create future exogenous data (must cover EXACTLY the forecast horizon)
CREATE OR REPLACE TABLE future_weather AS
SELECT product_id,
       (DATE '2024-03-01' + INTERVAL (i) DAY) AS date,
       0.9 + (i % 5) * 0.02 AS temperature
FROM range(0, 7) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- Forecast with exogenous regressor
SELECT forecast_step, model_name
FROM ts_forecast_exog_by(
    'sales_history',       -- historical data table
    product_id,            -- group column
    date,                  -- date column
    revenue,               -- target column
    ['temperature'],       -- exogenous columns (historical)
    'future_weather',      -- future exogenous table
    date,                  -- future date column
    ['temperature'],       -- future exogenous columns
    '1d',                  -- frequency
    'AutoARIMA',           -- model
    7,                     -- horizon
    MAP{}                  -- params (optional)
)
ORDER BY forecast_step
LIMIT 1;
-- model_name reports the selected order with ' with regressors' appended, e.g. 'AutoARIMA(3,0,1)(0,1,0)[9] with regressors'
```

---

## Panel / Global Forecasting (`ts_forecast_panel_by`)

Global cross-series learners that fit **one shared model across all series simultaneously** and predict per-series forecasts. Unlike `ts_forecast_by` (N independent fits), panel models pool the parameter optimization across the full panel — making them faster and better-regularized when individual series are short.

**Three supported methods:**

| Method | Description | When to Use |
|--------|-------------|-------------|
| `'GlobalETS'` | Pooled ETS with automatic spec selection (`GlobalAutoETS`) | Many related series with shared seasonal dynamics |
| `'GlobalTheta'` | Pooled Theta (Standard Theta Method, theta=2.0) | Trended panels; no seasonal config needed |
| `'GlobalCroston'` | Pooled Croston (Classic or SBA bias correction) | Intermittent/spare-parts panels with many zeros |

### Signature

```sql
ts_forecast_panel_by(
    source_table  VARCHAR,     -- table name (quoted string)
    group_col     IDENTIFIER,  -- series identifier (unquoted)
    date_col      IDENTIFIER,  -- date/timestamp column (unquoted)
    target_col    IDENTIFIER,  -- value column (unquoted)
    method        VARCHAR,     -- 'GlobalETS' | 'GlobalTheta' | 'GlobalCroston'
    horizon       INTEGER,     -- periods to forecast
    frequency     VARCHAR,     -- '1d', '1h', '1mo', ...
    params        MAP{}        -- optional; see per-method params below
) → TABLE(group_col, forecast_step INT, date_col TIMESTAMP, yhat DOUBLE, model_name VARCHAR)
```

### Ragged panel handling

Series with different lengths or start dates are automatically handled:
1. A shared date grid is built as the **union of all dates** across the panel, at the declared `frequency`.
2. Each series is aligned to the shared grid; gaps are filled with NaN and then imputed — interior gaps via linear interpolation, leading/trailing gaps via the nearest observed value (constant edge extension, D-14, G26).
3. Series with **fewer than 10 valid observations** after alignment are **dropped** and emitted as `DROPPED: too_short` rows (not as errors).
4. At least **3 series** must survive the drop step for the global fit to proceed.

### Point forecasts only (v1)

`ts_forecast_panel_by` returns point forecasts only — `yhat_lower` / `yhat_upper` are not available in this release. Prediction intervals via conformal prediction are planned for a future increment (D-Area3).

### Method-specific params

**GlobalETS:**
```text
MAP {
    'seasonal_period': '7',    -- period for seasonal ETS (0 or omit = non-seasonal only)
    'model_pool': 'Reduced'    -- 'Reduced' (8 candidates, default) or 'Complete' (19)
}
```

**GlobalTheta:** No params accepted (seasonal_period is ignored).

**GlobalCroston:**
```text
MAP {
    'croston_variant': 'SBA'   -- 'Classic' (default) or 'SBA' (bias correction)
}
```

### Examples

```sql
CREATE OR REPLACE TABLE seasonal_panel AS
SELECT uid,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
       50.0 + uid * 5.0 + 10.0 * sin(2 * pi() * i / 7.0) AS y
FROM range(0, 40) t(i)
CROSS JOIN (SELECT unnest([1, 2, 3]) AS uid);

CREATE OR REPLACE TABLE panel_sales AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
       20.0 + product_id * 3.0 + 0.6 * i AS y
FROM range(0, 40) t(i)
CROSS JOIN (SELECT unnest([1, 2, 3]) AS product_id);

CREATE OR REPLACE TABLE panel_intermittent AS
SELECT item_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
       CASE WHEN (i + item_id) % 4 = 0 THEN 2.0 + (i % 3) ELSE 0.0 END AS qty
FROM range(0, 40) t(i)
CROSS JOIN (SELECT unnest([1, 2, 3]) AS item_id);

-- GlobalETS: weekly seasonal panel (verified end-to-end)
SELECT uid AS series, forecast_step, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'seasonal_panel',
    uid,
    ds,
    y,
    'GlobalETS',
    7,
    '1d',
    MAP {'seasonal_period': '7'}
)
ORDER BY series, forecast_step;

-- GlobalTheta: trended panel, no seasonal config (verified end-to-end)
SELECT product_id, forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_sales',
    product_id,
    ds,
    y,
    'GlobalTheta',
    14,
    '1d'
)
ORDER BY product_id, forecast_step;

-- GlobalCroston SBA: intermittent demand panel (verified end-to-end)
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d',
    MAP {'croston_variant': 'SBA'}
)
ORDER BY item_id, forecast_step;

-- Method comparison (GlobalETS vs GlobalTheta on same panel)
SELECT 'GlobalETS' AS method, product_id, forecast_step, ROUND(yhat, 2) AS yhat
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 7, '1d')
UNION ALL
SELECT 'GlobalTheta', product_id, forecast_step, ROUND(yhat, 2)
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 7, '1d')
ORDER BY product_id, method, forecast_step;
```

### Per-method reference

- [GlobalETS](../reference/models/exponential-smoothing/global_ets.md) — pooled ETS with optional seasonality
- [GlobalTheta](../reference/models/theta/global_theta.md) — pooled Theta, minimal config
- [GlobalCroston](../reference/models/intermittent/global_croston.md) — pooled Croston for intermittent demand

---

## Classical Models — GARCH and Kalman

Univariate models for conditional volatility (GARCH) and state-space smoothing (Kalman),
accessible via the standard `ts_forecast_by` surface with `method = 'GARCH'` or `method = 'Kalman'`.

### GARCH — Conditional Volatility Forecasting

> **forecast_value (yhat) is conditional VOLATILITY (std-dev), not variance.**
> `yhat = sqrt(forecast_variance(h))`. Square it for variance: `yhat * yhat`.

Use GARCH on **returns** (first differences), not raw price levels. GARCH models volatility
clustering in financial time series: periods of high volatility tend to be followed by high
volatility, and low by low.

**Default:** GARCH(1,1). Override `p` and `q` via the `params` MAP.

```sql
-- GARCH(1,1) — conditional volatility on returns (verified end-to-end)
CREATE OR REPLACE TABLE returns AS
    SELECT 'Asset_A' AS asset_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           0.5 * SIN(i * 0.7) + 0.3 * COS(i * 0.3) AS y
    FROM range(40) t(i);

SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d')
ORDER BY asset_id, forecast_step;

-- GARCH(1,1) — explicit p=1, q=1 via params
SELECT asset_id, forecast_step, ds, ROUND(yhat, 6) AS conditional_volatility, model_name
FROM ts_forecast_by(
    'returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'}
)
ORDER BY asset_id, forecast_step;
```

**Minimum observations:** p + q + 10 (GARCH(1,1): 12 minimum).

See [GARCH reference](../reference/models/classical/garch.md) for full parameter docs and pitfalls.

### Kalman — State-Space Smoothing + h-Step Forecasting

Two state-space specifications via the `kalman_model` param:

| Spec | `kalman_model` value | Description |
|------|----------------------|-------------|
| Local level | `'local_level'` (default) | Random walk + noise; flat h-step forecast |
| Local linear trend | `'local_linear_trend'` | Level + slope; linearly growing/shrinking forecast |

```sql
-- Kalman local_level (default) — verified end-to-end
CREATE OR REPLACE TABLE sales AS
    SELECT 'Product_X' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.8 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM range(30) t(i);

SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 7, '1d')
ORDER BY product_id, forecast_step;

-- Kalman local_linear_trend — captures trend direction
SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'Kalman', 7, '1d',
    params := MAP{'kalman_model': 'local_linear_trend'}
)
ORDER BY product_id, forecast_step;
```

See [Kalman reference](../reference/models/state-space/kalman.md) for full docs.

---

## Ensemble Forecasting — AutoEnsemble

`AutoEnsemble` automatically fits three candidate models (AutoARIMA, AutoETS, AutoTheta),
ranks them by in-sample MSE, and combines the top-K members into a single forecast using
the specified `combination_method`. It is accessible via `ts_forecast_by` with
`method = 'AutoEnsemble'`.

> **Prediction intervals:** `yhat_lower` / `yhat_upper` are the crate `AutoEnsemble`'s own
> `predict_with_intervals` bounds at `confidence_level` (default 0.90) — not NULL, and not a
> combination of the three members' own individual intervals. Member/weight introspection is
> also available — see `ts_auto_ensemble_inspect_by` below.

### Parameters

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `top_k` | INTEGER | `3` | Number of top-ranked candidates to combine |
| `combination_method` | VARCHAR | `'mean'` | Blend strategy (see table below) |
| `seasonal_period` | INTEGER | `0` | Period passed to all three candidate models; `0` = non-seasonal |
| `confidence_level` | DOUBLE | `0.90` | Confidence level for the combined forecast's own prediction interval |

### combination_method strings

| SQL string(s) | Strategy |
|---|---|
| `''`, `'mean'` | Unweighted arithmetic mean of member forecasts |
| `'median'` | Coordinate-wise median; robust to outlier members |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weighting |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weighting |
| `'stacking'`, `'stack'` | Ridge-regularised stacking weights from in-sample holdout |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-step weights from rolling-origin errors |

All strings are case-insensitive. The string `'custom'` is **not** accepted (ENS-F1, deferred).

### Examples (verified end-to-end)

```sql
-- Basic usage — default mean combination, non-seasonal (verified end-to-end)
CREATE OR REPLACE TABLE sales AS
SELECT
    'Product_A' AS product_id,
    '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
    10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'AutoEnsemble', 5, '1d')
ORDER BY product_id, forecast_step;

-- Weighted MSE combination — better-fitting members get higher weight
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'weighted_mse', seasonal_period: 0}
)
ORDER BY product_id, forecast_step;

-- Stacking combination — holdout-fitted weights
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'stacking', seasonal_period: 0}
)
ORDER BY product_id, forecast_step;
```

See [AutoEnsemble reference](../reference/models/ensemble/autoensemble.md) for the full
parameter docs, all six `combination_method` strings + aliases, limitations, and choosing
between combination strategies.

---

## Explicit-Member Ensemble Forecasting (`ts_forecast_ensemble_by`)

`ts_forecast_ensemble_by` fits each **user-named** member model independently on every
series and combines the out-of-sample forecasts using the specified `combination_method`.
Unlike `AutoEnsemble` (which fixes the candidates to AutoARIMA / AutoETS / AutoTheta and
auto-selects by in-sample MSE), the explicit-member form gives you full control over which
models contribute to the blend.

**Minimum 2 members.** **26 supported members** (same model-name vocabulary as
`ts_forecast_by`). **10 blocked members** raise `InvalidParameter` naming the model (GARCH,
Laplace, ARIMA, MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS, AutoEnsemble).

> **Prediction intervals:** `yhat_lower` / `yhat_upper` are the crate `Ensemble`'s own
> `predict_with_intervals` bounds at `confidence_level` (default 0.90) — not NULL. Member/
> weight introspection is also available — see `ts_ensemble_inspect_by` below.

### Signature

```sql
ts_forecast_ensemble_by(
    source       VARCHAR,      -- source table (quoted string)
    group_col    IDENTIFIER,   -- series identifier (unquoted)
    date_col     IDENTIFIER,   -- date/timestamp column (unquoted)
    target_col   IDENTIFIER,   -- value column (unquoted)
    members      VARCHAR[],    -- 2+ model names, e.g. ['AutoARIMA','AutoETS']
    horizon      INTEGER,      -- periods to forecast
    frequency    VARCHAR,      -- time step, e.g. '1d', '1mo'
    combination_method := '',  -- blend strategy; default '' = 'mean'
    seasonal_period := 0,      -- shared period for all members; 0 = non-seasonal
    confidence_level := 0.90   -- confidence level for the combined forecast's interval
) → TABLE(group_col, forecast_step INT, date_col TIMESTAMP, yhat DOUBLE,
          yhat_lower DOUBLE, yhat_upper DOUBLE, model_name VARCHAR)
```

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `members` | VARCHAR[] | — | 2+ model names; same vocabulary as `ts_forecast_by` method param |
| `combination_method` | VARCHAR | `''` (= `'mean'`) | Blend strategy (same strings as AutoEnsemble) |
| `seasonal_period` | INTEGER | `0` | Period shared by all members; `0` = non-seasonal; `p>1` = seasonal; `0` now auto-detects the same way `ts_forecast_by` does when no member requires an explicit period |
| `confidence_level` | DOUBLE | `0.90` | Confidence level for the combined forecast's own prediction interval |

### combination_method strings

| SQL string(s) | Strategy |
|---|---|
| `''`, `'mean'` | Unweighted arithmetic mean of member forecasts |
| `'median'` | Coordinate-wise median; robust to outlier members |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weighting |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weighting |
| `'stacking'`, `'stack'` | Ridge-regularised stacking weights from in-sample holdout |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-step weights from rolling-origin errors |

### Examples (verified end-to-end)

```sql
-- 60-observation linear series for reliable member convergence
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- Basic usage — mean combination, non-seasonal (verified end-to-end)
SELECT forecast_step, ROUND(yhat, 4) AS yhat, yhat_lower, yhat_upper, model_name
FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY forecast_step;

-- Weighted MSE — better-fitting members get higher weight
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
ORDER BY forecast_step;

-- Seasonal ensemble — SeasonalNaive + HoltWinters with weekly data
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['SeasonalNaive', 'HoltWinters'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 12
)
ORDER BY forecast_step;
```

See [ensemble_explicit reference](../reference/models/ensemble/ensemble_explicit.md) for the
full parameter docs, all 26 supported members, the 10 blocked members and their alternatives,
the six `combination_method` strings with aliases, error examples, and limitations.

---

## Ensemble Member Introspection

Two functions expose the per-member weights and scores after fitting an ensemble — one for
explicit-member ensembles, one for AutoEnsemble. Both return **long format**: one row per
member per series group. They require the same data table as their corresponding forecast
functions; no separate `members` or `top_k` inference step is needed.

### `ts_ensemble_inspect_by` — explicit-member combination weights

Returns each named member's combination weight after fitting the ensemble on each series.
The `score` column is `NULL` (explicit-member inspection has no per-member MSE score in the
current implementation — use `ts_auto_ensemble_inspect_by` for scored introspection).

**Signature:**
```sql
ts_ensemble_inspect_by(
    source         VARCHAR,      -- source table (quoted string)
    group_col      IDENTIFIER,   -- series identifier (unquoted)
    date_col       IDENTIFIER,   -- date/timestamp column (unquoted)
    target_col     IDENTIFIER,   -- value column (unquoted)
    members        VARCHAR[],    -- 2+ model names (same vocab as ts_forecast_by)
    combination_method := '',    -- blend strategy; default '' = 'mean'
    seasonal_period := 0         -- shared period; 0 = non-seasonal
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE, score DOUBLE)
```

**Parameters:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `members` | VARCHAR[] | — | 2+ model names; same vocabulary as `ts_forecast_by` method param |
| `combination_method` | VARCHAR | `''` (= `'mean'`) | Blend strategy (same strings as `ts_forecast_ensemble_by`) |
| `seasonal_period` | INTEGER | `0` | Period shared by all members; `0` = non-seasonal |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `member_name` | VARCHAR | Model name (one row per member per series) |
| `weight` | DOUBLE | Combination weight; sums to 1.0 per group for all methods; `Mean` → equal `1/k` |
| `score` | DOUBLE | `NULL` for explicit-member (no per-member MSE in current impl) |

**Weight properties by combination_method:**
- `mean` / `''`: each member gets `1/k` (uniform); sum == 1.0 within floating-point tolerance
- `median`: each member gets `1/k` (Median does not use weighted combination)
- `weighted_mse`: inverse-MSE weights; non-negative; sum == 1.0
- `inverse_aic`: AIC-based weights; non-negative; sum == 1.0
- `stacking`: simplex-projected weights; non-negative; sum == 1.0
- `horizon_adaptive`: AVERAGE of per-step weights; sum == 1.0 (per-step matrix not exposed)

**Examples (verified end-to-end):**
```sql
-- 60-observation linear series
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- Mean combination: each member gets weight = 1/3
SELECT * FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, member_name;
-- Returns: id=1, AutoARIMA weight=0.333, AutoETS weight=0.333, Theta weight=0.333

-- WeightedMSE: weights proportional to inverse in-sample MSE
SELECT id, member_name, ROUND(weight, 6) AS weight, score
FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
ORDER BY id, member_name;
-- Returns: weight sums to 1.0 per group; best-fitting member gets highest weight

-- Assert weight sum = 1.0 per series
SELECT id, SUM(weight) AS weight_sum
FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
GROUP BY id;
-- Returns: weight_sum = 1.0 for every series
```

See [ensemble_inspect reference](../reference/models/ensemble/ensemble_inspect.md) for the
full parameter docs, worked examples for all six combination methods, and limitations.

---

### `ts_auto_ensemble_inspect_by` — AutoEnsemble member scores and optional weights

Returns the selected top-K member models with their in-sample MSE scores and (for `Mean`
combination only) equal combination weights. The `rank` column orders members from
best-fitting (rank=1) to worst-fitting (rank=k) by ascending MSE.

**Signature:**
```sql
ts_auto_ensemble_inspect_by(
    source         VARCHAR,      -- source table (quoted string)
    group_col      IDENTIFIER,   -- series identifier (unquoted)
    date_col       IDENTIFIER,   -- date/timestamp column (unquoted)
    target_col     IDENTIFIER,   -- value column (unquoted)
    top_k := 3,                  -- number of top candidates to select
    combination_method := '',    -- blend strategy; default '' = 'mean'
    seasonal_period := 0         -- period for all three candidate models; 0 = non-seasonal
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE, score DOUBLE, rank BIGINT)
```

**Parameters:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `top_k` | INTEGER | `3` | Number of top-ranked candidates to combine (same as `ts_forecast_by` AutoEnsemble param) |
| `combination_method` | VARCHAR | `''` (= `'mean'`) | Blend strategy. **Note:** `''` maps to `'mean'`, not `'weighted_mse'`. Pass `'weighted_mse'` explicitly for weighted combination. |
| `seasonal_period` | INTEGER | `0` | Period passed to all three candidate models; `0` = non-seasonal |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `member_name` | VARCHAR | Selected model name (`AutoARIMA`, `AutoETS`, or `AutoTheta`) |
| `weight` | DOUBLE | `1/k` for `Mean` combination; `NULL` for all other methods (anofox-forecast ≤0.15.10 limitation — see below) |
| `score` | DOUBLE | In-sample MSE from `all_scores()`; lower is better; always > 0 (or near-zero for near-perfect fits) |
| `rank` | BIGINT | 1..k ascending by MSE; rank 1 = best-fitting member |

**Weight availability by combination_method:**

| combination_method | weight column |
|---|---|
| `''` or `'mean'` | `1/k` (non-NULL) — equal weights for Mean combination |
| `'weighted_mse'`, `'inverse_aic'`, `'stacking'`, `'horizon_adaptive'` | `NULL` — inner combination weights are not accessible from the `anofox-forecast` ≤0.15.10 public API (upstream limitation; `AutoEnsemble.ensemble` is a private field) |

**Row count:** May be less than `top_k` if fewer than `top_k` candidate models fitted
successfully (e.g., AutoARIMA convergence failure). The returned `k` is `model_count()`
from the fitted AutoEnsemble.

**Examples (verified end-to-end):**
```sql
-- AutoEnsemble Mean: weight = 1/3, score > 0, rank in (1,2,3)
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, rank;
-- Returns: 3 rows per series; weight=0.333 for all; rank=1 is lowest-MSE member

-- AutoEnsemble WeightedMSE: weight IS NULL (anofox-forecast ≤0.15.10 limitation), score > 0
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'weighted_mse',  -- must be explicit: '' maps to 'mean'
    seasonal_period := 0
)
ORDER BY id, rank;
-- Returns: 3 rows per series; weight=NULL; score shows MSE ranking

-- Find the best member per series (rank=1)
SELECT id, member_name, score
FROM ts_auto_ensemble_inspect_by('ae_test', id, ds, y)
WHERE rank = 1
ORDER BY id;
```

See [ensemble_inspect reference](../reference/models/ensemble/ensemble_inspect.md) for the
full docs, the AutoEnsemble weight-NULL limitation (with upstream-enhancement note), and
the `model_count() < top_k` behavior.

---

## Multivariate Forecasting (`ts_forecast_var_by`)

VAR (Vector Autoregression) fits **one model across K variables simultaneously**,
capturing cross-variable dynamics. Unlike `ts_forecast_by` (one model per series per group),
`ts_forecast_var_by` uses all variable columns together in a single multivariate fit.

> **v1 constraint:** Single-panel only (no `group_col`). One VAR fit over the entire input table.
> **Lag order:** Use named param `p` (not `order` — SQL reserved word).
> **Output:** Long format — one row per (variable, horizon step).

### Signature

```sql
ts_forecast_var_by(
    source     VARCHAR,     -- source table name (quoted string)
    date_col   VARCHAR,     -- date column name (quoted string)
    value_cols VARCHAR[],   -- array of value column names
    horizon    INTEGER,     -- periods to forecast
    frequency  VARCHAR,     -- time step between observations
    p          INTEGER,     -- lag order (named param, default: 1)
    params     MAP          -- reserved for future use (default: MAP{})
)
→ TABLE(variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)
```

### Examples (verified end-to-end)

```sql
-- VAR(1) — 2-variable system, 14-step ahead, long format output
CREATE OR REPLACE TABLE var_src AS
    SELECT
        (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
        (0.6 * SIN(i * 0.4) + 0.1 * COS(i * 0.2)) AS y1,
        (0.05 * SIN(i * 0.4) + 0.7 * COS(i * 0.2)) AS y2
    FROM range(60) t(i);

-- Returns 2 variables * 14 steps = 28 rows in long format
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d')
ORDER BY variable, forecast_step;

-- VAR(2) — higher lag order for longer-range cross-variable dynamics
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d', p:=2)
ORDER BY variable, forecast_step;
```

### Key notes

- **Input format:** Wide — one column per variable, one row per time point. Use standard DuckDB
  pivoting (`PIVOT`) to convert long format to wide before calling `ts_forecast_var_by`.
- **Output format:** Long — `(variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)`.
  Use DuckDB `PIVOT` on `variable` to convert back to wide format.
- **Null handling:** Missing values are imputed via linear interpolation before fitting.
  All `value_cols` must have the same number of valid observations after imputation.
- **Minimum obs:** n > k × p + 1 (n = valid obs, k = number of variables, p = lag order).

See [VAR reference](../reference/models/multivariate/var.md) for full docs, pitfalls, and benchmark results.

---

## Explainability — inspecting fit state and decomposing forecasts

Two macros expose the crate's `Inspectable` and `Explainable` surfaces
as wide-STRUCT tables. Both require the `json` extension (auto-loaded
if `autoload_known_extensions` is on).

### `ts_forecast_inspect_by` — fit-state snapshot per group

Returns one row per group with a nullable, uniformly-typed STRUCT
`inspection` that carries the union of every family's payload.
`model_family` names which fields are populated:

| `model_family` | Populated fields |
|----------------|------------------|
| `Ets` | `spec`, `alpha`, `beta`, `gamma`, `phi`, `seasonal_period`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Arima` | `order_p/d/q`, `seasonal_order_P/D/Q/s`, `coefficients`, `aic`, `bic`, `fitted_values`, `residuals` |
| `Theta` | `variant`, `theta`, `alpha`, `seasonal_period`, `fitted_values`, `residuals` |
| `Tbats` | `seasonal_periods`, `box_cox_lambda`, `selected_config`, `aic`, `fitted_values`, `residuals` |
| `Mfles` | `seasonal_period`, `max_rounds`, `multiplicative`, `penalty`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Mstl` | `seasonal_periods`, `iterations`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Laplace` | `leaf_names`, `leaf_weights`, `horizon_dists_json`, `fitted_values`, `residuals` |
| `Regression` | `backend`, `feature_names`, `coefficients`, `intercept`, `coef_std_errors`, `intercept_std_error`, `r_squared`, `fitted_values` |

All other fields are `NULL` for a given family. `raw_json` carries the
untransformed serde payload for callers that need shapes the wide
struct doesn't expose (nested Laplace `GaussianMixture` state, etc.).

**Supported models (fit state):** `AutoETS`, `AutoARIMA`, `AutoTheta`,
`AutoTBATS`, `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `Laplace`. Any
other model errors with `does not implement Inspectable`.

```sql
-- ETS: read out per-group smoothing parameters
SELECT product_id,
       (inspection).spec,
       (inspection).alpha,
       (inspection).gamma
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoETS',
    {seasonal_period: 12});

-- Laplace: see the ensemble the shell picked and each leaf's weight
SELECT product_id,
       list_zip((inspection).leaf_names, (inspection).leaf_weights)
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'Laplace',
    {seasonal_period: 12, laplace_variant: 'auto_aid'});

-- ARIMA: pull the selected order and BIC
SELECT product_id,
       (inspection).order_p, (inspection).order_d, (inspection).order_q,
       (inspection).bic
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoARIMA',
    {seasonal_period: 12});
```

### `ts_forecast_explain_by` — per-horizon forecast decomposition

Returns per-group `decomposition` STRUCT with `level` / `trend` /
`seasonal` / `residual` (each a `DOUBLE[]` of length `horizon`; absent
components are `NULL`) plus `named_components_json` for family-specific
extras. `raw_json` carries the untransformed payload.

**Supported models (decomposition):** `ETS` (fixed spec), `MSTL`,
`AutoMSTL`, `Theta`. Other models error with `does not implement Explainable`.

```sql
-- Reconstruct: yhat = level + trend + seasonal + residual
SELECT product_id,
       (decomposition).level,
       (decomposition).trend,
       (decomposition).seasonal
FROM ts_forecast_explain_by('sales', product_id, ds, y, 'ETS', 12,
    {seasonal_period: 12});
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
