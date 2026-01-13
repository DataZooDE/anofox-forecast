# Forecasting Examples

> **Forecasting is the science of making informed guesses about the future.**

This folder contains runnable SQL examples demonstrating time series forecasting with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_forecasting_examples.sql`](synthetic_forecasting_examples.sql) | 8 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/forecasting/synthetic_forecasting_examples.sql
```

---

## Available Models (32 Total)

The extension provides 32 forecasting models organized in 7 categories:

| Category | Models | Best For |
|----------|--------|----------|
| **Automatic** | `AutoETS`, `AutoARIMA`, `AutoTheta`, `AutoMFLES`, `AutoMSTL`, `AutoTBATS` | Unknown patterns |
| **Basic** | `Naive`, `SMA`, `SeasonalNaive`, `SES`, `SESOptimized`, `RandomWalkDrift` | Baselines |
| **Exponential Smoothing** | `Holt`, `HoltWinters`, `SeasonalES`, `SeasonalESOptimized` | Trend + seasonality |
| **Theta** | `Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta` | Robust forecasting |
| **State Space** | `ETS`, `AutoETS`, `ARIMA`, `AutoARIMA` | General purpose |
| **Multi-Seasonal** | `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS` | Complex seasonality |
| **Intermittent** | `CrostonClassic`, `CrostonOptimized`, `CrostonSBA`, `ADIDA`, `IMAPA`, `TSB` | Sparse demand |

---

## Patterns Overview

### Pattern 1: Quick Start (Basic Forecast)

**Use case:** Generate forecasts for a single time series.

```sql
SELECT * FROM ts_forecast('sales_data', date, revenue, 'AutoETS', 12, MAP{});
```

**See:** `synthetic_forecasting_examples.sql` Section 1

---

### Pattern 2: Multi-Series Forecasting

**Use case:** Forecast multiple products/stores in one query.

```sql
SELECT * FROM ts_forecast_by(
    'sales_data', product_id, date, quantity,
    'ETS', 30, MAP{}
);
```

**See:** `synthetic_forecasting_examples.sql` Section 2

---

### Pattern 3: Model Selection (Baseline to Advanced)

**Use case:** Compare simple baselines to sophisticated models.

**Models compared:**
- `Naive` - Last value repeated
- `SeasonalNaive` - Last season repeated
- `AutoETS` - Automatic model selection
- `AutoARIMA` - ARIMA with automatic tuning

**See:** `synthetic_forecasting_examples.sql` Section 3

---

### Pattern 4: Seasonal Models

**Use case:** Data with clear weekly/monthly/yearly patterns.

**Key models:**
- `HoltWinters` - Classic triple exponential smoothing
- `SeasonalES` - Seasonal exponential smoothing
- `SeasonalNaive` - Simple seasonal baseline

```sql
SELECT * FROM ts_forecast_by(
    'weekly_sales', store_id, week, revenue,
    'HoltWinters', 52,
    MAP{'seasonal_period': '52'}
);
```

**See:** `synthetic_forecasting_examples.sql` Section 4

---

### Pattern 5: Multiple Seasonality

**Use case:** Hourly data with daily AND weekly patterns.

**Key models:**
- `MSTL` - STL decomposition with multiple periods
- `MFLES` - Multiple Frequency Loess
- `TBATS` - Box-Cox ARMA Trend Seasonal

```sql
SELECT * FROM ts_forecast_by(
    'hourly_data', sensor_id, timestamp, reading,
    'MSTL', 168,
    MAP{'seasonal_periods': '[24, 168]'}  -- daily + weekly
);
```

**See:** `synthetic_forecasting_examples.sql` Section 5

---

### Pattern 6: Intermittent Demand

**Use case:** Spare parts, luxury items, or irregular sales.

**Key models:**
- `CrostonClassic` - Original Croston method
- `CrostonSBA` - Syntetos-Boylan Approximation
- `ADIDA` - Aggregate-Disaggregate approach
- `TSB` - Teunter-Syntetos-Babai

```sql
SELECT * FROM ts_forecast_by(
    'spare_parts', sku, date, demand,
    'CrostonSBA', 12, MAP{}
);
```

**See:** `synthetic_forecasting_examples.sql` Section 6

---

### Pattern 7: Exogenous Variables

**Use case:** Include external factors (temperature, promotions, holidays).

**Supported models:** `AutoARIMA`, `OptimizedTheta`, `MFLES`

```sql
SELECT * FROM ts_forecast_exog(
    'sales', date, amount,
    'temperature,promotion',  -- external features
    'future_exog',            -- table with future values
    'AutoARIMA', 7, MAP{}
);
```

**See:** `synthetic_forecasting_examples.sql` Section 7

---

### Pattern 8: Aggregate Function (Custom Grouping)

**Use case:** Complex GROUP BY logic not covered by table macros.

```sql
SELECT
    region,
    product_category,
    ts_forecast_agg(date, revenue, 'ETS', 12, MAP{}) AS forecast
FROM sales
GROUP BY region, product_category;
```

**See:** `synthetic_forecasting_examples.sql` Section 8

---

## Key Concepts

### Model Parameters

All parameters are passed via the `params` MAP argument:

```sql
-- Custom smoothing parameters
MAP{'alpha': '0.5', 'beta': '0.2'}

-- Seasonal period
MAP{'seasonal_period': '7'}

-- Multiple seasonal periods
MAP{'seasonal_periods': '[7, 365]'}

-- Confidence level for prediction intervals
MAP{'confidence_level': '0.95'}
```

### Return Columns

All forecasting functions return:

| Column | Description |
|--------|-------------|
| `ds` | Forecast timestamp |
| `forecast` | Point forecast value |
| `lower` | Lower prediction interval (default 90%) |
| `upper` | Upper prediction interval (default 90%) |

### API Variants

| API | Best For | Example |
|-----|----------|---------|
| `ts_forecast` | Single series | `ts_forecast('data', date, val, 'ETS', 12, MAP{})` |
| `ts_forecast_by` | Multiple series | `ts_forecast_by('data', id, date, val, 'ETS', 12, MAP{})` |
| `ts_forecast_agg` | Custom grouping | `ts_forecast_agg(date, val, 'ETS', 12, MAP{})` |
| `ts_forecast_exog` | External features | `ts_forecast_exog('data', date, val, 'x,y', 'future', 'ARIMA', 12, MAP{})` |

---

## Tips

1. **Start with Baselines** - Always compare against `Naive` or `SeasonalNaive`.

2. **Use Auto Models** - When unsure, start with `AutoETS` or `AutoARIMA`.

3. **Match Seasonality** - Ensure `seasonal_period` matches your data frequency.

4. **Check Prediction Intervals** - Wide intervals indicate high uncertainty.

5. **Intermittent Data** - Use Croston variants for sparse/lumpy demand.

6. **Model Names are Case-Sensitive** - Use exact names like `HoltWinters`, not `holtwinters`.

---

## Troubleshooting

### Q: Why are my forecasts flat?

**A:** Model fell back to `Naive`. Check model name spelling (case-sensitive).

### Q: Why are prediction intervals very wide?

**A:** High volatility or short history. Try longer training data or simpler model.

### Q: Which model should I use?

**A:** Start with `AutoETS`. It automatically selects the best ETS variant.

### Q: How do I handle multiple seasonality?

**A:** Use `MSTL` with `seasonal_periods` parameter:
```sql
MAP{'seasonal_periods': '[7, 365]'}  -- weekly + yearly
```
