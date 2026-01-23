# Conformal Prediction Examples

> **Distribution-free prediction intervals with guaranteed coverage.**

This folder contains runnable SQL examples demonstrating conformal prediction with the anofox-forecast extension.

## Functions

| Function | Description |
|----------|-------------|
| `ts_conformal_by` | Compute conformal prediction intervals for multiple series |
| `ts_conformal_apply_by` | Apply pre-computed conformity score to forecasts |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_conformal_examples.sql`](synthetic_conformal_examples.sql) | Multi-series conformal prediction examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
```

---

## Usage

### Basic Conformal Prediction

```sql
-- Compute prediction intervals from backtest data
-- Table needs: actual, forecast (calibration), point_forecast (to add intervals)
SELECT * FROM ts_conformal_by('backtest_data', product_id, actual, forecast, point_forecast, MAP{});
```

### Different Coverage Levels

```sql
-- 95% coverage (alpha=0.05)
SELECT * FROM ts_conformal_by('backtest_data', product_id, actual, forecast, point_forecast,
    MAP{'alpha': '0.05'});

-- 80% coverage (alpha=0.20) - narrower intervals
SELECT * FROM ts_conformal_by('backtest_data', product_id, actual, forecast, point_forecast,
    MAP{'alpha': '0.20'});
```

### Asymmetric Intervals

```sql
-- For skewed residual distributions
SELECT * FROM ts_conformal_by('backtest_data', product_id, actual, forecast, point_forecast,
    MAP{'asymmetric': 'true'});
```

### Apply Pre-Computed Score

```sql
-- Apply a known conformity score to forecasts
SELECT * FROM ts_conformal_apply_by('forecast_table', product_id, point_forecast, 15.0);
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `alpha` | VARCHAR | '0.1' | Miscoverage rate (1 - coverage). 0.1 = 90% coverage |
| `asymmetric` | VARCHAR | 'false' | Use asymmetric intervals for skewed residuals |

When `MAP{}` is passed (empty), defaults to 90% coverage with symmetric intervals.

---

## Output Columns

### ts_conformal_by

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `point` | DOUBLE[] | Point forecasts |
| `lower` | DOUBLE[] | Lower bounds |
| `upper` | DOUBLE[] | Upper bounds |
| `conformity_score` | DOUBLE | Calibrated quantile |
| `coverage` | DOUBLE | Target coverage (1 - alpha) |

### ts_conformal_apply_by

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower bound |
| `upper` | DOUBLE | Upper bound |

---

## Key Concepts

### What is Conformal Prediction?

Conformal prediction creates prediction intervals with **guaranteed coverage** without distributional assumptions.

```
Coverage Guarantee: P(Y in [lower, upper]) >= 1 - alpha
```

### The Workflow

1. **Calibrate** from backtest residuals (actual - forecast)
2. **Compute** conformity score at target quantile
3. **Apply** to new forecasts: `forecast +/- conformity_score`

### Alpha and Coverage

| Alpha | Coverage | Interval Width |
|-------|----------|----------------|
| 0.20 | 80% | Narrower |
| 0.10 | 90% | Medium |
| 0.05 | 95% | Wider |
| 0.01 | 99% | Very wide |

### Symmetric vs Asymmetric

| Method | Use When |
|--------|----------|
| **Symmetric** | Residuals are roughly symmetric |
| **Asymmetric** | Residuals are skewed (e.g., demand forecasting) |

---

## Data Requirements

The `ts_conformal_by` function expects a table with:

1. **Calibration rows**: `actual` and `forecast` are non-NULL, `point_forecast` is NULL
2. **Forecast rows**: `actual` and `forecast` are NULL, `point_forecast` is non-NULL

```sql
-- Example table structure
CREATE TABLE backtest_data AS
SELECT * FROM (
    -- Calibration data
    SELECT 'A' AS id, 100.0 AS actual, 98.0 AS forecast, NULL::DOUBLE AS point_forecast
    UNION ALL
    -- ... more calibration rows
    -- Forecast data
    SELECT 'A' AS id, NULL AS actual, NULL AS forecast, 150.0 AS point_forecast
);
```

---

## Tips

1. **More calibration data = better** - Use at least 50+ residuals for reliable scores.

2. **Match the forecast horizon** - Calibrate on residuals from the same horizon you're forecasting.

3. **Check interval widths** - Wide intervals indicate high uncertainty in your forecasts.

4. **Use asymmetric for demand** - Demand forecasts often have skewed errors.

5. **Combine with backtesting** - Use backtest results from `ts_backtest_auto_by` as calibration data.

---

## Related Functions

- `ts_backtest_auto_by()` - Generate backtest results for calibration
- `ts_forecast_by()` - Generate point forecasts to conformalize
- `ts_coverage_by()` - Evaluate interval coverage
