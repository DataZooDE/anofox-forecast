# In-Sample Forecasts & Confidence Levels

## Overview

The forecasting API now returns **in-sample fitted values** and **confidence level metadata** for enhanced model diagnostics and transparency.

## New Output Fields

### 1. `insample_fitted` (DOUBLE[])

**Purpose**: Returns the model's fitted values on the training data (one-step-ahead predictions).

**Default**: Empty list (`[]`)

**When populated**: Set `return_insample: true` in parameters

**Use cases**:
- Residual analysis
- Model diagnostics
- Goodness-of-fit assessment
- Cross-validation
- Detect overfitting

**Example**:
```sql
SELECT 
    LEN(insample_fitted) AS num_fitted_values,
    insample_fitted[1:5] AS first_5_fitted
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                 {'return_insample': true, 'seasonal_period': 7});
```

### 2. `confidence_level` (DOUBLE)

**Purpose**: Shows what confidence level was used for prediction intervals.

**Default**: `0.90` (90%)

**Range**: 0.0 to 1.0 (exclusive)

**Use cases**:
- Document interval interpretation
- Verify expected coverage
- Metadata for downstream analysis

**Example**:
```sql
SELECT 
    confidence_level,
    ROUND((upper - lower), 2) AS interval_width
FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                 {'confidence_level': 0.95, 'seasonal_period': 7});
-- confidence_level = 0.95
```

---

## Parameters

### `return_insample` (BOOLEAN)

**Purpose**: Control whether to return fitted values.

**Default**: `false`

**Performance**: Minimal overhead (~1-2%) when enabled.

**Example**:
```sql
{'return_insample': true}
```

### `confidence_level` (DOUBLE)

**Purpose**: Set the confidence level for prediction intervals.

**Default**: `0.90` (90% confidence intervals)

**Valid range**: `0.0 < level < 1.0`

**Common values**:
- `0.80` - 80% CI (narrow, frequent misses)
- `0.90` - 90% CI (default, balanced)
- `0.95` - 95% CI (wide, standard)
- `0.99` - 99% CI (very wide, conservative)

**Example**:
```sql
{'confidence_level': 0.95}
```

---

## Complete Examples

### Example 1: Model Diagnostics with Residuals

```sql
-- Get fitted values and compute residuals
WITH forecast_data AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14,
                              {'return_insample': true, 'seasonal_period': 7})
),
residuals AS (
    SELECT 
        UNNEST(insample_fitted) AS fitted,
        ROW_NUMBER() OVER () AS idx
    FROM forecast_data
),
actuals AS (
    SELECT 
        amount AS actual,
        ROW_NUMBER() OVER (ORDER BY date) AS idx
    FROM sales
)
SELECT 
    r.idx AS observation,
    a.actual,
    ROUND(r.fitted, 2) AS fitted,
    ROUND(a.actual - r.fitted, 2) AS residual,
    CASE 
        WHEN ABS(a.actual - r.fitted) > 2 * STDDEV(a.actual - r.fitted) OVER ()
        THEN '⚠️ Outlier'
        ELSE '✓'
    END AS flag
FROM residuals r
JOIN actuals a ON r.idx = a.idx
ORDER BY ABS(a.actual - r.fitted) DESC
LIMIT 10;
```

### Example 2: Compare Multiple Confidence Levels

```sql
-- Show how interval width changes with confidence level
WITH ci_80 AS (
    SELECT 'CI 80%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.80, 'seasonal_period': 7})
),
ci_90 AS (
    SELECT 'CI 90%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.90, 'seasonal_period': 7})
),
ci_95 AS (
    SELECT 'CI 95%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.95, 'seasonal_period': 7})
),
ci_99 AS (
    SELECT 'CI 99%' AS level, AVG(upper - lower) AS width
    FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                     {'confidence_level': 0.99, 'seasonal_period': 7})
)
SELECT level, ROUND(width, 2) AS avg_interval_width
FROM ci_80 UNION ALL SELECT * FROM ci_90 
UNION ALL SELECT * FROM ci_95 UNION ALL SELECT * FROM ci_99
ORDER BY avg_interval_width;
```

### Example 3: Goodness-of-Fit Analysis

```sql
-- Assess model fit quality
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                              {'return_insample': true, 'seasonal_period': 7})
),
fitted_vs_actual AS (
    SELECT 
        s.amount AS actual,
        UNNEST(fc.insample_fitted) AS fitted
    FROM sales s, fc
)
SELECT 
    'Training observations: ' || COUNT(*) AS metric
FROM fitted_vs_actual
UNION ALL
SELECT 
    'R-squared: ' || ROUND(TS_R2(LIST(actual), LIST(fitted)), 4)
FROM fitted_vs_actual
UNION ALL
SELECT 
    'RMSE: ' || ROUND(TS_RMSE(LIST(actual), LIST(fitted)), 2)
FROM fitted_vs_actual
UNION ALL
SELECT 
    'MAE: ' || ROUND(TS_MAE(LIST(actual), LIST(fitted)), 2)
FROM fitted_vs_actual;
```

### Example 4: Cross-Validation with Fitted Values

```sql
-- Rolling window validation
CREATE TABLE cv_results AS
WITH windows AS (
    SELECT 
        w AS window_id,
        DATE '2023-01-01' + INTERVAL (w * 7) DAY AS train_end
    FROM generate_series(1, 10) t(w)
),
forecasts AS (
    SELECT 
        w.window_id,
        w.train_end,
        fc.*
    FROM windows w
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            (SELECT date, amount FROM sales WHERE date <= w.train_end),
            date, amount, 'AutoETS', 7,
            {'return_insample': true, 'seasonal_period': 7}
        )
    ) fc
)
SELECT 
    window_id,
    train_end,
    LEN(insample_fitted) AS train_size,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    confidence_level
FROM forecasts
GROUP BY window_id, train_end, confidence_level
ORDER BY window_id;

-- Summarize CV performance
SELECT 
    AVG(train_size) AS avg_train_size,
    STDDEV(avg_forecast) AS forecast_stability
FROM cv_results;
```

### Example 5: GROUP BY with In-Sample Analysis

```sql
-- Per-series diagnostics
WITH fc AS (
    SELECT * FROM TS_FORECAST_BY('multi_sales', product_id, date, amount, 
                                 'AutoETS', 28,
                                 {'return_insample': true, 
                                  'confidence_level': 0.95,
                                  'seasonal_period': 7})
),
fit_quality AS (
    SELECT 
        product_id,
        LEN(insample_fitted) AS n_obs,
        confidence_level,
        model_name
    FROM fc
)
SELECT 
    product_id,
    n_obs AS training_points,
    confidence_level * 100 || '%' AS conf_level,
    model_name
FROM fit_quality
WHERE n_obs >= 50  -- Only well-trained models
ORDER BY product_id;
```

### Example 6: Detect Overfitting

```sql
-- Compare in-sample vs out-of-sample performance
WITH training AS (
    SELECT * FROM sales WHERE date < DATE '2023-06-01'
),
test AS (
    SELECT * FROM sales WHERE date >= DATE '2023-06-01'
),
fc AS (
    SELECT * FROM TS_FORECAST('training', date, amount, 'AutoETS', 30,
                              {'return_insample': true, 'seasonal_period': 7})
),
insample_error AS (
    SELECT 
        TS_MAE(LIST(t.amount ORDER BY t.date), 
               LIST(UNNEST(fc.insample_fitted))) AS mae_train
    FROM training t, fc
),
outsample_error AS (
    SELECT 
        TS_MAE(LIST(test.amount ORDER BY test.date), 
               LIST(fc.point_forecast ORDER BY fc.forecast_step)) AS mae_test
    FROM test
    JOIN fc ON test.date = fc.date_col
)
SELECT 
    ROUND(i.mae_train, 2) AS mae_training,
    ROUND(o.mae_test, 2) AS mae_test,
    CASE 
        WHEN o.mae_test > i.mae_train * 1.5
        THEN '⚠️ Possible overfitting'
        WHEN o.mae_test > i.mae_train * 1.2
        THEN '⚠️ Monitor closely'
        ELSE '✓ Good generalization'
    END AS assessment
FROM insample_error i, outsample_error o;
```

---

## Model Support for Fitted Values

### ✅ Supported Models (23)

Models that return fitted values when `return_insample: true`:

- Naive, SeasonalNaive, RandomWalkWithDrift
- SESOptimized
- HoltWinters
- Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta
- SeasonalExponentialSmoothing, SeasonalESOptimized, SeasonalWindowAverage
- ETS, AutoETS
- AutoARIMA
- MFLES
- TBATS
- CrostonClassic, CrostonOptimized
- ADIDA, IMAPA, TSB

### ⚠️ Limited Support

Some models may return empty fitted values:
- SimpleMovingAverage (only provides fitted after warmup)
- Ensemble models (composite fittin)
- Some intermittent demand models

### 📝 Checking Support

```sql
-- Test if a model returns fitted values
SELECT 
    model_name,
    LEN(insample_fitted) AS fitted_count,
    CASE 
        WHEN LEN(insample_fitted) > 0 
        THEN '✅ Supported'
        ELSE '❌ Not supported'
    END AS status
FROM TS_FORECAST('sales', date, amount, 'ModelName', 7, 
                 {'return_insample': true});
```

---

## Performance Impact

| Feature | Overhead | Notes |
|---------|----------|-------|
| `return_insample: false` (default) | 0% | No overhead |
| `return_insample: true` | ~1-2% | Minimal - fitted values computed during fit() |
| `confidence_level` | 0% | No overhead, only affects interval width |

**Recommendation**: Enable `return_insample` only when needed for diagnostics.

---

## Best Practices

### 1. Model Validation
```sql
-- Always check fitted values quality before deploying
WITH fc AS (
    SELECT * FROM TS_FORECAST(..., {'return_insample': true})
)
SELECT 
    TS_R2(...) AS r_squared,
    TS_RMSE(...) AS rmse
FROM fc
WHERE TS_R2(...) > 0.7;  -- Ensure good fit
```

### 2. Confidence Level Selection
```sql
-- For production: balance precision vs recall
-- Use 90% for balanced risk
-- Use 95% for conservative estimates
-- Use 80% for aggressive forecasts
{'confidence_level': 0.90}  -- Recommended default
```

### 3. Residual Monitoring
```sql
-- Set up alerts for large residuals
CREATE VIEW forecast_alerts AS
WITH residuals AS (...)
SELECT * FROM residuals
WHERE ABS(residual) > 3 * STDDEV(residual) OVER ();
```

---

## API Summary

### Single Series
```sql
TS_FORECAST(table, date_col, value_col, method, horizon, params)

-- params:
{
    'return_insample': BOOLEAN,      -- Default: false
    'confidence_level': DOUBLE,      -- Default: 0.90
    ... other model-specific params
}
```

### Multiple Series
```sql
TS_FORECAST_BY(table, group_col, date_col, value_col, method, horizon, params)

-- Same params as above
```

### Output Schema
```
forecast_step: INT
date_col: TIMESTAMP
point_forecast: DOUBLE
lower: DOUBLE
upper: DOUBLE
model_name: VARCHAR
insample_fitted: DOUBLE[]        ← NEW
confidence_level: DOUBLE         ← NEW
```

---

## See Also

- **Model Selection**: docs/MODELS.md
- **Evaluation Metrics**: docs/50_evaluation_metrics.md (incl. TS_COVERAGE)
- **Examples**: examples/rolling_forecast.sql
- **API Reference**: 00_README.md

---

**Status**: ✅ Production-ready

Both features are fully tested and integrated across all forecast functions!

