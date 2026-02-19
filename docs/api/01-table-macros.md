# Table Macros

> Table-level API for operating directly on tables with SQL

## Overview

Table macros are high-level functions that operate directly on tables. Column names are passed as identifiers (unquoted), making them intuitive to use.

**Use this document to:**
- Learn how table macros work with DuckDB's `query_table()` function
- Understand parameter patterns (table names as strings, columns as identifiers)
- Choose between STRUCT and MAP syntax for parameters
- Find the right table macro for your use case across all categories

```sql
-- Example: Generate forecasts for all product series
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 30, '1d');
```

## How Table Macros Work

Table macros use DuckDB's `query_table()` function to dynamically reference tables. The first parameter is always the table name as a string, followed by column identifiers.

### Parameter Patterns

| Pattern | Description | Example |
|---------|-------------|---------|
| `'table_name'` | Table name as string | `'sales'` |
| `column_name` | Column as unquoted identifier | `product_id` |
| `MAP{}` or `{}` | Parameters as MAP or STRUCT | `{'method': 'Naive'}` |

### STRUCT vs MAP Parameters

As of v0.4.0, all table macros support STRUCT syntax for parameters with mixed types:

```sql
-- STRUCT allows mixed types (recommended)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    {'method': 'Naive', 'gap': 2, 'clip_horizon': true});

-- MAP requires homogeneous string values (legacy)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    MAP{'method': 'Naive', 'gap': '2', 'clip_horizon': 'true'});
```

## Available Table Macros

| Category | Functions |
|----------|-----------|
| [Hierarchical](02-hierarchical.md) | `ts_combine_keys`, `ts_aggregate_hierarchy` |
| [Statistics](03-statistics.md) | `ts_stats`, `ts_quality_report`, `ts_data_quality` |
| [Data Preparation](04-data-preparation.md) | `ts_drop_*`, `ts_fill_*`, `ts_diff` |
| [Period Detection](05-period-detection.md) | `ts_detect_periods_by`, `ts_detect_peaks_by`, `ts_analyze_peak_timing_by` |
| [Decomposition](05a-decomposition.md) | `ts_mstl_decomposition_by`, `ts_classify_seasonality_by` |
| [Changepoint Detection](06-changepoint-detection.md) | `ts_detect_changepoints`, `ts_detect_changepoints_by` |
| [Forecasting](07-forecasting.md) | `ts_forecast`, `ts_forecast_by`, `ts_forecast_exog` |
| [Cross-Validation](08-cross-validation.md) | `ts_backtest_auto_by`, `ts_cv_split_by`, `ts_cv_forecast_by` |
| [Evaluation Metrics](09-evaluation-metrics.md) | `ts_mae_by`, `ts_rmse_by`, `ts_coverage_by`, ... |
| [Feature Extraction](20-feature-extraction.md) | `ts_features_by`, `ts_features_table` |
| [Conformal Prediction](11-conformal-prediction.md) | `ts_conformal`, `ts_conformal_calibrate` |

## Function Naming Conventions

All functions are available with two naming patterns:
- `ts_*` - Short form (e.g., `ts_stats`)
- `anofox_fcst_ts_*` - Prefixed form (e.g., `anofox_fcst_ts_stats`)

Both forms are identical in functionality.

---

*See also: [API Reference Index](../API_REFERENCE.md)*
