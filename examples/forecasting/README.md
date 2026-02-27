# Forecasting Examples

> **Forecasting is the science of making informed guesses about the future.**

This folder contains runnable SQL examples demonstrating time series forecasting with the anofox-forecast extension.

## Functions

| Function | Description |
|----------|-------------|
| `ts_forecast_by` | Forecast multiple series with any model |
| `ts_forecast_exog_by` | Forecast with exogenous variables |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_forecasting_examples.sql`](synthetic_forecasting_examples.sql) | Multi-series forecasting examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/forecasting/synthetic_forecasting_examples.sql
```

---

## Available Models (32 Total)

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

## Usage

### Basic Multi-Series Forecasting

```sql
-- Forecast all products with ETS model
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'ETS', 12, '1d', MAP{});

-- Forecast with automatic model selection
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 12, '1d', MAP{});
```

### Seasonal Models

```sql
-- Holt-Winters with weekly seasonality
SELECT * FROM ts_forecast_by('sales', product_id, date, value,
    'HoltWinters', 14, '1d',
    MAP{'seasonal_period': '7'});

-- MSTL with multiple seasonalities (daily + weekly)
SELECT * FROM ts_forecast_by('hourly_data', sensor_id, timestamp, reading,
    'MSTL', 168, '1h',
    MAP{'seasonal_periods': '[24, 168]'});
```

### Intermittent Demand

```sql
-- Croston method for sparse demand
SELECT * FROM ts_forecast_by('spare_parts', sku, date, demand, 'CrostonSBA', 14, '1d', MAP{});
```

### Exogenous Variables

```sql
-- Forecast with external features
SELECT * FROM ts_forecast_exog_by(
    'sales', store_id, date, amount,
    ['temperature', 'promotion'],
    'future_features', date,
    ['temperature', 'promotion'],
    'AutoARIMA', 7, MAP{}, '1d'
);
```

---

## Parameters

Common parameters passed via `MAP{}`:

| Parameter | Type | Description |
|-----------|------|-------------|
| `seasonal_period` | VARCHAR | Single period, e.g., `'7'` |
| `seasonal_periods` | VARCHAR | Multiple periods, e.g., `'[24, 168]'` |
| `confidence_level` | VARCHAR | Prediction interval width, e.g., `'0.95'` |
| `alpha` | VARCHAR | Smoothing parameter for level |
| `beta` | VARCHAR | Smoothing parameter for trend |
| `gamma` | VARCHAR | Smoothing parameter for seasonality |

When `MAP{}` is passed (empty), uses model defaults.

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast value |
| `lo_90` | DOUBLE | Lower 90% prediction interval |
| `hi_90` | DOUBLE | Upper 90% prediction interval |

---

## Key Concepts

### Model Selection Guide

| Situation | Recommended Model |
|-----------|-------------------|
| Unknown pattern | `AutoETS` or `AutoARIMA` |
| Baseline comparison | `Naive` or `SeasonalNaive` |
| Strong trend + seasonality | `HoltWinters` or `ETS` |
| Multiple seasonalities | `MSTL` with `seasonal_periods` |
| Sparse/intermittent demand | `CrostonSBA` or `ADIDA` |
| External factors matter | `ts_forecast_exog_by` with `AutoARIMA` |

### Prediction Intervals

- Default interval is 90% (5th to 95th percentile)
- Wide intervals indicate high uncertainty
- Use `confidence_level` parameter to adjust

---

## Tips

1. **Start with Auto Models** - When unsure, use `AutoETS` or `AutoARIMA`.

2. **Compare to Baselines** - Always test against `Naive` or `SeasonalNaive`.

3. **Match Seasonality** - Set `seasonal_period` to match your data frequency.

4. **Check Prediction Intervals** - Wide intervals indicate high uncertainty.

5. **Intermittent Data** - Use Croston variants for sparse/lumpy demand.

6. **Model Names are Case-Sensitive** - Use exact names like `HoltWinters`, not `holtwinters`.

---

## Related Functions

- `ts_backtest_auto_by()` - Evaluate forecast accuracy
- `ts_mstl_decomposition_by()` - Decompose series before forecasting
- `ts_detect_periods_by()` - Find seasonal periods automatically
- `ts_conformal_by()` - Add prediction intervals with guaranteed coverage
