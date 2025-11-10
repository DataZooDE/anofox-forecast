# Anofox Forecast Extension - Usage Guide

## Overview

The `anofox_forecast` extension provides time series forecasting capabilities for DuckDB using the anofox-time library. This Phase 1 implementation supports three baseline forecasting methods: SMA, Naive, and Seasonal Naive.

## Installation

### Building the Extension

1. **Build anofox-time library** (optional - it's included in the extension build):

```bash
cd anofox-time
mkdir -p build && cd build
cmake .. -DBUILD_PYTHON_BINDINGS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
make -j4
cd ../..
```

2. **Build the DuckDB extension**:

```bash
make debug
# or for release build:
make release
```

### Loading the Extension

```sql
LOAD 'build/debug/extension/anofox_forecast/anofox_forecast.duckdb_extension';
```

Or in release mode:

```sql
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
```

## FORECAST Function

### Syntax

```sql
FORECAST(
    timestamp_column VARCHAR,
    value_column VARCHAR,
    model VARCHAR,
    horizon INTEGER,
    model_params STRUCT
) → TABLE (
    forecast_step INTEGER,
    point_forecast DOUBLE,
    lower_95 DOUBLE,
    upper_95 DOUBLE,
    model_name VARCHAR,
    fit_time_ms DOUBLE
)
```

### Parameters

- `timestamp_column`: Name of the timestamp column in your data
- `value_column`: Name of the value column to forecast
- `model`: Model name - one of: `'SMA'`, `'Naive'`, `'SeasonalNaive'`
- `horizon`: Number of future periods to forecast (must be positive)
- `model_params`: STRUCT containing model-specific parameters (use NULL if none needed)

### Supported Models (Phase 1)

#### 1. Naive

**Description**: Random walk model - all forecasts equal the last observed value.

**Parameters**: None

**Example**:

```sql
SELECT * FROM FORECAST('timestamp', 'sales', 'Naive', 12, NULL)
ORDER BY forecast_step;
```

#### 2. SMA (Simple Moving Average)

**Description**: Forecasts based on the average of the last N observations.

**Parameters**:

- `window`: Number of observations to average (default: 5)

**Example**:

```sql
-- Using default window (5)
SELECT * FROM FORECAST('date', 'revenue', 'SMA', 7, NULL);

-- Note: STRUCT parameters will be supported in a future update
```

#### 3. SeasonalNaive

**Description**: Forecasts equal to the last observed value from the same season.

**Parameters**:

- `seasonal_period`: Length of the seasonal cycle (required)

**Example**:

```sql
-- Note: STRUCT parameters will be supported in a future update
-- For now, use the default seasonal_period detection
```

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `forecast_step` | INTEGER | Forecast horizon step (1, 2, ..., horizon) |
| `point_forecast` | DOUBLE | Point forecast value |
| `lower_95` | DOUBLE | Lower bound of 95% prediction interval |
| `upper_95` | DOUBLE | Upper bound of 95% prediction interval |
| `model_name` | VARCHAR | Name of the model used |
| `fit_time_ms` | DOUBLE | Time taken to fit the model (milliseconds) |

## Examples

### Simple Forecast

```sql
-- Create sample data
CREATE TABLE sales (date TIMESTAMP, amount DOUBLE);
INSERT INTO sales VALUES 
    ('2024-01-01', 100), ('2024-01-02', 105), ('2024-01-03', 110),
    ('2024-01-04', 108), ('2024-01-05', 112), ('2024-01-06', 115),
    ('2024-01-07', 118), ('2024-01-08', 120), ('2024-01-09', 122),
    ('2024-01-10', 125);

-- Generate 5-step forecast using Naive method
SELECT 
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name
FROM FORECAST('date', 'amount', 'Naive', 5, NULL)
ORDER BY forecast_step;

-- Result:
-- forecast_step | point_forecast | lower_95 | upper_95 | model_name
-- 1            | 125.0         | 112.5    | 137.5    | Naive
-- 2            | 125.0         | 112.5    | 137.5    | Naive
-- ...
```

### Comparing Models

```sql
-- Compare Naive vs SMA forecasts
WITH naive_forecast AS (
    SELECT forecast_step, point_forecast as naive_pred
    FROM FORECAST('date', 'amount', 'Naive', 5, NULL)
),
sma_forecast AS (
    SELECT forecast_step, point_forecast as sma_pred
    FROM FORECAST('date', 'amount', 'SMA', 5, NULL)
)
SELECT 
    naive_forecast.forecast_step,
    naive_pred,
    sma_pred,
    abs(naive_pred - sma_pred) as difference
FROM naive_forecast
JOIN sma_forecast USING (forecast_step)
ORDER BY forecast_step;
```

### Performance Metrics

```sql
-- Check model fitting time
SELECT 
    model_name,
    AVG(fit_time_ms) as avg_fit_time_ms,
    MIN(fit_time_ms) as min_fit_time_ms,
    MAX(fit_time_ms) as max_fit_time_ms
FROM FORECAST('date', 'amount', 'SMA', 10, NULL)
GROUP BY model_name;
```

## Current Limitations (Phase 1)

1. **No GROUP BY support yet**: Batch forecasting across multiple series will be implemented in Phase 2
2. **STRUCT parameters**: Currently only NULL is supported for model_params. Named parameters will be added in future
3. **Prediction intervals**: Currently simplified (±10% of point forecast). Proper statistical intervals coming in Phase 2
4. **Limited models**: Only 3 baseline methods available. 30+ models will be added in Phase 2

## Phase 2 Roadmap

- Full GROUP BY support for batch forecasting
- All 30+ models from anofox-time (ARIMA, ETS, Theta, etc.)
- ENSEMBLE() table function
- BACKTEST() table function  
- Proper prediction intervals
- STRUCT parameter support with named fields
- Scalar utility functions (detect_seasonality, etc.)

## Troubleshooting

### Extension Not Loading

If you get "Extension not found", ensure:

1. The extension was built successfully (`make debug` or `make release`)
2. You're using the correct path to the `.duckdb_extension` file
3. The file has execute permissions

### Debug Output

The extension includes comprehensive debug logging. To see debug output:

```bash
./build/debug/test/unittest "your_test.test" 2>&1
```

Debug messages show:

- Extension loading
- Function registration
- Parameter validation
- Model creation and fitting
- Forecast generation

## Support

For issues or questions:

- Check the debug output for detailed error messages
- Verify your data types (TIMESTAMP and DOUBLE)
- Ensure time series has sufficient data points for the model
- Check that horizon is positive
