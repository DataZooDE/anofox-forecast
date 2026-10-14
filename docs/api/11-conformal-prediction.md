# Conformal Prediction

> Distribution-free prediction intervals

## Overview

Conformal prediction provides **distribution-free prediction intervals**. Coverage is
guaranteed only under the exchangeability assumption (calibration and test residuals drawn
from the same distribution, independently) — see
[Coverage guarantee and its limits](#coverage-guarantee-and-its-limits) below for the
finite-sample rule and what happens when your calibration set doesn't meet it. Unlike
parametric methods, conformal prediction makes minimal distributional assumptions and, when
exchangeability holds and the calibration set is large enough for the requested coverage
level, provides valid coverage even for finite samples.

**Use this document to:**
- Create prediction intervals targeting a nominal coverage level (e.g., 90%, 95%)
- Calibrate conformity scores from backtest residuals, per group or pooled
- Apply calibrated intervals to new forecasts, with optional per-row difficulty scaling
- Choose between one-step, modular, or array-based approaches
- Build production pipelines that separate calibration from application
- Generate residual-resampling bootstrap prediction intervals as a model-agnostic
  alternative

### How It Works

Conformal prediction works in two phases:
1. **Calibration**: Compute a conformity score from calibration residuals (per group by
   default)
2. **Prediction**: Apply the conformity score to new forecasts, paired by horizon/date

The resulting intervals target coverage of the true value with probability `1 - α`, where `α`
is the miscoverage rate — under exchangeability and a calibration set large enough for that
`α` (see [Coverage guarantee and its limits](#coverage-guarantee-and-its-limits)).

---

## Coverage guarantee and its limits

Split conformal prediction's `1 - α` coverage guarantee holds under exchangeability of
calibration and test residuals, **and** a finite-sample condition on the calibration set
size `n`: the conformity score is the `k/n` empirical quantile of `n` calibration residuals,
where `k = ceil((n + 1)(1 - α))`. That quantile only exists when `k <= n` — otherwise no
finite-sample interval at that coverage level is achievable from `n` residuals, no matter
how they're computed.

**Minimum calibration-set size per method and alpha:**

| alpha | Symmetric / Adaptive minimum n | Asymmetric minimum n (uses alpha/2 per tail) |
|-------|-------------------------------|-----------------------------------------------|
| 0.2   | 4                             | 9 (half-alpha 0.1)                            |
| 0.1   | 9                             | 19 (half-alpha 0.05)                          |
| 0.05  | 19                            | 39 (half-alpha 0.025)                         |

When the calibration set is below the minimum for the requested `alpha`, every surface
reports this explicitly instead of clamping to the maximum observed residual:
`conformity_score`/bounds are `NULL`, `coverage` is `NULL`, and a `calibration_insufficient`
column is `true`. `ts_conformal_predict_per_step` applies this rule **per step** — a fold
count sufficient for one step can be insufficient for another if step-level sample sizes
differ.

```sql
-- n=5 residuals, alpha=0.1 needs n>=9: insufficient -- NULL, not a clamped value
SELECT ts_conformal_quantile([1.0, 2.0, 3.0, 4.0, 5.0]::DOUBLE[], 0.1);
-- NULL

-- the same residuals at alpha=0.2 (minimum n=4): sufficient
SELECT ts_conformal_quantile([1.0, 2.0, 3.0, 4.0, 5.0]::DOUBLE[], 0.2);
-- 5.0
```

`alpha` outside `(0, 1)` is always a hard SQL error naming the value, on every surface that
takes one — never a silent default or a `NULL`:

```sql
SELECT ts_conformal_quantile([1.0, 2.0, 3.0]::DOUBLE[], 0.0);
-- Invalid Input Error: ts_conformal_quantile: alpha must be in (0, 1), got 0
```

`coverage` (on `ts_conformal_by`, `ts_conformal_predict`, `ts_conformal_predict_asymmetric`,
and `ts_conformal_calibrate`) is the **empirical** fraction of calibration residuals within
the computed bound — not simply `1 - alpha`. A separate `nominal_level` column always reports
the requested `1 - alpha` level, so you can compare what you asked for against what the
calibration set actually delivered.

---

## Choose Your Approach

The API offers three levels of abstraction. Choose based on your needs:

| Approach | Functions | Use When |
|----------|-----------|----------|
| **One-Step** | `ts_conformal_by` | You have backtest results and want intervals immediately |
| **Modular** | `ts_conformal_calibrate` + `ts_conformal_apply_by` | You need to reuse calibration across multiple forecasts |
| **Array-Based** | `ts_conformal_predict`, `ts_conformal_quantile`, etc. | You're building custom pipelines or working with array data |

### One-Step Approach

Best for most users. Calibrates and applies intervals in a single call. Requires a
`date_col`/step column so residuals and forecasts are paired by horizon, not by value.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    date,               -- required: orders and pairs every row
    actual,
    forecast,
    point_forecast,
    params := {'alpha': 0.1}  -- 90% coverage
);
```

### Modular Approach

Use when you need to:
- Apply the same calibration to multiple forecast tables
- Store calibration scores for production use
- Separate calibration from application for debugging

```sql
-- Step 1: Calibrate once (per group_col by default)
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', product_id, actual, forecast, params := {'alpha': 0.1}
);

-- Step 2: Apply to any forecast table -- `calibration` is a TABLE name, matched
-- per group (falling back to a pooled row with group_col IS NULL, if present)
SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    date,
    forecast,
    'calibration'
);
```

### Array-Based Functions

Use when you need:
- Fine-grained control over the conformal prediction process
- Integration with custom forecasting pipelines
- Array inputs/outputs for programmatic use

```sql
-- Direct array-based prediction
SELECT r.* FROM (
    SELECT ts_conformal_predict(
        [1.2, -0.8, 1.5, -1.0, 0.5, 0.9, -1.1, 0.6, -0.4]::DOUBLE[],  -- residuals_array
        [100.0, 102.0, 104.0]::DOUBLE[],                              -- forecasts_array
        0.1
    ) AS r
);
```

---

## Common Scenarios

### Scenario 1: Quick Uncertainty Quantification

**Goal:** You have backtest results and need prediction intervals quickly.

**Solution:** Use `ts_conformal_by` for a one-step solution.

```sql
-- Already have backtest results with actual vs forecast, dated
SELECT * FROM ts_conformal_by(
    'my_backtest_results',
    product_id,
    date,
    actual,
    forecast,
    point_forecast,
    params := {'alpha': 0.1}
);
```

### Scenario 2: Production Forecasting System

**Goal:** Calibrate once, apply to daily/weekly forecast batches.

**Solution:** Use modular approach—store calibration, apply repeatedly.

```sql
-- Run once: calibrate from historical backtests
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'historical_backtest', product_id, actual, forecast, params := {'alpha': 0.1}
);

-- Run daily: apply to new forecasts
INSERT INTO forecast_with_intervals
SELECT * FROM ts_conformal_apply_by(
    'todays_forecasts',
    product_id,
    date,
    forecast,
    'calibration'
);
```

### Scenario 3: Skewed Forecast Errors

**Goal:** Your forecasts consistently under-predict (e.g., demand forecasting).

**Solution:** Use asymmetric intervals, which compute separate upper and lower quantiles
from the full signed-residual array (and its negation), instead of one symmetric quantile
over absolute residuals. Asymmetric is not guaranteed tighter than symmetric for every
residual distribution — measure width empirically for your own data.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    date,
    actual,
    forecast,
    point_forecast,
    params := {'alpha': 0.1, 'method': 'asymmetric'}
);
```

### Scenario 4: Model Comparison

**Goal:** Compare which model produces sharper intervals while maintaining coverage.

**Solution:** Use evaluation functions to compare interval quality.

```sql
-- Generate intervals for both models
CREATE TABLE model_a_intervals AS ...;
CREATE TABLE model_b_intervals AS ...;

-- Compare interval widths (date_col pairs lower/upper -- never sorted independently)
SELECT 'Model A' AS model, * FROM ts_interval_width_by(
    'model_a_intervals', product_id, date, lower, upper
)
UNION ALL
SELECT 'Model B' AS model, * FROM ts_interval_width_by(
    'model_b_intervals', product_id, date, lower, upper
);
```

### Scenario 5: Custom Array-Based Pipeline

**Goal:** Working with pre-aggregated array data in a custom pipeline.

**Solution:** Use scalar functions directly.

```sql
-- Data already in array form
WITH series_data AS (
    SELECT
        product_id,
        list(actual ORDER BY date) AS actuals,
        list(forecast ORDER BY date) AS forecasts,
        list(actual - forecast ORDER BY date) AS residuals
    FROM backtest_results
    WHERE actual IS NOT NULL
    GROUP BY product_id
)
SELECT
    series_data.product_id,
    r.*
FROM series_data, LATERAL (
    SELECT ts_conformal_predict(residuals, forecasts, 0.1) AS r
);
```

### Scenario 6: Adaptive Intervals Scaled by Difficulty

**Goal:** Some periods are inherently harder to forecast (e.g., higher recent volatility),
and you want their intervals to widen accordingly — without a batch-dependent calculation.

**Solution:** Pass a per-row difficulty column at both calibration and prediction time.
Scores are `|residual| / difficulty`; bounds are `point ± q * difficulty` using the
difficulty **as given** — a row's interval never depends on which other forecasts are in
the same batch.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    date,
    actual,
    forecast,
    point_forecast,
    params := {'method': 'adaptive'},
    difficulty_col := recent_volatility
);
```

---

## Complete Workflow Example

A full workflow from raw data to conformal intervals:

```sql
-- Step 0: Detect seasonality (e.g., weekly = 7)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- Step 1: Generate backtest results with AutoETS (two-step cross-validation)
CREATE OR REPLACE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', product_id, date, value, 5, 7, MAP{});
CREATE OR REPLACE TABLE backtest AS
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, value,
    'AutoETS', MAP{'seasonal_period': '7'}
);

-- Step 2: Calibrate conformity score per product (actual is 'y', forecast is 'yhat')
CREATE OR REPLACE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', product_id, y, yhat, params := {'alpha': 0.1}
);

-- Step 3: Generate future forecasts
CREATE OR REPLACE TABLE future AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value, 'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'}
);

-- Step 4: Apply conformal intervals (matched per product_id against `calibration`;
-- ts_forecast_by's output date/forecast columns are 'ds'/'yhat')
SELECT * FROM ts_conformal_apply_by(
    'future',
    product_id,
    ds,
    yhat,
    'calibration'
);
```

---

## Table Macros

> **When to use:** Table macros operate on tables directly and handle grouping automatically. Use these for standard forecasting workflows with tabular data.

### ts_conformal_by

High-level macro for conformal prediction on grouped backtest results. Requires a date/step
column so output rows are paired to the correct horizon step, never independently
value-sorted.

**Signature:**
```sql
ts_conformal_by(backtest_results, group_col, date_col, actual_col, forecast_col,
                 point_forecast_col, params := MAP{}, difficulty_col := NULL)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `group_col` | COLUMN | Yes | — | Series identifier column |
| `date_col` | COLUMN | Yes | — | Date/step column; orders and pairs residuals and forecasts |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Historical forecast column (used to compute calibration residuals) |
| `point_forecast_col` | COLUMN | Yes | — | New point forecast column to wrap with an interval |
| `params` | MAP/STRUCT | No | `MAP{}` | Configuration parameters (see below) |
| `difficulty_col` | COLUMN | No | `NULL` | Per-row difficulty; required for, and only used by, `method := 'adaptive'` |

**Params Options** (read json-free via `params::MAP(VARCHAR, VARCHAR)`; a `{'key': value}`
STRUCT literal casts implicitly — malformed values error instead of silently defaulting):
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage, 0.05 = 95%) |
| `method` | VARCHAR | `'symmetric'` | `'symmetric'` (alias `'split'`), `'asymmetric'`, or `'adaptive'` (requires `difficulty_col`) |

**Returns:** `group_col`, `date_col`, `point`, `lower`, `upper`, `coverage` (empirical),
`nominal_level`, `conformity_score`, `n_calibration`, `calibration_insufficient`, `method` —
**one row per `(group_col, date_col)`**.

**Examples:**
```sql
-- Symmetric intervals (default)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    date,
    actual,
    forecast,
    point_forecast,
    params := {'alpha': 0.1, 'method': 'symmetric'}
);

-- Asymmetric intervals (for skewed residuals)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    date,
    actual,
    forecast,
    point_forecast,
    params := {'alpha': 0.1, 'method': 'asymmetric'}
);
```

**When to use asymmetric:** Use `'asymmetric'` when residuals are not symmetric (e.g.,
demand forecasts often under-predict more than over-predict). This derives separate upper
and lower margins from the full signed-residual array (and its negation) — it does not
guarantee a *tighter* interval than symmetric split; measure both on your data.

---

### ts_conformal_calibrate

Calibrates a conformity profile from backtest residuals, **per group by default**.

**Signature:**
```sql
ts_conformal_calibrate(backtest_results, group_col, actual_col, forecast_col,
                        params := MAP{}, difficulty_col := NULL)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `group_col` | COLUMN | Yes | — | Series identifier column |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Forecast values column |
| `params` | MAP/STRUCT | No | `MAP{}` | Configuration parameters |
| `difficulty_col` | COLUMN | No | `NULL` | Per-row difficulty for `method := 'adaptive'` |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage) |
| `method` | VARCHAR | `'symmetric'` | `'symmetric'` (alias `'split'`) or `'adaptive'`; `'asymmetric'` errors — two margins can't collapse into one `conformity_score`, use `ts_conformal_by` instead |
| `pooled` | BOOLEAN | `false` | `false`: one row per `group_col` value. `true`: pool every row into ONE row with `group_col = NULL` |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `group_col` | VARCHAR | Group identifier, or `NULL` when `pooled := true` |
| `method` | VARCHAR | The calibrated method (`'symmetric'` / `'adaptive'`) |
| `conformity_score` | DOUBLE | Calibrated score for interval construction; `NULL` when insufficient |
| `coverage` | DOUBLE | **Empirical** coverage on the calibration residuals; `NULL` when insufficient |
| `nominal_level` | DOUBLE | The requested `1 - alpha` level |
| `n_calibration` | BIGINT | Number of calibration residuals actually used |
| `calibration_insufficient` | BOOLEAN | `true` when `ceil((n+1)(1-alpha)) > n` — see [Coverage guarantee and its limits](#coverage-guarantee-and-its-limits) |

**Example:**
```sql
-- Per-group calibration (default): different residual scales get different quantiles
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',
    product_id,
    actual,
    forecast,
    params := {'alpha': 0.05}
);

-- Pool every group into one quantile
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',
    product_id,
    actual,
    forecast,
    params := {'alpha': 0.05, 'pooled': true}
);
```

---

### ts_conformal_apply_by

Applies a calibration TABLE (the shape `ts_conformal_calibrate` returns) to new forecasts.

**Signature:**
```sql
ts_conformal_apply_by(forecast_results, group_col, date_col, forecast_col, calibration,
                       difficulty_col := NULL)
```

**Parameters:**
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `forecast_results` | VARCHAR | Yes | Table with forecast results |
| `group_col` | COLUMN | Yes | Series identifier column |
| `date_col` | COLUMN | Yes | Date/step column; orders output rows |
| `forecast_col` | COLUMN | Yes | Point forecast column |
| `calibration` | VARCHAR | Yes | **Name of a table** holding `ts_conformal_calibrate`'s output columns (`group_col`, `method`, `conformity_score`, ...) |
| `difficulty_col` | COLUMN | No | Per-row prediction difficulty for `method := 'adaptive'` |

Each forecast group is matched to the calibration row whose `group_col` equals it, if one
exists; otherwise to the pooled row (`group_col IS NULL`), if one exists; otherwise its
bounds come back `NULL`. **A literal score is applied by wrapping it in a one-row table**
with `group_col = NULL`:

```sql
CREATE TABLE literal_calibration AS
SELECT NULL::VARCHAR AS group_col, 'symmetric' AS method, 15.0 AS conformity_score;

SELECT * FROM ts_conformal_apply_by(
    'future_forecasts', product_id, date, forecast, 'literal_calibration'
);
```

**Returns:** `group_col`, `date_col`, `point`, `lower`, `upper`, `method`,
`conformity_score` — **one row per `(group_col, date_col)`**.

**Example:**
```sql
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', product_id, actual, forecast, params := {'alpha': 0.1}
);

SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    date,
    forecast,
    'calibration'
);
```

---

### ts_interval_width_by

Computes mean interval width for grouped forecast results, pairing `lower_col`/`upper_col`
by `date_col` through ONE date-ordered `LIST` of per-row structs — never two
independently-sorted lists.

**Signature:**
```sql
ts_interval_width_by(results, group_col, date_col, lower_col, upper_col)
```

**Returns:** `group_col`, `mean_width`, `n_intervals`

**Example:**
```sql
SELECT * FROM ts_interval_width_by(
    'forecast_results',
    product_id,
    date,
    lower,
    upper
);
```

---

## Scalar Functions

Scalar functions operate on arrays and return scalar values or structs. They are the building blocks used internally by the table macros.

### When to Use Scalar Functions

| Scenario | Recommended Functions |
|----------|----------------------|
| **Custom pipeline with array data** | `ts_conformal_predict`, `ts_conformal_predict_asymmetric` |
| **Need just the quantile score** | `ts_conformal_quantile` |
| **Apply pre-computed score to arrays** | `ts_conformal_intervals` |
| **Evaluate interval quality** | `ts_conformal_coverage`, `ts_conformal_evaluate`, `ts_mean_interval_width` |
| **Multi-level calibration profiles** | `ts_conformal_learn` / `ts_conformal_apply` |
| **Model-agnostic, distribution-free intervals without a fitted residual quantile** | `ts_bootstrap_intervals`, `ts_bootstrap_quantiles` |

---

### NULL handling

Every scalar function in this document is marshalled through the shared `ListDoubleReader` /
`ScalarArgReader` (constant/dictionary-safe) and applies one of four NULL-missing-data policy
classes, decided in Rust — never a silent C++ splice:

- **Residual pools** (`residuals[]` in `ts_conformal_quantile`, `ts_conformal_predict`,
  `ts_conformal_predict_asymmetric`, `ts_conformal_learn`, `ts_bootstrap_intervals`,
  `ts_bootstrap_quantiles`) drop a `NULL`/`NaN` residual before computing — a `NULL` residual
  is dropped the same way a metric function drops an incomplete pair.
- **Paired evaluation inputs** (`ts_conformal_coverage`'s `actual`/`lower`/`upper`,
  `ts_conformal_evaluate`'s same three, `ts_mean_interval_width`'s `lower`/`upper`, and each
  per-step `(forecast, actual)` pair inside `ts_conformal_predict_per_step`) use
  pairwise-complete observations: a position is dropped only when at least one side of the
  pair is `NULL`/`NaN` at that position.
- **Forecast positions** that an interval is built around (`ts_conformal_intervals`,
  `ts_conformal_predict(_asymmetric)`, `ts_conformal_apply`, `ts_conformal_predict_per_step`,
  both bootstrap functions) are position-preserving: a `NULL` forecast at position `k` yields
  `NULL` at position `k` in every output array (`point`/`lower`/`upper`), never a splice that
  shifts later positions into earlier ones.
- **Parameter lists** (`ts_conformal_learn`'s `alphas`, `ts_conformal_apply`'s calibration
  profile arrays, `ts_bootstrap_quantiles`'s `quantile_levels`) reject a `NULL` entry as a
  malformed argument, not data — the error names the parameter and the 1-based position.

```sql
-- Residual pool: a NULL residual is dropped, same quantile as the complete list.
SELECT ts_conformal_quantile([1, NULL, 2, 3]::DOUBLE[], 0.1) = ts_conformal_quantile([1, 2, 3]::DOUBLE[], 0.1);
-- true

-- Forecast position: NULL at position 2 is NULL at position 2 in both bounds.
SELECT (ts_conformal_intervals([10, NULL, 12]::DOUBLE[], 2.0)).lower;
-- [8.0, NULL, 10.0]
```

**`ts_conformal_predict_per_step` also validates the residual count against `horizon` in
Rust**: the flat `fold_forecasts`/`fold_actuals` length must be an exact multiple of
`horizon`, or the call errors (the number of folds is never silently truncated by integer
division):

```sql
SELECT ts_conformal_predict_per_step(
    [1, 2, 3, 4, 5, 6, 7]::DOUBLE[], [1, 2, 3, 4, 5, 6, 7]::DOUBLE[],
    [1, 2, 3]::DOUBLE[], 0.1, 3
);
-- Invalid Input Error: ts_conformal_predict_per_step: Invalid input: per-step conformal: residual count 7 is not a multiple of horizon 3
```

**`ts_conformal_predict_per_step` also requires `point_forecasts` to have exactly
`horizon` elements** — `point`/`lower`/`upper` all share that one length, so a
mismatched `point_forecasts` errors naming both counts instead of producing
mismatched output-array lengths:

```sql
SELECT ts_conformal_predict_per_step(
    [100,105,110,100,105,110]::DOUBLE[], [101,108,109,101,108,109]::DOUBLE[],
    [100.0,101.0,102.0,103.0,104.0]::DOUBLE[], 0.1, 3
);
-- Invalid Input Error: ts_conformal_predict_per_step: Invalid input: per-step conformal: point_forecasts has 5 elements but horizon is 3
```

**The insufficiency rule applies per step**: with 2 folds and `horizon=3`, alpha `0.1`
(`ceil(3*0.9)=3 > 2`) is insufficient at every step (every `half_widths` entry `NULL`), while
alpha `0.4` (`ceil(3*0.6)=2 <= 2`) is sufficient at every step.

---

### Calibration Functions

These functions compute conformity scores from residuals.

#### ts_conformal_quantile

Computes the empirical quantile of absolute residuals for split conformal prediction.
Returns `NULL` (not a clamped value) when the calibration set is below the alpha's minimum
size — see [Coverage guarantee and its limits](#coverage-guarantee-and-its-limits).

**Signature:**
```sql
ts_conformal_quantile(residuals DOUBLE[], alpha DOUBLE) → DOUBLE
```

**Parameters:**
- `residuals`: Residuals (actual - predicted) from calibration set
- `alpha`: Miscoverage rate (0 < alpha < 1). Use 0.1 for 90% coverage. Outside `(0, 1)` is a
  hard error naming the value.

**Example:**
```sql
SELECT ts_conformal_quantile(
    [1.0, -0.5, 2.0, -1.5, 0.8, 1.2, -0.9, 0.3, 1.7],  -- 9 residuals: alpha=0.1's minimum
    0.1                                                  -- 90% coverage
) AS conformity_score;
```

---

### Prediction Functions

These functions apply conformity scores to generate prediction intervals.

#### ts_conformal_intervals

Applies a pre-computed conformity score to create prediction intervals. The 2-argument form
is symmetric; the 4-argument form takes a method and optional per-row difficulty (for
`'adaptive'`).

**Signature:**
```sql
ts_conformal_intervals(forecasts DOUBLE[], conformity_score DOUBLE)
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE[], method VARCHAR)

ts_conformal_intervals(forecasts DOUBLE[], conformity_score DOUBLE, method VARCHAR,
                        difficulty DOUBLE[])
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE[], method VARCHAR)
```

**Example:**
```sql
SELECT
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).lower AS lower,
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).upper AS upper;
-- Returns: lower=[95,105,115], upper=[105,115,125]

-- Adaptive: difficulty scales the margin per forecast, as given
SELECT (ts_conformal_intervals([100.0, 100.0], 1.905, 'adaptive', [1.0, 2.0])).upper;
-- [101.905, 103.81]
```

---

#### ts_conformal_predict

Full split conformal prediction: computes conformity score and applies to forecasts in one call.

**Signature:**
```sql
ts_conformal_predict(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE)
→ STRUCT(
    point DOUBLE[],
    lower DOUBLE[],
    upper DOUBLE[],
    coverage DOUBLE,
    conformity_score DOUBLE,
    method VARCHAR,
    nominal_level DOUBLE,
    n_calibration BIGINT,
    calibration_insufficient BOOLEAN
)
```

`method` is the legacy name `'split_conformal'` (unchanged by Phase 16 — distinct from the
canonical `'symmetric'`/`'asymmetric'`/`'adaptive'` names the `ts_conformal_learn`/
`ts_conformal_calibrate` family returns). `coverage` is empirical; `nominal_level` reports
the requested `1 - alpha`.

**Example:**
```sql
WITH backtest_residuals AS (
    SELECT [1.2, -0.8, 1.5, -1.0, 0.5, 0.9, -1.1, 0.6, -0.4]::DOUBLE[] AS residuals
),
new_forecasts AS (
    SELECT [100.0, 102.0, 104.0]::DOUBLE[] AS forecasts
)
SELECT
    r.*
FROM backtest_residuals, new_forecasts, LATERAL (
    SELECT ts_conformal_predict(residuals, forecasts, 0.1) AS r
);
```

---

#### ts_conformal_predict_asymmetric

Asymmetric conformal prediction with separate upper and lower quantiles, derived from the
full signed-residual array (and its negation) at the `alpha/2` and `1 - alpha/2` quantile
levels — not a positive/negative residual subset.

**Signature:**
```sql
ts_conformal_predict_asymmetric(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE)
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE,
         conformity_score DOUBLE, method VARCHAR, nominal_level DOUBLE,
         n_calibration BIGINT, calibration_insufficient BOOLEAN)
```

**Use Case:** When residuals are not symmetric (e.g., skewed demand forecasts). Because this
halves `alpha` per tail, it needs roughly double the calibration-set size of symmetric at
the same `alpha` (see the minimum-n table above).

**Example:**
```sql
SELECT r.* FROM (
    SELECT ts_conformal_predict_asymmetric(
        [-1.0,-0.8,-0.6,-0.4,-0.2,0.0,0.2,0.4,0.6,0.8,1.0,1.2,1.4,1.6,1.8,2.0,3.0,4.0,5.0]::DOUBLE[],
        [100.0, 110.0]::DOUBLE[],  -- forecasts
        0.1              -- 90% coverage; n=19 is this alpha's asymmetric minimum
    ) AS r
);
```

---

### Evaluation Functions

These functions assess the quality of prediction intervals.

#### ts_mean_interval_width

Computes the mean width of prediction intervals. Narrower intervals are better (more precise) as long as coverage is maintained.

**Signature:**
```sql
ts_mean_interval_width(lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Example:**
```sql
SELECT
    ts_mean_interval_width([95.0, 105.0], [105.0, 115.0]) AS model_a_width,
    ts_mean_interval_width([90.0, 100.0], [110.0, 120.0]) AS model_b_width;
-- model_a_width=10.0, model_b_width=20.0 (model A has sharper intervals)
```

---

#### ts_conformal_coverage

Computes the empirical coverage of prediction intervals. Coverage should be close to `1 - alpha` (e.g., 90% for alpha=0.1).

**Signature:**
```sql
ts_conformal_coverage(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Returns:** Fraction of actuals within [lower, upper] bounds.

**Example:**
```sql
SELECT ts_conformal_coverage(
    [100.0, 110.0, 120.0],  -- actual values
    [95.0, 105.0, 115.0],   -- lower bounds
    [105.0, 115.0, 125.0]   -- upper bounds
) AS coverage;
-- Returns: 1.0 (all values within bounds)
```

---

#### ts_conformal_evaluate

Comprehensive evaluation of conformal prediction intervals. Returns multiple metrics in one call.

**Signature:**
```sql
ts_conformal_evaluate(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[], alpha DOUBLE)
→ STRUCT(coverage DOUBLE, violation_rate DOUBLE, mean_width DOUBLE,
         winkler_score DOUBLE, n_observations INTEGER)
```

**Returns:**
- `coverage`: Empirical coverage rate
- `violation_rate`: Fraction of values outside intervals
- `mean_width`: Average interval width
- `winkler_score`: Winkler score (penalizes width and violations)
- `n_observations`: Number of observations

**Example:**
```sql
SELECT r.* FROM (
    SELECT ts_conformal_evaluate(
        [100.0, 110.0, 120.0]::DOUBLE[],
        [95.0, 105.0, 115.0]::DOUBLE[],
        [105.0, 115.0, 125.0]::DOUBLE[],
        0.1
    ) AS r
);
```

---

### Advanced Calibration

#### ts_conformal_learn / ts_conformal_apply

Learn a calibration profile from residuals, then apply it to forecasts — use this when you
need multiple coverage levels, per-row difficulty, or to reuse a profile across many
`ts_conformal_apply` calls without re-sorting residuals each time.

**Signatures:**
```sql
ts_conformal_learn(residuals DOUBLE[], alphas DOUBLE[], method VARCHAR, strategy VARCHAR)
ts_conformal_learn(residuals DOUBLE[], alphas DOUBLE[], method VARCHAR, strategy VARCHAR,
                    difficulty DOUBLE[])
→ STRUCT(
    method VARCHAR,
    strategy VARCHAR,
    alphas DOUBLE[],
    state_vector DOUBLE[],
    scores_lower DOUBLE[],
    scores_upper DOUBLE[],
    n_residuals BIGINT
)

ts_conformal_apply(forecasts DOUBLE[], profile STRUCT, difficulty DOUBLE[] := NULL)
→ STRUCT(point DOUBLE[], coverage DOUBLE[], lower DOUBLE[], upper DOUBLE[], method VARCHAR)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `residuals` | DOUBLE[] | Calibration residuals (actual - predicted) |
| `alphas` | DOUBLE[] | Array of miscoverage rates to calibrate |
| `method` | VARCHAR | `'symmetric'` (alias `'split'`, `'sym'`), `'asymmetric'` (alias `'asym'`), or `'adaptive'` (alias `'adapt'`) |
| `strategy` | VARCHAR | `'split'` or `'crossval'` (alias `'cross_val'`, `'cv'`) — see Strategies below |
| `difficulty` | DOUBLE[] | Per-row difficulty; required for, and only used by, `method := 'adaptive'` |

**Methods:**
| Method | Description |
|--------|-------------|
| `'symmetric'` | Use absolute residuals for symmetric intervals |
| `'asymmetric'` | Separate upper/lower quantiles from the full signed-residual array (and its negation) |
| `'adaptive'` | Scores `\|residual\| / difficulty`; bounds `point ± q * difficulty`, difficulty as given (no batch-mean normalisation) |

**Strategies:**
| Strategy | Description |
|----------|-------------|
| `'split'` | Simple empirical quantile (default) |
| `'crossval'` (alias `'cross_val'`, `'cv'`) | Assumes residuals already come from CV folds; identical quantile computation to `'split'` |

**`'jackknife+'` (and every removed-alias spelling — `'JackknifePlus'`, `'jackknifeplus'`,
`'jackknife_plus'`, `'jk+'`, case-insensitive) was removed.** It computed the exact same
quantile as `'split'` (a true leave-one-out variant, requiring per-point refits this
project's GROUP BY-oriented architecture doesn't provide, was never implemented under the
removed name). Passing any removed alias now raises a clear error instead of silently
behaving like `'split'`:

```sql
SELECT ts_conformal_learn([1.0, -2.0, 3.0]::DOUBLE[], [0.1]::DOUBLE[], 'symmetric', 'jackknife+');
-- Invalid Input Error: ts_conformal_learn: conformal strategy 'jackknife+' was removed: it
-- computed the same quantile as 'split' (no leave-one-out refits). Use 'split' or 'crossval'.
```

Any other unrecognized `method` or `strategy` string is also a hard error listing the valid
names — never a silent default:

```sql
SELECT ts_conformal_learn([1.0, -2.0, 3.0]::DOUBLE[], [0.1]::DOUBLE[], 'bogus', 'typo');
-- Invalid Input Error: ts_conformal_learn: Unknown conformal method: 'bogus'. Valid:
-- symmetric (alias split, sym), asymmetric (alias asym), adaptive (alias adapt)
```

**Example:**
```sql
-- Learn calibration profile for multiple coverage levels, 9+ residuals (alpha=0.1 minimum)
CREATE TEMP TABLE _r AS SELECT [1.2, -0.8, 1.5, -1.0, 0.5, 0.9, -1.1, 0.6, -0.4, 1.3]::DOUBLE[] AS r;
SELECT ts_conformal_learn(
    r, [0.1, 0.05, 0.2]::DOUBLE[],  -- 90%, 95%, 80% coverage
    'symmetric', 'split'
) AS profile
FROM _r;

-- Apply it to new forecasts
WITH profile AS (
    SELECT ts_conformal_learn(r, [0.1]::DOUBLE[], 'symmetric', 'split') AS p
    FROM _r
)
SELECT ts_conformal_apply([100.0, 105.0]::DOUBLE[], p) AS result
FROM profile;
-- {point: [100.0, 105.0], coverage: [0.9], lower: [98.5, 103.5], upper: [101.5, 106.5], method: symmetric}
```

---

### Bootstrap Prediction Intervals

Residual-resampling bootstrap intervals are a model-agnostic alternative to conformal
prediction: simulate future error paths by resampling historical residuals with
replacement, accumulating them per horizon step (uncertainty grows naturally with horizon),
then take the empirical quantile of the simulated paths at each step.

**Signatures:**
```sql
ts_bootstrap_intervals(residuals DOUBLE[], forecasts DOUBLE[], n_paths INTEGER,
                        coverage DOUBLE, seed BIGINT)
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE)

ts_bootstrap_quantiles(residuals DOUBLE[], forecasts DOUBLE[], n_paths INTEGER,
                        quantiles DOUBLE[], seed BIGINT)
→ STRUCT(point DOUBLE[], quantiles DOUBLE[], values DOUBLE[])
```

**Parameters:**
- `residuals`: Historical residuals (`actual - forecast`)
- `forecasts`: Point forecasts to wrap with intervals (determines the horizon)
- `n_paths`: Number of bootstrap resampling paths. Must be `>= 10` — a smaller value errors
  naming it, instead of silently being floored to 10
- `coverage` (intervals only): Coverage level, e.g. `0.9` for 90%. Must be strictly between 0
  and 1 — a value outside `(0, 1)` errors instead of panicking
- `quantiles` (quantiles only): Quantile levels to compute, each in `[0, 1]`
- `seed`: Random seed for reproducibility (any negative value uses entropy instead)

`n_paths * len(forecasts)` (the simulation sample budget) must be `<= 10,000,000` — a larger
request errors naming both numbers instead of allocating unboundedly.

A right-skewed residual distribution correctly puts the interval's long tail on the **upper**
side (and a left-skewed one on the lower side) — fixed in Phase 16 (D-10); before the fix,
the resampled residual's sign was inverted, mirroring every bound across the point forecast.

**Example:**
```sql
-- A right-skewed residual: the 10% long tail must land ABOVE the point forecast
SELECT (r).lower[1], (r).upper[1]
FROM (SELECT ts_bootstrap_intervals(
    [0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,10.0]::DOUBLE[], [100.0]::DOUBLE[], 2000, 0.9, 42
) AS r);
-- lower=100.0, upper=110.0

SELECT q.* FROM (
    SELECT ts_bootstrap_quantiles(
        [1.0, -0.5, 2.0, -1.5, 0.8, 1.2, -0.9, 0.3, 1.7, -0.6]::DOUBLE[],
        [100.0, 105.0]::DOUBLE[], 1000, [0.1, 0.5, 0.9]::DOUBLE[], 42
    ) AS q
);
```

---

---

## Ensemble Prediction Intervals (EPI-01)

The conformal machinery is **model-agnostic** — it works on any point forecasts, including
ensemble forecasts. No new interval code is needed: the same `ts_cv_folds_by` →
`ts_conformal_calibrate` → `ts_conformal_apply_by` pipeline applies to both
`AutoEnsemble` and explicit-member (`ts_forecast_ensemble_by`) forecasts.

**Model-native alternative:** `Ensemble::predict_with_intervals()` returns a
widest-envelope bound (min of member lowers, max of member uppers). This is model-native
and avoids the CV step, but provides no coverage guarantee. For distribution-free,
coverage-guaranteed intervals, use the conformal path shown here.

### LIMITATIONS (documented)

1. **`ts_cv_forecast_by` ignores `top_k`/`combination_method` for AutoEnsemble.**
   The CV native does not parse these params — it always backtests with `top_k=3, Mean`.
   Additionally, `ts_cv_forecast_by('AutoEnsemble')` crashes at runtime (segfault). The
   workaround is the manual per-fold loop using `_ts_forecast_scalar` shown below.

2. **`ts_forecast_ensemble_by` cannot flow through `ts_cv_forecast_by`.**
   It is a ScalarFunction (not a string-method dispatch). The explicit-member conformal path
   requires the manual fold loop using `_ts_forecast_ensemble_native` per fold.

`ts_conformal_calibrate` calibrates **per series** by default (its `group_col` parameter is
not opt-in), and `ts_conformal_apply_by` matches each forecast row to its own series'
calibration row via a calibration TABLE — both sections below pass the series id as
`group_col`, so `series_A` and `series_B` each get their own `conformity_score`, with no
manual bound arithmetic needed on the apply side.

### AutoEnsemble conformal path (verified)

```sql
-- Shared test data: 2 series, 120 daily observations
CREATE OR REPLACE TABLE ae_series AS
SELECT 1 AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, 10.0 + i * 0.5 AS y
FROM range(120) t(i);

-- Step 1: CV folds
CREATE OR REPLACE TABLE ae_folds AS
SELECT * FROM ts_cv_folds_by('ae_series', id, ds, y, 3, 5, MAP{});

-- Step 2: Manual fold loop (ts_cv_forecast_by crashes for AutoEnsemble — see LIMITATIONS)
CREATE OR REPLACE TABLE ae_bt AS
WITH train AS (
    SELECT id, fold_id, ds, y FROM ae_folds WHERE split = 'train'
),
test AS (
    SELECT id, fold_id, ds, y AS actual,
           ROW_NUMBER() OVER (PARTITION BY id, fold_id ORDER BY ds) AS step
    FROM ae_folds WHERE split = 'test'
),
fc AS (
    SELECT id, fold_id, forecast_step, yhat
    FROM (
        SELECT id, fold_id,
               unnest(
                   _ts_forecast_scalar(
                       LIST(ds ORDER BY ds), LIST(y::DOUBLE ORDER BY ds),
                       (SELECT max(step) FROM test t WHERE t.id = d.id AND t.fold_id = d.fold_id)::INTEGER,
                       '1d', 'AutoEnsemble', MAP{}::MAP(VARCHAR, VARCHAR)
                   ), recursive := true
               )
        FROM train d GROUP BY id, fold_id
    )
)
SELECT te.id, te.fold_id, te.ds, te.actual AS y, fc.yhat
FROM test te JOIN fc ON te.id=fc.id AND te.fold_id=fc.fold_id AND te.step=fc.forecast_step;

-- Step 3: Calibrate per series (90% coverage, alpha=0.1)
CREATE OR REPLACE TABLE ae_calib AS
SELECT * FROM ts_conformal_calibrate('ae_bt', id, y, yhat, params := {'alpha': 0.1});

-- Step 4: Final AutoEnsemble forecast
CREATE OR REPLACE TABLE ae_fcst AS
SELECT * FROM ts_forecast_by('ae_series', id, ds, y, 'AutoEnsemble', 5, '1d');

-- Step 5: Apply each series' own conformal interval
SELECT * FROM ts_conformal_apply_by('ae_fcst', id, ds, yhat, 'ae_calib')
ORDER BY group_col, date_col;
-- Verification: lower <= point <= upper for all steps (bad_rows must be 0)
```

### Explicit-member conformal path (verified)

`ts_forecast_ensemble_by` is a ScalarFunction and cannot flow through `ts_cv_forecast_by`.
The workaround calls `_ts_forecast_ensemble_native` on each fold's train partition:

```sql
-- Same ae_series from above; reuse ae_folds
CREATE OR REPLACE TABLE exp_bt AS
WITH train AS (
    SELECT id, fold_id, ds, y FROM ae_folds WHERE split = 'train'
),
test AS (
    SELECT id, fold_id, ds, y AS actual,
           ROW_NUMBER() OVER (PARTITION BY id, fold_id ORDER BY ds) AS step
    FROM ae_folds WHERE split = 'test'
),
fc AS (
    SELECT id, fold_id, forecast_step, yhat
    FROM (
        SELECT id, fold_id,
               unnest(
                   _ts_forecast_ensemble_native(
                       LIST(ds ORDER BY ds), LIST(y::DOUBLE ORDER BY ds),
                       ['AutoARIMA', 'AutoETS', 'Theta'],
                       (SELECT max(step) FROM test t WHERE t.id = d.id AND t.fold_id = d.fold_id)::INTEGER,
                       '1d',
                       '',   -- combination_method: '' = mean
                       0     -- seasonal_period
                   ), recursive := true
               )
        FROM train d GROUP BY id, fold_id
    )
)
SELECT te.id, te.fold_id, te.ds, te.actual AS y, fc.yhat
FROM test te JOIN fc ON te.id=fc.id AND te.fold_id=fc.fold_id AND te.step=fc.forecast_step;

CREATE OR REPLACE TABLE exp_calib AS
SELECT * FROM ts_conformal_calibrate('exp_bt', id, y, yhat, params := {'alpha': 0.1});

CREATE OR REPLACE TABLE exp_fcst AS
SELECT * FROM ts_forecast_ensemble_by(
    'ae_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := ''
);

SELECT * FROM ts_conformal_apply_by('exp_fcst', id, ds, yhat, 'exp_calib')
ORDER BY group_col, date_col;
-- Verification: lower <= point <= upper for all steps (bad_rows must be 0)
```

See [ensemble_intervals.sql](../../examples/forecasting/ensemble_intervals.sql) for the full
runnable EPI-01 DoD example with both paths verified end-to-end.

---

*See also: [Cross-Validation](08-cross-validation.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
