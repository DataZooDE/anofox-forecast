# Conformal Prediction

> Distribution-free prediction intervals

## Overview

Conformal prediction provides **distribution-free prediction intervals**. Coverage is
guaranteed only under the exchangeability assumption (calibration and test residuals drawn
from the same distribution, independently) — see [Coverage caveats](#coverage-caveats) below
for where this implementation's ordering and pooling choices currently fall short of that
guarantee in practice. Unlike parametric methods, conformal prediction makes minimal
distributional assumptions and, *when exchangeability holds and the implementation delivers
it*, provides valid coverage even for finite samples.

**Use this document to:**
- Create prediction intervals targeting a nominal coverage level (e.g., 90%, 95%) — see
  caveats on when the delivered coverage may diverge from nominal
- Calibrate conformity scores from backtest residuals
- Apply calibrated intervals to new forecasts
- Choose between one-step, modular, or array-based approaches
- Build production pipelines that separate calibration from application

### How It Works

Conformal prediction works in two phases:
1. **Calibration**: Compute a conformity score from calibration residuals
2. **Prediction**: Apply the conformity score to new forecasts

The resulting intervals target coverage of the true value with probability `1 - α`, where `α`
is the miscoverage rate — under exchangeability and a correctly horizon-ordered calibration
set (see [Coverage caveats](#coverage-caveats)).

---

## Coverage caveats

The theoretical coverage guarantee of split conformal prediction holds under exchangeability
of calibration and test residuals. This implementation has known gaps that can make the
*delivered* coverage diverge from the nominal `1 - α` target:

- **`ts_conformal_calibrate`'s `coverage` column is nominal, not empirical.** It literally
  returns `1 - alpha` — it is not computed by checking how many calibration residuals the
  interval actually covers. Measure empirical coverage yourself with `ts_conformal_coverage`
  / `ts_conformal_evaluate` on held-out data if you need a real coverage estimate.
- **`ts_conformal_by` orders forecasts by value, not by horizon.** The `LIST(point_forecast_col
  ORDER BY point_forecast_col)` construction used internally sorts the per-group forecast
  array by the forecast *value*, not by date/forecast-step order. If point forecasts are not
  already monotonic in time, the resulting lower/upper bounds are misaligned with their
  intended horizon step.
- **Asymmetric is not guaranteed tighter than symmetric.** Treat `'method': 'asymmetric'` as a
  different quantile estimator, not a strict improvement; measure interval width on your own
  data.
- **`jackknife+` currently behaves like `split`.** The applied conformity scores
  (`scores_lower`/`scores_upper`) are identical between the two strategies today, so selecting
  `'jackknife+'` does not currently buy you the leave-one-out robustness the name implies.
- **Unrecognized `strategy` strings silently fall back to `'split'`** rather than raising an
  error — a typo in the strategy name will not be caught.

(fix planned for v0.10.0, Phase 16)

---

## Choose Your Approach

The API offers three levels of abstraction. Choose based on your needs:

| Approach | Functions | Use When |
|----------|-----------|----------|
| **One-Step** | `ts_conformal_by` | You have backtest results and want intervals immediately |
| **Modular** | `ts_conformal_calibrate` + `ts_conformal_apply_by` | You need to reuse calibration across multiple forecasts |
| **Array-Based** | `ts_conformal_predict`, `ts_conformal_quantile`, etc. | You're building custom pipelines or working with array data |

### One-Step Approach

Best for most users. Calibrates and applies intervals in a single call.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    MAP{'alpha': '0.1'}  -- 90% coverage
);
```

### Modular Approach

Use when you need to:
- Apply the same calibration to multiple forecast tables
- Store calibration scores for production use
- Separate calibration from application for debugging

```sql
-- Step 1: Calibrate once
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', actual, forecast, MAP{'alpha': '0.1'}
);

-- Step 2: Apply to any forecast table
SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

### Array-Based Functions

Use when you need:
- Fine-grained control over the conformal prediction process
- Integration with custom forecasting pipelines
- Array inputs/outputs for programmatic use

```sql
-- Direct array-based prediction
SELECT (ts_conformal_predict(
    residuals_array,
    forecasts_array,
    0.1
)).*;
```

---

## Common Scenarios

### Scenario 1: Quick Uncertainty Quantification

**Goal:** You have backtest results and need prediction intervals quickly.

**Solution:** Use `ts_conformal_by` for a one-step solution.

```sql
-- Already have backtest results with actual vs forecast
SELECT * FROM ts_conformal_by(
    'my_backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    MAP{'alpha': '0.1'}
);
```

### Scenario 2: Production Forecasting System

**Goal:** Calibrate once, apply to daily/weekly forecast batches.

**Solution:** Use modular approach—store calibration, apply repeatedly.

```sql
-- Run once: calibrate from historical backtests
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'historical_backtest', actual, forecast, MAP{'alpha': '0.1'}
);

-- Run daily: apply to new forecasts
INSERT INTO forecast_with_intervals
SELECT * FROM ts_conformal_apply_by(
    'todays_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

### Scenario 3: Skewed Forecast Errors

**Goal:** Your forecasts consistently under-predict (e.g., demand forecasting).

**Solution:** Use asymmetric intervals, which compute separate upper and lower quantiles
instead of one symmetric quantile. Do not assume this makes the interval tighter — internal
measurement found the asymmetric path can be substantially *wider* than symmetric split on
some residual distributions. Measure width empirically for your own data before relying on
"asymmetric is tighter." (fix planned for v0.10.0, Phase 16)

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    MAP{'alpha': '0.1', 'method': 'asymmetric'}
);
```

### Scenario 4: Model Comparison

**Goal:** Compare which model produces sharper intervals while maintaining coverage.

**Solution:** Use evaluation functions to compare interval quality.

```sql
-- Generate intervals for both models
CREATE TABLE model_a_intervals AS ...;
CREATE TABLE model_b_intervals AS ...;

-- Compare interval widths
SELECT 'Model A' AS model, * FROM ts_interval_width_by(
    'model_a_intervals', product_id, lower, upper
)
UNION ALL
SELECT 'Model B' AS model, * FROM ts_interval_width_by(
    'model_b_intervals', product_id, lower, upper
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
    GROUP BY product_id
)
SELECT
    product_id,
    (ts_conformal_predict(residuals, forecasts, 0.1)).*
FROM series_data;
```

---

## Complete Workflow Example

A full workflow from raw data to conformal intervals:

```sql
-- Step 0: Detect seasonality (e.g., weekly = 7)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- Step 1: Generate backtest results with AutoETS (two-step cross-validation)
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', product_id, date, value, 5, 7, MAP{});
CREATE TABLE backtest AS
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, value,
    'AutoETS', MAP{'seasonal_period': '7'}
);

-- Step 2: Calibrate conformity score (actual is 'y', forecast is 'yhat')
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', y, yhat, MAP{'alpha': '0.1'}
);

-- Step 3: Generate future forecasts
CREATE TABLE future AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value, 'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'}
);

-- Step 4: Apply conformal intervals
SELECT * FROM ts_conformal_apply_by(
    'future',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

---

## Table Macros

> **When to use:** Table macros operate on tables directly and handle grouping automatically. Use these for standard forecasting workflows with tabular data.

### ts_conformal_by

High-level macro for conformal prediction on grouped backtest results.

**Signature:**
```sql
ts_conformal_by(backtest_results, group_col, actual_col, forecast_col, point_forecast_col, params)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `group_col` | COLUMN | Yes | — | Series identifier column |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Forecast values column |
| `point_forecast_col` | COLUMN | Yes | — | Point forecast column |
| `params` | STRUCT | Yes | — | Configuration parameters |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage, 0.05 = 95%) |
| `method` | VARCHAR | `'split'` | `'split'` (symmetric) or `'asymmetric'` |

**Examples:**
```sql
-- Symmetric intervals (default)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    yhat,
    MAP{'alpha': '0.1', 'method': 'split'}
);

-- Asymmetric intervals (for skewed residuals)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    yhat,
    MAP{'alpha': '0.1', 'method': 'asymmetric'}
);
```

**When to use asymmetric:** Use `'asymmetric'` when residuals are not symmetric (e.g., demand forecasts often under-predict more than over-predict). This creates separate upper and lower quantiles — it does not guarantee a *tighter* interval than symmetric split; measure both on your data. (fix planned for v0.10.0, Phase 16)

**Per-horizon ordering caveat:** `ts_conformal_by` currently builds the per-group forecast
array with `LIST(point_forecast_col ORDER BY point_forecast_col)` — ordering by forecast
*value*, not by the original horizon/date order. This silently scrambles which calibration
residual lines up with which forecast step whenever point forecasts are not already
monotonic. See [Coverage caveats](#coverage-caveats). (fix planned for v0.10.0, Phase 16)

---

### ts_conformal_calibrate

Calibrates a conformity score from backtest residuals.

**Signature:**
```sql
ts_conformal_calibrate(backtest_results, actual_col, forecast_col, params)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Forecast values column |
| `params` | STRUCT | Yes | — | Configuration parameters |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `conformity_score` | DOUBLE | Calibrated score for interval construction |
| `coverage` | DOUBLE | **Nominal** coverage (`1 - alpha`) — NOT empirically measured from the calibration residuals, despite the name |
| `n_residuals` | INTEGER | Number of residuals used |

**Example:**
```sql
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',
    actual,
    forecast,
    MAP{'alpha': '0.05'}
);
```

---

### ts_conformal_apply_by

Applies a pre-computed conformity score to forecast results.

**Signature:**
```sql
ts_conformal_apply_by(forecast_results, group_col, forecast_col, conformity_score)
```

**Parameters:**
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `forecast_results` | VARCHAR | Yes | Table with forecast results |
| `group_col` | COLUMN | Yes | Series identifier column |
| `forecast_col` | COLUMN | Yes | Point forecast column |
| `conformity_score` | DOUBLE | Yes | Score from ts_conformal_calibrate |

**Example:**
```sql
WITH score AS (
    SELECT conformity_score FROM ts_conformal_calibrate(
        'backtest', actual, forecast, MAP{'alpha': '0.1'}
    )
)
SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM score)
);
```

---

### ts_interval_width_by

Computes mean interval width for grouped forecast results.

**Signature:**
```sql
ts_interval_width_by(results, group_col, lower_col, upper_col)
```

**Returns:** `group_col`, `mean_width`, `n_intervals`

**Example:**
```sql
SELECT * FROM ts_interval_width_by(
    'forecast_results',
    product_id,
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
| **Multi-level calibration profiles** | `ts_conformal_learn` |

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

---

### Calibration Functions

These functions compute conformity scores from residuals.

#### ts_conformal_quantile

Computes the empirical quantile of absolute residuals for split conformal prediction.

**Signature:**
```sql
ts_conformal_quantile(residuals DOUBLE[], alpha DOUBLE) → DOUBLE
```

**Parameters:**
- `residuals`: Residuals (actual - predicted) from calibration set
- `alpha`: Miscoverage rate (0 < alpha < 1). Use 0.1 for 90% coverage.

**Example:**
```sql
SELECT ts_conformal_quantile(
    [1.0, -0.5, 2.0, -1.5, 0.8],  -- residuals from backtest
    0.1                           -- 90% coverage
) AS conformity_score;
```

---

### Prediction Functions

These functions apply conformity scores to generate prediction intervals.

#### ts_conformal_intervals

Applies a pre-computed conformity score to create symmetric prediction intervals.

**Signature:**
```sql
ts_conformal_intervals(forecasts DOUBLE[], conformity_score DOUBLE)
→ STRUCT(lower DOUBLE[], upper DOUBLE[])
```

**Example:**
```sql
SELECT
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).lower AS lower,
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).upper AS upper;
-- Returns: lower=[95,105,115], upper=[105,115,125]
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
    method VARCHAR
)
```

**Example:**
```sql
WITH backtest_residuals AS (
    SELECT [1.2, -0.8, 1.5, -1.0, 0.5]::DOUBLE[] AS residuals
),
future_forecasts AS (
    SELECT [100.0, 102.0, 104.0]::DOUBLE[] AS forecasts
)
SELECT
    (ts_conformal_predict(residuals, forecasts, 0.1)).*
FROM backtest_residuals, future_forecasts;
```

---

#### ts_conformal_predict_asymmetric

Asymmetric conformal prediction with separate upper and lower quantiles.

**Signature:**
```sql
ts_conformal_predict_asymmetric(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE)
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE,
         conformity_score DOUBLE, method VARCHAR)
```

**Use Case:** When residuals are not symmetric (e.g., skewed demand forecasts).

**Example:**
```sql
SELECT
    (ts_conformal_predict_asymmetric(
        [-0.5, -0.3, 0.2, 1.0, 2.5],  -- positively skewed residuals
        [100.0, 110.0],               -- forecasts
        0.1                           -- 90% coverage
    )).*;
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
SELECT (ts_conformal_evaluate(
    [100.0, 110.0, 120.0],
    [95.0, 105.0, 115.0],
    [105.0, 115.0, 125.0],
    0.1
)).*;
```

---

### Advanced Calibration

#### ts_conformal_learn

Learn a calibration profile from residuals. Use this when you need multiple coverage levels or advanced calibration strategies.

**Signature:**
```sql
ts_conformal_learn(residuals DOUBLE[], alphas DOUBLE[], method VARCHAR, strategy VARCHAR)
→ STRUCT(
    method VARCHAR,
    strategy VARCHAR,
    alphas DOUBLE[],
    state_vector DOUBLE[],
    scores_lower DOUBLE[],
    scores_upper DOUBLE[]
)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `residuals` | DOUBLE[] | Calibration residuals (actual - predicted) |
| `alphas` | DOUBLE[] | Array of miscoverage rates to calibrate |
| `method` | VARCHAR | `'symmetric'` or `'asymmetric'` |
| `strategy` | VARCHAR | `'split'`, `'crossval'`, or `'jackknife+'` (see Strategies below) |

**Methods:**
| Method | Description |
|--------|-------------|
| `'symmetric'` | Use absolute residuals for symmetric intervals |
| `'asymmetric'` | Separate upper/lower quantiles for asymmetric intervals |

**Strategies:**
| Strategy | Description |
|----------|-------------|
| `'split'` | Simple empirical quantile (default) |
| `'crossval'` (alias `'cv'`) | Cross-validation based calibration |
| `'jackknife+'` (alias `'jackknife_plus'`) | Leave-one-out jackknife+; currently produces the **same** applied interval score as `'split'` — the internal state representation differs but `scores_lower`/`scores_upper` are identical today |

Any other string (e.g. `'naive'`, `'adaptive'`) is silently treated as `'split'` rather than
raising an error. (fix planned for v0.10.0, Phase 16)

**Example:**
```sql
-- Learn calibration profile for multiple coverage levels
SELECT ts_conformal_learn(
    [1.2, -0.8, 1.5, -1.0, 0.5, -0.3, 0.9, -1.2]::DOUBLE[],  -- residuals
    [0.1, 0.05, 0.2]::DOUBLE[],                               -- 90%, 95%, 80% coverage
    'symmetric',
    'split'
) AS profile;
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

3. **`ts_conformal_calibrate` returns a global quantile** across all series and folds.
   For per-series calibration, use `ts_conformal_by` (one-step) with a `group_col`.

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

-- Step 3: Calibrate (90% coverage, alpha=0.1)
CREATE OR REPLACE TABLE ae_calib AS
SELECT * FROM ts_conformal_calibrate('ae_bt', y, yhat, MAP{'alpha': '0.1'});

-- Step 4: Final AutoEnsemble forecast
CREATE OR REPLACE TABLE ae_fcst AS
SELECT * FROM ts_forecast_by('ae_series', id, ds, y, 'AutoEnsemble', 5, '1d');

-- Step 5: Apply conformal intervals
SELECT f.id, f.ds, f.forecast_step, f.yhat,
       f.yhat - c.conformity_score AS yhat_lower,
       f.yhat + c.conformity_score AS yhat_upper
FROM ae_fcst f CROSS JOIN ae_calib c
ORDER BY id, forecast_step;
-- Verification: lower <= yhat <= upper for all steps (bad_rows must be 0)
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
SELECT * FROM ts_conformal_calibrate('exp_bt', y, yhat, MAP{'alpha': '0.1'});

CREATE OR REPLACE TABLE exp_fcst AS
SELECT * FROM ts_forecast_ensemble_by(
    'ae_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := ''
);

SELECT f.id, f.ds, f.forecast_step, f.yhat,
       f.yhat - c.conformity_score AS yhat_lower,
       f.yhat + c.conformity_score AS yhat_upper
FROM exp_fcst f CROSS JOIN exp_calib c
ORDER BY id, forecast_step;
-- Verification: lower <= yhat <= upper for all steps (bad_rows must be 0)
```

See [ensemble_intervals.sql](../../examples/forecasting/ensemble_intervals.sql) for the full
runnable EPI-01 DoD example with both paths verified end-to-end.

---

*See also: [Cross-Validation](08-cross-validation.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
