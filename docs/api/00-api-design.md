# API Design Guide

> Consistent patterns for the Anofox Forecast Extension API

## Overview

The extension provides three API styles for time series operations. This document defines the naming conventions, parameter ordering, and patterns that should be followed for consistency.

## Three-Tier API Pattern

For any time series operation `<operation>`, the API can provide up to three variants:

| API Type | Function Pattern | Use Case |
|----------|------------------|----------|
| **Scalar** | `ts_<operation>(values[], ...)` | Array input, composable with `GROUP BY + LIST()` |
| **Aggregate** | `ts_<operation>_agg(date, value, ...)` | Direct GROUP BY accumulation |
| **Table Macro** | `ts_<operation>_by(source, group, date, value, ...)` | Multi-series table operation |

### When to Use Each

```sql
-- SCALAR: When you already have arrays or want to compose with LIST()
SELECT product_id, ts_stats(LIST(value ORDER BY date)) AS stats
FROM sales GROUP BY product_id;

-- AGGREGATE: When you want direct GROUP BY without LIST()
SELECT product_id, ts_forecast_agg(date, value, 'AutoETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;

-- TABLE MACRO: Cleanest syntax for table-level operations
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'AutoETS', 12, MAP{});
```

---

## Naming Conventions

### Function Names

| Type | Pattern | Examples |
|------|---------|----------|
| Scalar | `ts_<operation>` | `ts_stats`, `ts_mae`, `ts_detect_periods` |
| Aggregate | `ts_<operation>_agg` | `ts_forecast_agg`, `ts_features_agg` |
| Table (single) | `ts_<operation>` | `ts_forecast`, `ts_detect_changepoints` |
| Table (multi) | `ts_<operation>_by` | `ts_forecast_by`, `ts_detect_changepoints_by` |

### Dual Registration

All functions are registered with two names for compatibility:
- `ts_<name>` - Short form (recommended)
- `anofox_fcst_ts_<name>` - Prefixed form (for namespacing)

Both forms are identical in functionality.

---

## Parameter Ordering

### Standard Order by API Type

```sql
-- Scalar Functions (array-based)
ts_function(values DOUBLE[], ...additional_params)

-- Aggregate Functions
ts_function_agg(date_col TIMESTAMP, value_col DOUBLE, ...additional_params)

-- Table Macro (single series, no grouping)
ts_function(source VARCHAR, date_col COLUMN, value_col COLUMN, ...params)

-- Table Macro (multi-series with grouping)
ts_function_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, ...params)
```

### Column Parameter Order

For table macros operating on time series:

1. `source` - Table name (VARCHAR, quoted)
2. `group_col` - Series identifier column (COLUMN, unquoted) - only for `_by` functions
3. `date_col` - Date/timestamp column (COLUMN, unquoted)
4. `value_col` / `target_col` - Value column (COLUMN, unquoted)
5. Operation-specific parameters
6. `params` - MAP or STRUCT for optional configuration

### Params Convention

The `params` parameter supports both MAP and STRUCT syntax (v0.4.0+):

```sql
-- STRUCT allows mixed types (recommended)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    {'method': 'Naive', 'gap': 2, 'clip_horizon': true});

-- MAP requires homogeneous string values (legacy)
SELECT * FROM ts_backtest_auto('sales', id, date, value, 7, 3, '1d',
    MAP{'method': 'Naive', 'gap': '2', 'clip_horizon': 'true'});
```

---

## API Coverage Matrix

The following table shows which API variants exist for each functionality:

| Functionality | Scalar | Aggregate | Table Macro | Table `_by` |
|---------------|--------|-----------|-------------|-------------|
| **Forecasting** | - | `ts_forecast_agg` | `ts_forecast` | `ts_forecast_by` |
| **Statistics** | `ts_stats` | - | `ts_stats` | `ts_stats_by` |
| **Features** | `ts_features_scalar` | `ts_features_agg` | - | `ts_features_by` |
| **Changepoints** | `ts_detect_changepoints` | `ts_detect_changepoints_agg` | `ts_detect_changepoints` | `ts_detect_changepoints_by` |
| **Seasonality** | - | `ts_classify_seasonality_agg` | - | `ts_classify_seasonality_by` |
| **Period Detection** | `ts_detect_periods` | - | - | - |
| **Decomposition** | - | - | `ts_mstl_decomposition` | - |
| **Metrics** | `ts_mae`, `ts_rmse`, etc. | - | - | - |
| **Conformal** | `ts_conformal_*` | - | `ts_conformal` macros | - |
| **Data Quality** | `ts_data_quality` | - | `ts_data_quality` | - |

---

## Examples by Pattern

### Scalar Functions

Operate on arrays, composable with SQL aggregation:

```sql
-- Compute MAE per product
SELECT
    product_id,
    ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS mae
FROM results
GROUP BY product_id;

-- Detect seasonality
SELECT
    product_id,
    (ts_detect_periods(LIST(value ORDER BY date))).primary_period AS season
FROM sales
GROUP BY product_id;
```

### Aggregate Functions

Accumulate values via GROUP BY:

```sql
-- Forecast per product
SELECT
    product_id,
    ts_forecast_agg(date, value, 'AutoETS', 12, MAP{}) AS forecast
FROM sales
GROUP BY product_id;

-- Extract features per product
SELECT
    product_id,
    ts_features_agg(date, value) AS features
FROM sales
GROUP BY product_id;
```

### Table Macros

Operate on entire tables:

```sql
-- Single series forecast
SELECT * FROM ts_forecast('single_series', date, value, 'AutoETS', 12, MAP{});

-- Multi-series forecast
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'AutoETS', 12, MAP{});

-- Statistics per series
SELECT * FROM ts_stats_by('sales', product_id, date, value);

-- One-liner backtest
SELECT * FROM ts_backtest_auto('sales', product_id, date, value, 7, 3, '1d', MAP{});
```

---

## Return Type Conventions

### Scalar Functions

- Simple metrics: Return `DOUBLE`
- Complex results: Return `STRUCT` with named fields

```sql
-- Simple metric
ts_mae(actual[], forecast[]) → DOUBLE

-- Complex result
ts_stats(values[]) → STRUCT(length, mean, std_dev, ...)
ts_detect_periods(values[]) → STRUCT(periods[], confidences[], primary_period, ...)
```

### Aggregate Functions

Always return `STRUCT` with operation-specific fields:

```sql
ts_forecast_agg(...) → STRUCT(
    forecast_step[], forecast_timestamp[], point_forecast[],
    lower_90[], upper_90[], model_name, ...
)
```

### Table Macros

Return tables with operation-specific columns:

```sql
-- ts_forecast_by returns:
| group_col | ds | forecast | lower | upper |

-- ts_backtest_auto returns:
| fold_id | group_col | date | forecast | actual | error | abs_error | ... |
```

---

## Frequency Strings

Time-based functions accept frequency strings in two formats:

| Polars Style | DuckDB INTERVAL | Meaning |
|--------------|-----------------|---------|
| `'1d'` | `'1 day'` | Daily |
| `'1h'` | `'1 hour'` | Hourly |
| `'30m'` | `'30 minutes'` | 30 minutes |
| `'1w'` | `'1 week'` | Weekly |
| `'1mo'` | `'1 month'` | Monthly |
| `'1q'` | `'3 months'` | Quarterly |
| `'1y'` | `'1 year'` | Yearly |

Both formats are automatically converted internally.

---

## Adding New Functions

When adding new time series functionality, follow this checklist:

1. **Naming**: Use `ts_<operation>` pattern
2. **Register both names**: `ts_*` and `anofox_fcst_ts_*`
3. **Parameter order**: Follow standard column ordering
4. **Consider all tiers**: Decide which API variants to implement
5. **Return types**: Use STRUCT for complex results
6. **Document**: Add to appropriate `docs/api/*.md` file

---

*See also: [API Reference](../API_REFERENCE.md)*
