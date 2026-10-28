# Anofox Forecast Extension - API Reference

> Complete function and macro reference for time series analysis and forecasting in DuckDB

**Version:** 0.4.0
**DuckDB Version:** v2.0.0 (stable), v1.5.6
**Forecasting Engine:** anofox-fcst-core (Rust)

---

## Overview

The Anofox Forecast extension brings comprehensive time series analysis and forecasting capabilities directly into DuckDB. It enables analysts and data scientists to perform sophisticated time series operations using familiar SQL syntax, without needing external tools or data movement.

**Key Benefits:**
- **SQL-native**: All operations are expressed as SQL functions and macros
- **High Performance**: Core algorithms implemented in Rust for speed and safety
- **Comprehensive**: 36 forecasting models, 117 features, seasonality detection, changepoint detection
- **Flexible API**: Three API styles to fit different workflows

All computations are performed by the **anofox-fcst-core** library, implemented in Rust.

### Quick Start

```sql
-- Load the extension
LOAD anofox_forecast;
```

```sql
-- Generate forecasts for multiple products
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 30, '1d');

-- Analyze seasonality (internal scalar -- ts_detect_periods is a table function,
-- not callable as a scalar; _ts_detect_periods is the scalar form)
SELECT _ts_detect_periods(LIST(quantity ORDER BY date)) FROM sales GROUP BY product_id;

-- Compute time series statistics (frequency is a required 5th positional argument)
SELECT * FROM ts_stats('sales', product_id, date, quantity, '1d');
```

### API Variants

The extension provides **three API styles** to accommodate different use cases:

#### 1. Scalar Functions (Array-Based)
Low-level functions that operate on arrays. Composable with `GROUP BY` and `LIST()`.

```sql
-- ts_stats is a TABLE function, not callable as a scalar -- ts_stats_agg is the
-- scalar-aggregate form, taking (date_col, value_col) directly, no LIST() wrapping
SELECT product_id, ts_stats_agg(date, value) AS stats
FROM sales GROUP BY product_id;
```

#### 2. Table Macros (Table-Based)
High-level macros that operate directly on tables. Column names are passed as identifiers (unquoted).

```sql
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'AutoETS', 12, '1d');
```

#### 3. Aggregate Functions
Aggregate functions for use with custom `GROUP BY` patterns.

```sql
SELECT product_id, ts_forecast_agg(date, value, 'ETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;
```

### Parameter Syntax (v0.4.0+)

Table macros support both MAP and STRUCT syntax for parameters:

```sql
-- STRUCT allows mixed types (recommended)
SELECT * FROM ts_cv_folds_by('sales', product_id, date, value, 3, 7,
    {'gap': 2, 'clip_horizon': true});

-- MAP requires homogeneous string values (legacy)
SELECT * FROM ts_cv_folds_by('sales', product_id, date, value, 3, 7,
    MAP{'gap': '2', 'clip_horizon': 'true'});
```

### Function Naming Conventions

All functions are available with two naming patterns:
- `ts_*` - Short form (e.g., `ts_stats`, `ts_mae`)
- `anofox_fcst_ts_*` - Prefixed form (e.g., `anofox_fcst_ts_stats`)

Both forms are identical in functionality.

---

## API Reference by Category

### Core Documentation

| Category | Description | Documentation |
|----------|-------------|---------------|
| **API Design Guide** | Naming conventions and patterns | [00-api-design.md](api/00-api-design.md) |
| **Table Macros** | High-level API overview | [01-table-macros.md](api/01-table-macros.md) |
| **Hierarchical** | Multi-key hierarchy functions | [02-hierarchical.md](api/02-hierarchical.md) |
| **Statistics** | Time series statistics and data quality | [03-statistics.md](api/03-statistics.md) |
| **Data Preparation** | Filtering, cleaning, imputation | [04-data-preparation.md](api/04-data-preparation.md) |
| **Period Detection** | Period detection and seasonality | [05-period-detection.md](api/05-period-detection.md) |
| **Decomposition** | Seasonal decomposition and classification | [05a-decomposition.md](api/05a-decomposition.md) |
| **Peak Detection** | Peak detection and timing analysis | [05b-peak-detection.md](api/05b-peak-detection.md) |
| **Changepoint Detection** | Structural break detection | [06-changepoint-detection.md](api/06-changepoint-detection.md) |
| **Forecasting** | 36 forecasting models | [07-forecasting.md](api/07-forecasting.md) |
| **Cross-Validation** | Backtesting and CV functions | [08-cross-validation.md](api/08-cross-validation.md) |
| **Evaluation Metrics** | Forecast accuracy metrics | [09-evaluation-metrics.md](api/09-evaluation-metrics.md) |
| **Feature Extraction** | 117 tsfresh-inspired features | [20-feature-extraction.md](api/20-feature-extraction.md) |
| **Conformal Prediction** | Distribution-free prediction intervals | [11-conformal-prediction.md](api/11-conformal-prediction.md) |

---

## Quick Reference

### Most Common Functions

| Function | Purpose | Example |
|----------|---------|---------|
| `ts_forecast_by` | Forecast multiple series | `ts_forecast_by('tbl', id, date, val, 'AutoETS', 12, '1d')` |
| `ts_cv_folds_by` | Cross-validation folds (backtesting) | `ts_cv_folds_by('tbl', id, date, val, 3, 7, MAP{})` |
| `ts_stats` | Compute 34 statistics | `ts_stats(LIST(val ORDER BY date))` |
| `ts_detect_periods` | Detect seasonality | `ts_detect_periods(LIST(val ORDER BY date))` |
| `ts_detect_periods_by` | Detect seasonality (multi-series) | `ts_detect_periods_by('tbl', id, date, val)` |
| `ts_features` | Extract 117 features | `ts_features(date, value)` |

### Forecasting Models (36 Models)

| Category | Models | Reference |
|----------|--------|-----------|
| **Baseline** | `Naive`, `SMA`, `SeasonalNaive`, `RandomWalkDrift`, `SeasonalWindowAverage` | [baseline/](reference/models/baseline/) |
| **Exponential Smoothing** | `SES`, `SESOptimized`, `Holt`, `HoltWinters`, `SeasonalES`, `SeasonalESOptimized` | [exponential-smoothing/](reference/models/exponential-smoothing/) |
| **State Space** | `ETS`, `ARIMA`, `AutoETS`, `AutoARIMA`, `Kalman` | [state-space/](reference/models/state-space/) |
| **Classical** | `GARCH` (conditional volatility) | [classical/garch.md](reference/models/classical/garch.md) |
| **Theta** | `Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta` | [theta/](reference/models/theta/) |
| **Multi-Seasonal** | `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS` | [multi-seasonal/](reference/models/multi-seasonal/) |
| **Intermittent Demand** | `CrostonClassic`, `CrostonOptimized`, `CrostonSBA`, `ADIDA`, `IMAPA`, `TSB` | [intermittent/](reference/models/intermittent/) |
| **Distributional** | `Laplace` (variants: `auto`, `auto_aid`, `skaters`) | [distributional/laplace.md](reference/models/distributional/laplace.md) |
| **Ensemble** | `AutoEnsemble` (auto-fit ARIMA/ETS/Theta, combine top-K) | [ensemble/autoensemble.md](reference/models/ensemble/autoensemble.md) |

### Evaluation Metrics (12 Metrics)

Available as scalar functions (recommended — use with `LIST()` + `GROUP BY`, ~2400x
faster) and as deprecated `_by` table macros. **The `_by` family's `source` MUST be a
subquery, e.g. `(SELECT group_col, date_col, actual, forecast FROM table)` — a bare
table name fails to bind (the native function's first parameter is `TABLE`-typed);
`date_col`/`actual_col`/`forecast_col`/etc. are quoted column-name string literals, not
unquoted identifiers, and there is no separate positional `group` argument — grouping
columns simply pass through whatever the subquery selects.**

| Metric | Scalar Function | Table Macro |
|--------|-----------------|-------------|
| MAE | `ts_mae(actual, pred)` | `ts_mae_by(source, date_col, actual_col, forecast_col)` |
| MSE | `ts_mse(actual, pred)` | `ts_mse_by(source, date_col, actual_col, forecast_col)` |
| RMSE | `ts_rmse(actual, pred)` | `ts_rmse_by(source, date_col, actual_col, forecast_col)` |
| MAPE | `ts_mape(actual, pred)` — NULL if any actual is 0 | `ts_mape_by(source, date_col, actual_col, forecast_col)` |
| sMAPE | `ts_smape(actual, pred)` | `ts_smape_by(source, date_col, actual_col, forecast_col)` |
| R² | `ts_r2(actual, pred)` | `ts_r2_by(source, date_col, actual_col, forecast_col)` |
| Bias | `ts_bias(actual, pred)` | `ts_bias_by(source, date_col, actual_col, forecast_col)` |
| MASE | `ts_mase(actual, pred, insample, seasonal_period)` — Hyndman-Koehler | `ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)` |
| rMAE | `ts_rmae(actual, pred1, pred2)` | `ts_rmae_by(source, date_col, actual_col, pred1_col, pred2_col)` |
| Coverage | `ts_coverage(actual, lower, upper)` | `ts_coverage_by(source, date_col, actual_col, lower_col, upper_col)` |
| Quantile Loss | `ts_quantile_loss(actual, pred, q)` | `ts_quantile_loss_by(source, date_col, actual_col, forecast_col, quantile)` |
| MQLoss | `ts_mqloss(actual, quantiles, levels)` | — |

Example: `SELECT * FROM ts_mae_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')`.

---

## Notes

### Array-Based Design

All scalar functions operate on `DOUBLE[]` arrays. To convert table data to arrays:

```sql
-- ts_stats is a TABLE function, not callable as a scalar -- ts_stats_agg is the
-- scalar-aggregate form, taking (date_col, value_col) directly, no LIST() wrapping
SELECT product_id, ts_stats_agg(date, value) AS stats
FROM sales GROUP BY product_id;
```

**Important:** Always use `ORDER BY` in `LIST()` to ensure correct temporal ordering.

### NULL Handling

- **Statistics functions**: NULLs are typically excluded
- **Imputation functions**: Designed to fill NULLs (`ts_fill_nulls_*`)
- **Forecasting**: Impute NULLs before forecasting

### Minimum Data Requirements

| Function Type | Minimum | Recommended |
|--------------|---------|-------------|
| Basic statistics | n ≥ 2 | n ≥ 10 |
| Seasonality detection | n ≥ 2 × period | n ≥ 4 × period |
| Forecasting (simple) | n ≥ 3 | n ≥ 20 |
| Forecasting (seasonal) | n ≥ 2 × period | n ≥ 3 × period |
| Feature extraction | n ≥ 10 | n ≥ 50 |

### Performance Tips

1. **Use table macros** - optimized for batch processing
2. **Filter early** - apply WHERE clauses before forecast functions
3. **Limit horizon** - forecasts beyond 2-3 seasonal periods have high uncertainty
4. **Batch processing** - process multiple series in one query

---

**Last Updated:** 2026-01-19
**API Version:** 0.4.0
