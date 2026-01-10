# Backtesting Capabilities Summary

## Overview

The anofox-forecast extension provides a comprehensive time series cross-validation (CV) and backtesting framework designed to evaluate forecasting models without data leakage. The framework respects temporal ordering - models are only trained on past data to predict future values.

---

## Core Architecture

### Two Usage Patterns

| Pattern | Function | Use Case |
|---------|----------|----------|
| **One-liner** | `ts_backtest_auto` | Quick evaluation, 80% of use cases |
| **Modular** | `ts_cv_split` + `ts_cv_forecast_by` + JOIN | Custom pipelines, advanced control |

Both patterns produce **identical results** (verified by equivalence tests).

---

## Primary Functions

### 1. ts_backtest_auto (One-liner Backtest)

Complete backtesting in a single function call. Combines fold generation, data splitting, forecasting, and evaluation.

**Signature:**
```sql
ts_backtest_auto(
    source          VARCHAR,    -- Table name
    group_col       COLUMN,     -- Series identifier
    date_col        COLUMN,     -- Date/timestamp column
    target_col      COLUMN,     -- Target value
    horizon         INTEGER,    -- Forecast horizon
    folds           INTEGER,    -- Number of CV folds
    frequency       VARCHAR,    -- Time frequency ('1d', '1h', '1w', '1mo')
    params          MAP,        -- Model and CV parameters
    features        VARCHAR[],  -- Optional: regressor columns (default NULL)
    metric          VARCHAR     -- Optional: fold metric ('rmse', 'mae', 'mape', 'mse')
) → TABLE
```

**Output Columns:**
- `fold_id` - CV fold number
- `group_col` - Series identifier
- `date` - Forecast date
- `forecast` - Point forecast
- `actual` - Actual value
- `error` - forecast - actual
- `abs_error` - |forecast - actual|
- `lower_90`, `upper_90` - 90% prediction intervals
- `model_name` - Model used
- `fold_metric_score` - Calculated metric per fold

### 2. ts_cv_split (Data Splitting)

Splits time series data into train/test sets for multiple CV folds.

**Signature:**
```sql
ts_cv_split(
    source              VARCHAR,
    group_col           COLUMN,
    date_col            COLUMN,
    target_col          COLUMN,
    training_end_times  DATE[],     -- List of cutoff dates
    horizon             INTEGER,
    frequency           VARCHAR,
    params              MAP
) → TABLE
```

### 3. ts_cv_forecast_by (Parallel Fold Execution)

Generates forecasts for all CV folds in parallel using DuckDB's thread pool.

**Signature:**
```sql
ts_cv_forecast_by(
    cv_splits   VARCHAR,    -- Train split table
    group_col   COLUMN,
    date_col    COLUMN,
    target_col  COLUMN,
    method      VARCHAR,    -- Forecast method
    horizon     INTEGER,
    params      MAP,
    frequency   VARCHAR
) → TABLE
```

### 4. ts_backtest_regression (Regression Backtest)

Backtesting for regression models using DuckDB's built-in OLS or external statistics packages.

### 5. ts_prepare_regression_input (Regression Adapter)

Prepares data for regression models by masking target values in test rows (NULL for test, actual for train).

### 6. ts_hydrate_features (Feature Hydration)

Safe join of CV splits with source features, with `_is_test` flag for masking unknown features.

---

## Supported Forecasting Methods

### Univariate Methods (21 total)

| Category | Methods |
|----------|---------|
| **Automatic Selection** | AutoETS, AutoARIMA, auto |
| **ARIMA** | ARIMA |
| **Exponential Smoothing** | ETS, SES, Holt, DampedHolt, HoltWinters |
| **Theta** | Theta, OptimizedTheta, DynamicTheta |
| **Naive/Baseline** | Naive, SeasonalNaive, Drift, RandomWalkDrift, SeasonalWindowAverage |
| **Intermittent Demand** | CrostonClassic, CrostonOptimized, CrostonSBA, TSB, ADIDA, IMAPA |
| **Advanced** | MFLES |

### Regression Methods (via anofox-statistics)

| Method | Aliases |
|--------|---------|
| OLS | `ols`, `linear_regression_ols`, `linear_regression` |
| Ridge | `ridge`, `ridge_regression` |
| Lasso | `lasso`, `lasso_regression` |
| Elastic Net | via `elasticnet_fit_predict_by` |
| WLS | via `wls_fit_predict_by` (requires weight column) |
| RLS | via `rls_fit_predict_by` |
| Poisson | via `poisson_fit_predict_by` |
| ALM | via `alm_fit_predict_by` (24 distributions) |
| BLS | via `bls_fit_predict_by` (bounded least squares) |

---

## Cross-Validation Parameters

### Window Types

| Type | Behavior | Use Case |
|------|----------|----------|
| `expanding` (default) | Training grows with each fold | Maximum data utilization |
| `fixed` | Constant training window size | Concept drift concerns |
| `sliding` | Window slides forward | Recency emphasis |

### Leakage Prevention

| Parameter | Purpose | Use Case |
|-----------|---------|----------|
| `gap` | Periods between train end and test start | Simulates ETL/data latency |
| `embargo` | Periods to exclude after previous fold's test | Prevents label leakage with forward-looking targets |

**Visual Example:**
```
gap=2:      [TRAIN TRAIN]  ##  [TEST TEST]
                           └── 2 periods skipped

embargo=3:  Fold 1: [TRAIN][TEST]
            Fold 2:       xxx[TRAIN][TEST]
                          └── 3 periods excluded from fold 2's training
```

### Fold Spacing

| Parameter | Purpose | Default | Use Case |
|-----------|---------|---------|----------|
| `skip_length` | Periods between fold start times | `horizon` | Custom fold spacing |

**Visual Example:**
```
skip_length=horizon (default):  Folds spaced by horizon
    Fold 1: [TRAIN════════][TEST═══]
    Fold 2:        [TRAIN════════][TEST═══]   ← horizon periods later

skip_length=1 (dense):  Overlapping folds for more test coverage
    Fold 1: [TRAIN════════][TEST═══]
    Fold 2:  [TRAIN════════][TEST═══]         ← 1 period later
    Fold 3:   [TRAIN════════][TEST═══]        ← 1 more period

skip_length=30 (sparse):  Monthly spacing for efficiency
    Fold 1: [TRAIN════════][TEST═══]
    Fold 2:                              [TRAIN════════][TEST═══]  ← 30 periods later
```

---

## Metrics

| Metric | Parameter | Description |
|--------|-----------|-------------|
| RMSE | `'rmse'` | Root Mean Squared Error (default) |
| MAE | `'mae'` | Mean Absolute Error |
| MAPE | `'mape'` | Mean Absolute Percentage Error |
| MSE | `'mse'` | Mean Squared Error |

---

## Supporting Functions

| Function | Purpose |
|----------|---------|
| `ts_cv_generate_folds` | Auto-generate training end times |
| `ts_cv_split_folds` | Preview fold boundaries without data |
| `ts_cv_split_index` | Memory-efficient split (index only) |
| `ts_hydrate_split` | Safe join with single column masking |
| `ts_hydrate_split_full` | Join with all columns (debugging) |
| `ts_hydrate_split_strict` | Fail-safe join (no data columns) |
| `ts_mark_unknown` | Mark rows as known/unknown |
| `ts_fill_unknown` | Fill future feature values |
| `ts_validate_timestamps` | Validate timestamp completeness |

---

## Frequency Support

| Format | Meaning | Example |
|--------|---------|---------|
| `'Nd'` | N days | `'1d'`, `'7d'` |
| `'Nh'` | N hours | `'1h'`, `'6h'` |
| `'Nm'` or `'Nmin'` | N minutes | `'15m'`, `'30min'` |
| `'Nw'` | N weeks | `'1w'` |
| `'Nmo'` | N months | `'1mo'` |
| `'Nq'` | N quarters | `'1q'` |
| `'Ny'` | N years | `'1y'` |

---

## Performance Characteristics

- **Parallel execution**: ts_cv_forecast_by processes all folds simultaneously
- **Vectorized**: Leverages DuckDB's columnar engine
- **Memory-efficient**: ts_cv_split_index for large datasets
- **3-10x faster** than serial fold-by-fold execution

---

## Test Coverage

- `ts_backtest_auto.test` - 48 assertions
- `ts_backtest_regression.test` - 15 assertions
- `ts_backtest_equivalence.test` - 58 assertions (verifies one-liner = detailed approach)
- `ts_cv_split.test` - 100 assertions
- `ts_hydrate_features.test` - 17 assertions
- `ts_prepare_regression_input.test` - 26 assertions

---

## Integration with External Packages

### anofox-statistics Extension

Regression models from anofox-statistics can be used with the backtesting framework:

```sql
-- Install extension
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Use with ts_prepare_regression_input
SELECT * FROM ols_fit_predict_by(
    'regression_input_table',
    fold_id,
    masked_target,  -- NULL for test rows
    [feature1, feature2]
);
```

The pattern: `masked_target = NULL` for test rows enables fit-predict in one pass.
