# Anofox Forecast Extension - API Reference

**Version:** 0.4.0
**DuckDB Version:** >= v1.4.3
**Forecasting Engine:** anofox-fcst-core (Rust)

---

## Overview

The Anofox Forecast extension brings comprehensive time series analysis and forecasting capabilities directly into DuckDB. It enables analysts and data scientists to perform sophisticated time series operations using familiar SQL syntax, without needing external tools or data movement.

**Key Benefits:**
- **SQL-native**: All operations are expressed as SQL functions and macros
- **High Performance**: Core algorithms implemented in Rust for speed and safety
- **Comprehensive**: 32 forecasting models, 117 features, seasonality detection, changepoint detection
- **Flexible API**: Three API styles to fit different workflows

All computations are performed by the **anofox-fcst-core** library, implemented in Rust.

### Quick Start

```sql
-- Load the extension
LOAD anofox_forecast;

-- Generate forecasts for multiple products
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 30, MAP{});

-- Analyze seasonality
SELECT ts_detect_periods(LIST(quantity ORDER BY date)) FROM sales GROUP BY product_id;

-- Compute time series statistics
SELECT * FROM ts_stats('sales', product_id, date, quantity);
```

### API Variants

The extension provides **three API styles** to accommodate different use cases:

#### 1. Scalar Functions (Array-Based)
Low-level functions that operate on arrays. Composable with `GROUP BY` and `LIST()`.

```sql
SELECT product_id, ts_stats(LIST(value ORDER BY date)) AS stats
FROM sales GROUP BY product_id;
```

#### 2. Table Macros (Table-Based)
High-level macros that operate directly on tables. Column names are passed as identifiers (unquoted).

```sql
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'AutoETS', 12, MAP{});
```

#### 3. Aggregate Functions
Aggregate functions for use with custom `GROUP BY` patterns.

```sql
SELECT product_id, ts_forecast_agg(ts, value, 'ETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;
```

### Parameter Syntax (v0.4.0+)

Table macros support both MAP and STRUCT syntax for parameters:

```sql
-- STRUCT allows mixed types (recommended)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    {'method': 'Naive', 'gap': 2, 'clip_horizon': true});

-- MAP requires homogeneous string values (legacy)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    MAP{'method': 'Naive', 'gap': '2', 'clip_horizon': 'true'});
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
| **Statistics** | Time series statistics and data quality | [02-statistics.md](api/02-statistics.md) |
| **Data Preparation** | Filtering, cleaning, imputation | [03-data-preparation.md](api/03-data-preparation.md) |
| **Seasonality** | Period detection and decomposition | [04-seasonality.md](api/04-seasonality.md) |
| **Forecasting** | 32 forecasting models | [05-forecasting.md](api/05-forecasting.md) |
| **Cross-Validation** | Backtesting and CV functions | [06-cross-validation.md](api/06-cross-validation.md) |
| **Evaluation Metrics** | Forecast accuracy metrics | [07-evaluation-metrics.md](api/07-evaluation-metrics.md) |
| **Feature Extraction** | 117 tsfresh-compatible features | [08-feature-extraction.md](api/08-feature-extraction.md) |
| **Changepoint Detection** | Structural break detection | [09-changepoint-detection.md](api/09-changepoint-detection.md) |
| **Hierarchical** | Multi-key hierarchy functions | [10-hierarchical.md](api/10-hierarchical.md) |
| **Conformal Prediction** | Distribution-free prediction intervals | [11-conformal-prediction.md](api/11-conformal-prediction.md) |

---

## Quick Reference

### Most Common Functions

| Function | Purpose | Example |
|----------|---------|---------|
| `ts_forecast_by` | Forecast multiple series | `ts_forecast_by('tbl', id, date, val, 'AutoETS', 12, MAP{})` |
| `ts_backtest_auto` | One-liner backtesting | `ts_backtest_auto('tbl', id, date, val, 7, 3, '1d', MAP{})` |
| `ts_stats` | Compute 34 statistics | `ts_stats(LIST(val ORDER BY date))` |
| `ts_detect_periods` | Detect seasonality | `ts_detect_periods(LIST(val ORDER BY date))` |
| `ts_detect_periods_by` | Detect seasonality (multi-series) | `ts_detect_periods_by('tbl', id, date, val)` |
| `ts_features` | Extract 117 features | `ts_features(date, value)` |

### Forecasting Models (32 Models)

| Category | Models |
|----------|--------|
| **Automatic** | `AutoETS`, `AutoARIMA`, `AutoTheta`, `AutoMFLES`, `AutoMSTL`, `AutoTBATS` |
| **Basic** | `Naive`, `SMA`, `SeasonalNaive`, `SES`, `SESOptimized`, `RandomWalkDrift` |
| **Exponential Smoothing** | `Holt`, `HoltWinters`, `SeasonalES`, `SeasonalESOptimized` |
| **Theta** | `Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta` |
| **State Space** | `ETS`, `ARIMA` |
| **Multi-Seasonality** | `MFLES`, `MSTL`, `TBATS` |
| **Intermittent Demand** | `CrostonClassic`, `CrostonOptimized`, `CrostonSBA`, `ADIDA`, `IMAPA`, `TSB` |

### Evaluation Metrics (12 Metrics)

Available as both scalar functions and `_by` table macros:

| Metric | Scalar Function | Table Macro |
|--------|-----------------|-------------|
| MAE | `ts_mae(actual, pred)` | `ts_mae_by(source, group, date, actual, forecast)` |
| MSE | `ts_mse(actual, pred)` | `ts_mse_by(...)` |
| RMSE | `ts_rmse(actual, pred)` | `ts_rmse_by(...)` |
| MAPE | `ts_mape(actual, pred)` | `ts_mape_by(...)` |
| sMAPE | `ts_smape(actual, pred)` | `ts_smape_by(...)` |
| R² | `ts_r2(actual, pred)` | `ts_r2_by(...)` |
| Bias | `ts_bias(actual, pred)` | `ts_bias_by(...)` |
| MASE | `ts_mase(actual, pred, baseline)` | `ts_mase_by(..., baseline)` |
| rMAE | `ts_rmae(actual, pred1, pred2)` | `ts_rmae_by(..., pred1, pred2)` |
| Coverage | `ts_coverage(actual, lower, upper)` | `ts_coverage_by(..., lower, upper)` |
| Quantile Loss | `ts_quantile_loss(actual, pred, q)` | `ts_quantile_loss_by(..., quantile)` |
| MQLoss | `ts_mqloss(actual, quantiles, levels)` | — |

---

## Notes

### Array-Based Design

All scalar functions operate on `DOUBLE[]` arrays. To convert table data to arrays:

```sql
SELECT product_id, ts_stats(LIST(value ORDER BY date)) AS stats
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
