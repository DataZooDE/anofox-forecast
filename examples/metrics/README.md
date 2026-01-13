# Metrics Examples

> **Measure forecast accuracy - the foundation of model selection.**

This folder contains runnable SQL examples demonstrating forecast accuracy metrics with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_metrics_examples.sql`](synthetic_metrics_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/metrics/synthetic_metrics_examples.sql
```

---

## Overview

The extension provides functions for evaluating forecast accuracy:

| Function | Full Name | Best For |
|----------|-----------|----------|
| `ts_mae` | Mean Absolute Error | General purpose |
| `ts_rmse` | Root Mean Squared Error | Penalize large errors |
| `ts_mape` | Mean Absolute Percentage Error | Scale-independent |
| `ts_smape` | Symmetric MAPE | Bounded percentage |
| `ts_mase` | Mean Absolute Scaled Error | Cross-series comparison |
| `ts_quantile_loss` | Quantile Loss | Probabilistic forecasts |
| `ts_mean_interval_width` | Mean Interval Width | Interval sharpness |

---

## Patterns Overview

### Pattern 1: Basic Point Metrics

**Use case:** Evaluate point forecast accuracy.

```sql
SELECT
    ts_mae(LIST(actual), LIST(forecast)) AS mae,
    ts_rmse(LIST(actual), LIST(forecast)) AS rmse,
    ts_mape(LIST(actual), LIST(forecast)) AS mape,
    ts_smape(LIST(actual), LIST(forecast)) AS smape
FROM my_forecasts;
```

**See:** `synthetic_metrics_examples.sql` Section 1

---

### Pattern 2: Understanding Each Metric

**Use case:** Compare metric behavior on different error patterns.

```sql
-- MAE: Mean of |actual - forecast|
-- RMSE: sqrt(Mean of (actual - forecast)^2)
-- RMSE is more sensitive to outliers
```

**See:** `synthetic_metrics_examples.sql` Section 2

---

### Pattern 3: Percentage Metrics

**Use case:** Scale-independent error measurement.

```sql
-- MAPE: Mean of |actual - forecast| / |actual| * 100
-- SMAPE: Mean of |actual - forecast| / (|actual| + |forecast|) * 200
-- SMAPE is bounded [0, 200], MAPE can exceed 100%
```

**See:** `synthetic_metrics_examples.sql` Section 3

---

### Pattern 4: Quantile and Interval Metrics

**Use case:** Evaluate probabilistic forecasts.

```sql
-- Quantile loss for 90th percentile
SELECT ts_quantile_loss(actual_array, forecast_array, 0.9);

-- Mean interval width
SELECT ts_mean_interval_width(lower_array, upper_array);
```

**See:** `synthetic_metrics_examples.sql` Section 4

---

### Pattern 5: Comparing Models

**Use case:** Evaluate multiple forecast methods.

```sql
SELECT
    'Model A' AS model,
    ts_mae(actual, forecast_a) AS mae,
    ts_rmse(actual, forecast_a) AS rmse
UNION ALL
SELECT
    'Model B' AS model,
    ts_mae(actual, forecast_b) AS mae,
    ts_rmse(actual, forecast_b) AS rmse;
```

**See:** `synthetic_metrics_examples.sql` Section 5

---

## Metric Formulas

### Point Forecast Metrics

| Metric | Formula | Range |
|--------|---------|-------|
| MAE | `mean(|y - ŷ|)` | [0, ∞) |
| RMSE | `sqrt(mean((y - ŷ)²))` | [0, ∞) |
| MAPE | `mean(|y - ŷ| / |y|) × 100` | [0, ∞) |
| SMAPE | `mean(|y - ŷ| / (|y| + |ŷ|)) × 200` | [0, 200] |

### Probabilistic Metrics

| Metric | Formula | Purpose |
|--------|---------|---------|
| Quantile Loss | `mean(max(q(y-ŷ), (q-1)(y-ŷ)))` | Penalize quantile errors |
| Interval Width | `mean(upper - lower)` | Measure sharpness |

---

## Choosing a Metric

| Situation | Recommended Metric |
|-----------|-------------------|
| General comparison | MAE or RMSE |
| Large errors are costly | RMSE |
| Scale-independent | MAPE or SMAPE |
| Small values possible | SMAPE (avoids division by zero) |
| Cross-series comparison | MASE |
| Probabilistic forecasts | Quantile Loss |

---

## Key Concepts

### MAE vs RMSE

| Aspect | MAE | RMSE |
|--------|-----|------|
| Outlier sensitivity | Low | High |
| Interpretation | Average error | Typical error |
| Units | Same as data | Same as data |

### MAPE vs SMAPE

| Aspect | MAPE | SMAPE |
|--------|------|-------|
| Bounded | No | Yes [0, 200] |
| Symmetric | No | Yes |
| Zero actuals | Undefined | Defined |

### When MAPE Fails

```sql
-- Actual is 1, forecast is 2
-- MAPE = |1-2|/|1| × 100 = 100%

-- Actual is 100, forecast is 101
-- MAPE = |100-101|/|100| × 100 = 1%

-- Same absolute error, very different MAPE
```

---

## Tips

1. **Use multiple metrics** - No single metric tells the whole story.

2. **Match metric to business** - If large errors are costly, use RMSE.

3. **Beware of MAPE** - It can be undefined or misleading for small values.

4. **Quantile loss for intervals** - Use when evaluating prediction intervals.

5. **Report in context** - An MAE of 10 means nothing without knowing the scale.
