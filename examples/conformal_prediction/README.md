# Conformal Prediction Examples

> **Distribution-free prediction intervals with guaranteed coverage.**

This folder contains runnable SQL examples demonstrating conformal prediction with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_conformal_examples.sql`](synthetic_conformal_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
```

---

## Overview

The extension provides functions for conformal prediction:

| Function | Type | Purpose |
|----------|------|---------|
| `ts_conformal_predict` | Scalar | Compute intervals from residuals |
| `ts_conformal_predict_asymmetric` | Scalar | Asymmetric intervals |
| `ts_conformal_quantile` | Scalar | Compute conformity score |
| `ts_conformal_intervals` | Scalar | Apply score to forecasts |
| `ts_conformal_calibrate` | Table Macro | Calibrate from backtest |
| `ts_conformal_apply` | Table Macro | Apply to new forecasts |
| `ts_conformal` | Table Macro | End-to-end workflow |

---

## Patterns Overview

### Pattern 1: Quick Start (Scalar Functions)

**Use case:** Create prediction intervals from residuals.

```sql
SELECT ts_conformal_predict(
    calibration_residuals,  -- DOUBLE[]
    point_forecasts,        -- DOUBLE[]
    alpha                   -- DOUBLE (miscoverage rate)
) AS result;
```

**See:** `synthetic_conformal_examples.sql` Section 1

---

### Pattern 2: Compute Conformity Score

**Use case:** Calibrate conformity score from historical residuals.

```sql
SELECT ts_conformal_quantile(
    LIST(actual - forecast),  -- residuals
    0.1                       -- alpha for 90% coverage
) AS conformity_score;
```

**See:** `synthetic_conformal_examples.sql` Section 2

---

### Pattern 3: Full Calibration Workflow

**Use case:** Calibrate from backtest results table.

```sql
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',  -- table name
    actual_col,          -- actual values column
    forecast_col,        -- forecast values column
    MAP{'alpha': '0.1'}  -- 90% coverage
);
```

**See:** `synthetic_conformal_examples.sql` Section 3

---

### Pattern 4: Apply to New Forecasts

**Use case:** Apply calibrated intervals to new point forecasts.

```sql
SELECT * FROM ts_conformal_apply(
    'forecast_results',  -- table with forecasts
    group_col,           -- series identifier
    forecast_col,        -- point forecasts
    conformity_score     -- from calibration
);
```

**See:** `synthetic_conformal_examples.sql` Section 4

---

### Pattern 5: Asymmetric Intervals

**Use case:** Handle skewed residual distributions.

```sql
-- Symmetric (equal-tailed)
SELECT ts_conformal_predict(residuals, forecasts, alpha);

-- Asymmetric (separate upper/lower quantiles)
SELECT ts_conformal_predict_asymmetric(residuals, forecasts, alpha);
```

**See:** `synthetic_conformal_examples.sql` Section 5

---

## Output Structures

### ts_conformal_predict

| Field | Type | Description |
|-------|------|-------------|
| `point` | `DOUBLE[]` | Point forecasts |
| `lower` | `DOUBLE[]` | Lower bounds |
| `upper` | `DOUBLE[]` | Upper bounds |
| `coverage` | `DOUBLE` | Target coverage (1 - alpha) |
| `conformity_score` | `DOUBLE` | Calibrated quantile |
| `method` | `VARCHAR` | Method used |

### ts_conformal_calibrate

| Field | Type | Description |
|-------|------|-------------|
| `conformity_score` | `DOUBLE` | Calibrated score |
| `coverage` | `DOUBLE` | Target coverage |
| `n_residuals` | `BIGINT` | Number of calibration points |

---

## Key Concepts

### What is Conformal Prediction?

Conformal prediction creates prediction intervals with **guaranteed coverage** without distributional assumptions.

```
Coverage Guarantee: P(Y ∈ [lower, upper]) ≥ 1 - α
```

### The Workflow

1. **Generate residuals** from backtest/validation
2. **Calibrate** conformity score using `ts_conformal_quantile`
3. **Apply** to new forecasts: `forecast ± conformity_score`

### Alpha and Coverage

| Alpha | Coverage | Meaning |
|-------|----------|---------|
| 0.10 | 90% | Wide intervals |
| 0.05 | 95% | Wider intervals |
| 0.01 | 99% | Very wide intervals |

### Symmetric vs Asymmetric

| Method | Use When |
|--------|----------|
| **Symmetric** | Residuals are roughly symmetric |
| **Asymmetric** | Residuals are skewed |

---

## Tips

1. **More calibration data = better** - Use at least 50+ residuals.

2. **Match the forecast horizon** - Calibrate on residuals from the same horizon.

3. **Check coverage empirically** - Verify intervals achieve target coverage.

4. **Use asymmetric for demand** - Demand forecasts often have skewed errors.

5. **Combine with forecasting** - Use backtest results from `ts_cv_forecast_by`.
