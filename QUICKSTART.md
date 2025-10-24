# Quick Start Guide

## 5-Minute Setup

### 1. Build the Extension

```bash
cd /home/simonm/projects/ai/anofox-forecast
make release
```

### 2. Start DuckDB and Load Extension

```bash
duckdb
```

```sql
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
```

### 3. Run Your First Forecast

```sql
-- Create sample data
CREATE TABLE daily_sales (
    date TIMESTAMP,
    sales DOUBLE
);

INSERT INTO daily_sales VALUES
    ('2024-01-01', 100), ('2024-01-02', 105), ('2024-01-03', 102),
    ('2024-01-04', 108), ('2024-01-05', 112), ('2024-01-06', 115),
    ('2024-01-07', 118), ('2024-01-08', 120), ('2024-01-09', 122),
    ('2024-01-10', 125), ('2024-01-11', 128), ('2024-01-12', 130);

-- Generate 7-day forecast using Naive method
SELECT 
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name
FROM FORECAST('date', 'sales', 'Naive', 7, NULL)
ORDER BY forecast_step;
```

### 4. Try Different Models

```sql
-- Simple Moving Average (uses last 5 values by default)
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) as forecast
FROM FORECAST('date', 'sales', 'SMA', 7, NULL)
ORDER BY forecast_step;

-- Compare models side by side
WITH naive_fc AS (
    SELECT forecast_step, point_forecast as naive
    FROM FORECAST('date', 'sales', 'Naive', 5, NULL)
),
sma_fc AS (
    SELECT forecast_step, point_forecast as sma
    FROM FORECAST('date', 'sales', 'SMA', 5, NULL)
)
SELECT 
    n.forecast_step,
    ROUND(n.naive, 2) as naive_forecast,
    ROUND(s.sma, 2) as sma_forecast,
    ROUND(ABS(n.naive - s.sma), 2) as difference
FROM naive_fc n
JOIN sma_fc s USING (forecast_step)
ORDER BY forecast_step;
```

## Available Models (Phase 1)

| Model | Description | Parameters |
|-------|-------------|------------|
| `Naive` | Last value carried forward | None |
| `SMA` | Simple Moving Average | `window` (default: 5) |
| `SeasonalNaive` | Last seasonal value | `seasonal_period` (required*) |

\* Note: STRUCT parameter support coming in Phase 2. For now, use NULL and defaults will apply.

## Output Columns

Every forecast returns:
- `forecast_step`: Step number (1, 2, 3, ...)
- `point_forecast`: The predicted value
- `lower_95` / `upper_95`: Prediction interval bounds
- `model_name`: Which model was used
- `fit_time_ms`: How long fitting took

## Common Patterns

### Forecast Next Week

```sql
SELECT 
    date '2024-01-13' + INTERVAL (forecast_step - 1) DAY as forecast_date,
    point_forecast
FROM FORECAST('date', 'sales', 'SMA', 7, NULL)
ORDER BY forecast_step;
```

### Get Only Point Forecasts

```sql
SELECT forecast_step, point_forecast 
FROM FORECAST('date', 'sales', 'Naive', 10, NULL);
```

### Include Confidence Intervals

```sql
SELECT 
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    (upper_95 - lower_95) / 2 as margin_of_error
FROM FORECAST('date', 'sales', 'SMA', 5, NULL);
```

## Troubleshooting

### "Extension not found"
```bash
# Make sure you built the extension
make release

# Check the file exists
ls -la build/release/extension/anofox_forecast/
```

### "Column not found"
```sql
-- Make sure column names match your table exactly (case-sensitive)
PRAGMA table_info('daily_sales');
```

### "Invalid model name"
```sql
-- Use exact names: 'Naive', 'SMA', 'SeasonalNaive' (case-sensitive)
SELECT * FROM FORECAST('date', 'sales', 'Naive', 5, NULL);  -- ✓
SELECT * FROM FORECAST('date', 'sales', 'naive', 5, NULL);  -- ✗
```

## Next Steps

- See `docs/USAGE.md` for detailed documentation
- See `docs/PHASE2_PLAN.md` for upcoming features
- See `IMPLEMENTATION_SUMMARY.md` for technical details

## Testing

```bash
# Run all tests
make test_release

# Or with debug build
make test_debug
```

## What's Coming in Phase 2

- 🚀 30+ additional forecasting models
- 🚀 GROUP BY support for batch forecasting
- 🚀 STRUCT parameters with model configuration
- 🚀 ENSEMBLE() and BACKTEST() functions
- 🚀 Proper statistical prediction intervals
- 🚀 Model comparison and selection tools

---

**You're ready to forecast! 📈**
