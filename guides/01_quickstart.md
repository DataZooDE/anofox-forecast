# Quick Start Guide

## Introduction

The Anofox Forecast extension provides time series forecasting capabilities within DuckDB. The extension implements 31 forecasting models through the anofox-time C++ library, accessible via SQL table functions, aggregates, and scalar functions.

### Core Forecasting Functions

- **anofox_fcst_ts_forecast**: Single time series forecasting
- **anofox_fcst_ts_forecast_by**: Multiple time series forecasting with GROUP BY parallelization
- **anofox_fcst_ts_forecast_agg**: Aggregate function for custom GROUP BY patterns

### Prerequisites

- DuckDB version 1.4.2 or higher
- Anofox Forecast extension built and accessible
- Basic SQL knowledge

For complete function signatures, parameters, and model specifications, see [API Reference](../docs/API_REFERENCE.md).

## Single Series Forecasting

### Setup

Load the extension and create sample data:

```sql
-- Create a simple daily sales dataset
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Verify data
SELECT * FROM my_sales LIMIT 5;
```

### Generate Forecast

Use `anofox_fcst_ts_forecast` to generate forecasts for a single time series:

```sql
-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Forecast next 14 days
SELECT * FROM anofox_fcst_ts_forecast(
    'my_sales',      -- table name
    date,            -- date column
    sales,           -- value column
    'AutoETS',       -- model (automatic)
    14,              -- horizon (14 days)
    MAP{'seasonal_period': 7}  -- weekly seasonality
);
```

**Output Schema:**

The function returns a table with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| `forecast_step` | INTEGER | Sequential horizon step (1, 2, ..., horizon) |
| `date` | DATE \| TIMESTAMP \| INTEGER | Forecast timestamp (type matches input date column) |
| `point_forecast` | DOUBLE | Point forecast value |
| `lower` | DOUBLE | Lower bound of prediction interval |
| `upper` | DOUBLE | Upper bound of prediction interval |
| `model_name` | VARCHAR | Name of the forecasting model used |
| `insample_fitted` | DOUBLE[] | In-sample fitted values (empty unless `return_insample: true`) |
| `confidence_level` | DOUBLE | Confidence level for prediction intervals (default: 0.90) |

**Parameters:**

- `table_name`: Source table name (VARCHAR)
- `date_col`: Date/timestamp column name
- `value_col`: Value column name to forecast
- `method`: Model name (see [Supported Models](../docs/API_REFERENCE.md#supported-models))
- `horizon`: Number of future periods to forecast (must be > 0)
- `params`: Configuration MAP with model-specific parameters

**Notes:**

- All parameters are positional (named parameters with `:=` syntax are not supported)
- Date column type (DATE, TIMESTAMP, or INTEGER) is preserved in output
- Prediction intervals are computed at the specified confidence level (default 0.90)
- Timestamps are generated based on training data intervals (configurable via `generate_timestamps`)

## Multiple Models Comparison

Compare forecasts from different models using the same dataset:

```sql
-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Compare forecasts from different models using the same dataset
-- Model 1: AutoETS (automatic exponential smoothing)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'AutoETS', 14, MAP{'seasonal_period': 7})
LIMIT 5;

-- Model 2: SES (Simple Exponential Smoothing)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'SES', 14, MAP{})
LIMIT 5;

-- Model 3: Theta (theta decomposition method)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'Theta', 14, MAP{'seasonal_period': 7})
LIMIT 5;

```

**Model Selection:**

- **AutoETS**: Automatic exponential smoothing with trend and seasonality selection
- **SeasonalNaive**: Seasonal naive method (repeats value from same period in previous cycle)
- **Theta**: Theta decomposition method with seasonal adjustment

All three models require the `seasonal_period` parameter for weekly seasonality (7 days). For complete model specifications and parameter requirements, see [Supported Models](../docs/API_REFERENCE.md#supported-models).

## Multiple Series Forecasting

Use `anofox_fcst_ts_forecast_by` to forecast multiple time series in parallel:

```sql
-- Create multi-product data
CREATE TABLE product_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Forecast all products at once
SELECT 
    product_id,
    forecast_step,
    ROUND(point_forecast, 2) AS forecast
FROM anofox_fcst_ts_forecast_by(
    'product_sales',
    product_id,      -- GROUP BY this column
    date,
    sales,
    'AutoETS',
    14,
    MAP{'seasonal_period': 7}
)
WHERE forecast_step <= 3
ORDER BY product_id, forecast_step;
```

**Output Schema:**

The function returns the same columns as `anofox_fcst_ts_forecast`, plus:

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Grouping column value (type matches input) |

**Parameters:**

- `table_name`: Source table name (VARCHAR)
- `group_col`: Grouping column name (any type, preserved in output)
- `date_col`: Date/timestamp column name
- `value_col`: Value column name to forecast
- `method`: Model name
- `horizon`: Number of future periods to forecast
- `params`: Configuration MAP

**Behavioral Notes:**

- Automatic parallelization: series are distributed across CPU cores
- Group column type is preserved in output
- Independent parameter validation per series
- Efficient for thousands of series

## Troubleshooting

**Error: "SeasonalNaive model requires 'seasonal_period' parameter"**

Seasonal models require the `seasonal_period` parameter in the `params` MAP. Ensure the parameter is provided:

```sql
SELECT * FROM anofox_fcst_ts_forecast(
    'my_sales', date, sales, 'SeasonalNaive', 14,
    {'seasonal_period': 7}  -- Required for seasonal models
);
```

**Error: "Table with name X does not exist"**

Ensure the source table exists and the table name is spelled correctly. Table names are case-sensitive.

**Error: "Binder Error" for date column types**

Date column types must match the frequency parameter type:

- VARCHAR frequency → DATE or TIMESTAMP date column required
- INTEGER frequency → INTEGER or BIGINT date column required

For complete error handling and parameter validation details, see [API Reference](../docs/API_REFERENCE.md).
