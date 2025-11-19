# Quick Start Guide - 5 Minutes to Your First Forecast

## Goal

Generate your first time series forecast using the Anofox Forecast extension's SQL API.

## Prerequisites

- DuckDB installed (version 1.4.1+)
- Anofox-forecast extension built and accessible
- Basic understanding of SQL table functions

## Step 1: Load Extension (30 seconds)

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

## Step 2: Create Sample Data (1 minute)

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

## Step 3: Generate Forecast (30 seconds)

```sql
-- Forecast next 14 days
SELECT * FROM TS_FORECAST(
    'my_sales',      -- table name
    date,            -- date column
    sales,           -- value column
    'AutoETS',       -- model (automatic)
    14,              -- horizon (14 days)
    {'seasonal_period': 7}  -- weekly seasonality
);
```

**Output Schema**:

```
┌───────────────┬────────────┬────────────────┬────────┬────────┐
│ forecast_step │  date_col  │ point_forecast │ lower  │ upper  │
│    INTEGER    │    DATE    │     DOUBLE     │ DOUBLE │ DOUBLE │
├───────────────┼────────────┼────────────────┼────────┼────────┤
│             1 │ 2023-04-01 │         118.52 │ 109.74 │ 127.30 │
│             2 │ 2023-04-02 │         125.43 │ 116.65 │ 134.21 │
│             3 │ 2023-04-03 │         121.89 │ 113.11 │ 130.67 │
│           ... │        ... │            ... │    ... │    ... │
```

**Schema Notes**:

- `forecast_step`: Sequential horizon step (1, 2, ..., horizon)
- `date`: Forecast timestamp (type matches input date column)
- `point_forecast`: Point forecast value
- `lower`, `upper`: Prediction interval bounds (default 90% confidence)

## Step 4: Visualize (Optional)

```sql
-- Simple ASCII visualization
WITH fc AS (
    SELECT 
        forecast_step,
        point_forecast,
        REPEAT('█', CAST(point_forecast / 5 AS INT)) AS bar
    FROM TS_FORECAST('my_sales', date, sales, 'AutoETS', 14, {'seasonal_period': 7})
)
SELECT forecast_step, ROUND(point_forecast, 1) AS forecast, bar
FROM fc;
```

## Step 5: Multiple Series (1 minute)

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
FROM TS_FORECAST_BY(
    'product_sales',
    product_id,      -- GROUP BY this column
    date,
    sales,
    'AutoETS',
    14,
    {'seasonal_period': 7}
)
WHERE forecast_step <= 3
ORDER BY product_id, forecast_step;
```

## 🎉 You Did It

You've just:

- ✅ Loaded the extension
- ✅ Created sample data
- ✅ Generated a forecast
- ✅ Forecasted multiple series in parallel

## 🆘 Troubleshooting

**Error: "SeasonalNaive model requires 'seasonal_period' parameter"**

```sql
-- Add seasonal_period to params
{'seasonal_period': 7}  -- for weekly data
```
