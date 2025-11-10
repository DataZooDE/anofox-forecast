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
- `date_col`: Forecast timestamp (type matches input date column)
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

## Next Steps

### Learn More

- [Basic Forecasting Guide](30_basic_forecasting.md) - Detailed walkthrough
- [Model Selection Guide](40_model_selection.md) - Choose the best model
- [EDA & Data Prep](11_exploratory_analysis.md) - Clean your data first

### Try Different Models

```sql
-- Compare models
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'SeasonalNaive', 14, {'seasonal_period': 7});
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'Theta', 14, {'seasonal_period': 7});
SELECT * FROM TS_FORECAST('my_sales', date, sales, 'ARIMA', 14, {'p': 1, 'd': 0, 'q': 1});
```

### Evaluate Accuracy

```sql
-- If you have actual future values
SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse
FROM results;
```

### Check Data Quality

```sql
-- Analyze your data before forecasting
SELECT * FROM TS_STATS('my_sales', product_id, date, sales);
SELECT * FROM TS_QUALITY_REPORT('stats', 30);
```

## 💡 Pro Tips

1. **Seasonality detection first**: Use `TS_DETECT_SEASONALITY_ALL()` to determine appropriate `seasonal_period` parameter
2. **Start with AutoETS**: Automatic model selection from 30+ ETS candidates with parameter optimization
3. **Prediction intervals**: `lower` and `upper` bounds computed at specified `confidence_level` (default 0.90)
4. **Metric validation**: Use array-based metrics with `LIST()` aggregation for GROUP BY operations
5. **Data quality**: Run `TS_STATS()` and `TS_QUALITY_REPORT()` before forecasting

## ⚠️ Common Pitfalls

❌ **Insufficient data**: Seasonal models require ≥ 2x `seasonal_period` observations (3-4x recommended)  
❌ **Missing timestamps**: Use `TS_FILL_GAPS()` for DATE/TIMESTAMP, `TS_FILL_GAPS_INT()` for INTEGER  
❌ **Constant series**: Models will fail - filter with `TS_DROP_CONSTANT()` before forecasting  
❌ **Wrong seasonality**: Incorrect `seasonal_period` degrades accuracy - use `TS_DETECT_SEASONALITY()`  
❌ **Pandas datetime64[ns]**: Convert to DATE via `df['date'].dt.date` before passing to DuckDB  

## 🆘 Troubleshooting

**Error: "SeasonalNaive model requires 'seasonal_period' parameter"**

```sql
-- Add seasonal_period to params
{'seasonal_period': 7}  -- for weekly data
```

**Error: "Series too short"**

```sql
-- Need more data or reduce min_length
TS_DROP_SHORT('table', group_col, date, 14)  -- Keep series with ≥14 obs
```

**Poor forecast accuracy?**

```sql
-- 1. Check data quality
SELECT * FROM TS_STATS('my_sales', product_id, date, sales);

-- 2. Detect seasonality
SELECT * FROM TS_DETECT_SEASONALITY_ALL('my_sales', product_id, date, sales);

-- 3. Try different models
-- See guides/11_model_selection.md
```

---

**Ready for more?** → [Basic Forecasting Guide](30_basic_forecasting.md)

**Need help?** → Check [guides/](.) for comprehensive documentation!
