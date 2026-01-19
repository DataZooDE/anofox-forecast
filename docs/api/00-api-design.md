# API Design Guide

> Consistent patterns for the Anofox Forecast Extension API

## Overview

The extension provides three API styles for time series operations. This document defines the naming conventions, parameter ordering, and patterns that should be followed for consistency.

## Three-Tier API Pattern

For any time series operation `<operation>`, the API can provide up to three variants:

| API Type | Function Pattern | Use Case |
|----------|------------------|----------|
| **Scalar** | `ts_<operation>(values[], ...)` | Simple array→value computations |
| **Aggregate** | `ts_<operation>_agg(date, value, ...)` | Direct GROUP BY accumulation |
| **Table Macro** | `ts_<operation>_by(source, group, date, value, ...)` | Multi-series table operation |

### When to Implement Each Tier

Not every operation needs all three tiers. Use this decision guide:

| Tier | Implement When | Skip When |
|------|----------------|-----------|
| **Scalar** | Simple metrics (array→number), composable in expressions | Complex output, needs table context |
| **Aggregate** | Natural fit for GROUP BY workflows | Operation is inherently table-level |
| **Table `_by`** | Multi-series operations, clean table output | N/A (always implement for grouped ops) |
| **Table (single)** | Single-series version is useful | Only grouped version makes sense |

### Scalar Functions: Design Guidance

**DO expose public scalars for:**
- Simple metrics: `ts_mae(actual[], forecast[])` → `DOUBLE`
- Composable in SQL expressions: `WHERE ts_mae(a, b) < threshold`
- Conformal prediction workflow (arrays passed between steps)

**DON'T expose public scalars for:**
- Complex structured output (use table macro instead)
- Operations requiring table context (dates, ordering)
- When aggregate provides cleaner syntax

**Internal scalars** (underscore prefix like `_ts_stats`) are implementation details for table macros, not user-facing API.

### When to Use Each (User Perspective)

```sql
-- SCALAR: Simple metrics, composable in expressions
SELECT product_id, ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS mae
FROM results GROUP BY product_id;

-- AGGREGATE: Complex operations with GROUP BY (avoids LIST() boilerplate)
SELECT product_id, ts_forecast_agg(date, value, 'AutoETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;

-- TABLE MACRO: Cleanest syntax, structured table output
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
| **Statistics** | `_ts_stats`* | `ts_stats_agg` | `ts_stats` | `ts_stats_by` |
| **Features** | `_ts_features`* | `ts_features_agg` | `ts_features_table` | `ts_features_by` |
| **Data Quality** | `_ts_data_quality`* | `ts_data_quality_agg` | `ts_data_quality` | `ts_data_quality_by` |
| **Changepoints** | - | `ts_detect_changepoints_agg` | `ts_detect_changepoints` | `ts_detect_changepoints_by` |
| **Seasonality** | - | `ts_classify_seasonality_agg` | `ts_classify_seasonality` | `ts_classify_seasonality_by` |
| **Period Detection** | `_ts_detect_periods`* | `ts_detect_periods_agg` | `ts_detect_periods` | `ts_detect_periods_by` |
| **Decomposition** | `_ts_mstl_decomposition`* | - | - | `ts_mstl_decomposition_by` |
| **Metrics** | `ts_mae`, `ts_rmse`, ...† | - | - | `ts_mae_by`, `ts_rmse_by`, ...† |
| **Conformal** | `ts_conformal_*`‡ | - | `ts_conformal_calibrate` | `ts_conformal_by`, `ts_conformal_apply_by` |
| **Data Prep** | - | - | - | `ts_fill_gaps_by`, `ts_diff_by`, `ts_drop_*_by`§ |
| **Cross-Val** | - | - | - | `ts_cv_split_by`, `ts_backtest_auto_by`¶ |

**Legend:** ✓ = exists, - = not applicable

*Internal scalar functions (underscore prefix) - implementation details for table macros
†12 metric functions - see [Evaluation Metrics](07-evaluation-metrics.md)
‡9 conformal scalar functions - see [Conformal Prediction](11-conformal-prediction.md)
§15+ data preparation macros - see [Data Preparation](03-data-preparation.md)
¶8+ cross-validation macros - see [Cross-Validation](06-cross-validation.md)

### API Design Rationale

| Category | Scalar | Aggregate | Table `_by` | Notes |
|----------|--------|-----------|-------------|-------|
| **Metrics** | ✓ Public | - | ✓ | Scalars useful in expressions; `_by` enables per-group metrics |
| **Statistics/Features** | Internal | ✓ | ✓ | Aggregate cleaner than `LIST()` boilerplate |
| **Decomposition** | Internal | - | ✓ | Complex output suits table format |
| **Data Prep** | - | - | ✓ | Requires table context (dates, ordering) |
| **Cross-Val** | - | - | ✓ | Inherently table-level operations |
| **Conformal** | ✓ Public | - | ✓ | Workflow passes arrays; also needs table ops |

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
SELECT * FROM ts_backtest_auto_by('sales', product_id, date, value, 7, 3, '1d', {'method': 'AutoETS'});
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
4. **Choose appropriate tiers** (see decision guide below)
5. **Return types**: Use STRUCT for complex results
6. **Document**: Add to appropriate `docs/api/*.md` file

### Tier Selection Guide

```
Is output simple (array → number)?
├─ YES → Implement PUBLIC SCALAR (ts_<op>)
│        Example: ts_mae, ts_rmse
└─ NO → Is it a grouped/multi-series operation?
        ├─ YES → Implement TABLE MACRO _BY (ts_<op>_by)
        │        Also consider: Aggregate if GROUP BY natural
        └─ NO → Implement TABLE MACRO (ts_<op>)

Does it need internal array processing for table macros?
├─ YES → Implement INTERNAL SCALAR (_ts_<op>)
│        Keep hidden from users (underscore prefix)
└─ NO → Skip scalar tier
```

### Examples by Category

| New Function Type | Implement | Skip |
|-------------------|-----------|------|
| Simple metric | Scalar, Table `_by` | Aggregate (scalar suffices) |
| Complex analysis | Aggregate, Table `_by` | Public scalar |
| Data transformation | Table `_by` only | Scalar, Aggregate |
| Table-level operation | Table `_by` only | Scalar, Aggregate |

---

*See also: [API Reference](../API_REFERENCE.md)*
