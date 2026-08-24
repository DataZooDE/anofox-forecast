# Cross-Validation Guide

> How to properly evaluate forecast accuracy

**Use this guide to:**
- Understand why standard cross-validation doesn't work for time series
- Run one-liner backtests to quickly evaluate models
- Configure fold parameters (horizon, number of folds, frequency)
- Handle gaps between training and test sets
- Compare multiple models on the same data splits

## Why Cross-Validation Matters

A model that fits historical data well may forecast poorly. Cross-validation simulates real forecasting by:
1. Training on past data only
2. Testing on future data
3. Repeating across multiple time periods

## Quick Start: Two-Step Backtest

Backtesting is a two-step workflow: generate train/test folds, then forecast on them.

```sql
-- Step 1: Generate CV folds (both train and test rows, with actual dates)
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by(
    'sales',        -- table name
    product_id,     -- group column
    date,           -- date column
    quantity,       -- target column
    3,              -- number of folds
    7,              -- forecast horizon (7 days)
    MAP{}           -- default parameters
);

-- Step 2: Forecast on the folds (horizon inferred from test rows)
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, quantity, 'Naive', MAP{}
);
```

**`ts_cv_forecast_by` output columns:**
- `fold_id` - which CV fold
- `product_id` - series identifier (original group column name preserved)
- `date` - forecast date
- `y` - actual value
- `yhat` - point forecast
- `yhat_lower` / `yhat_upper` - prediction interval
- `model_name` - model used

Compute per-fold errors with the metric functions:

```sql
SELECT
    product_id, fold_id,
    ts_rmse(LIST(y ORDER BY date), LIST(yhat ORDER BY date)) AS rmse,
    ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date))  AS mae
FROM cv_results
GROUP BY product_id, fold_id;
```

## Understanding CV Folds

Cross-validation creates multiple train/test splits:

```
Data:    [══════════════════════════════════════]
Fold 1:  [═══════TRAIN═══════][TEST]
Fold 2:  [═════════TRAIN═════════][TEST]
Fold 3:  [════════════TRAIN════════════][TEST]
```

Each fold:
1. Uses more historical data for training
2. Tests on the next `horizon` periods
3. Measures forecast accuracy independently

## Key Parameters

### Basic Parameters

```sql
-- The forecasting model is chosen in step 2 (ts_cv_forecast_by), not in the folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by(
    'sales', product_id, date, quantity,
    5,              -- folds: 5 CV splits
    7,              -- horizon: forecast 7 periods ahead
    MAP{}
);

SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, quantity, 'AutoETS', MAP{}
);
```

### Advanced Parameters

Fold-shape parameters (`gap`, `window_type`, etc.) belong to `ts_cv_folds_by`:

```sql
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by(
    'sales', product_id, date, quantity, 5, 7,
    {
        'gap': 2,                   -- 2-period gap (simulates data latency)
        'window_type': 'fixed',     -- fixed vs expanding window
        'min_train_size': 30,       -- minimum training observations
        'initial_train_size': 60,   -- first fold training size
        'skip_length': 14           -- periods between fold starts
    }
);

-- Then forecast with the model of your choice
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, quantity, 'AutoETS', MAP{}
);
```

### Window Types

```
Expanding (default): Training grows each fold
Fold 1: [═══TRAIN═══][TEST]
Fold 2: [════TRAIN════][TEST]
Fold 3: [═════TRAIN═════][TEST]

Fixed: Training size stays constant
Fold 1:     [═TRAIN═][TEST]
Fold 2:       [═TRAIN═][TEST]
Fold 3:         [═TRAIN═][TEST]
```

Use `fixed` when:
- Recent data is more relevant than old data
- You want consistent training size
- Memory is a constraint

### Gap Parameter

The `gap` simulates real-world data latency:

```
Without gap (gap=0):
Train: [═════════] → Forecast: [TEST]
                 ↑              ↑
              day 0          day 1

With gap (gap=2):
Train: [═════════] → Forecast:    [TEST]
                 ↑                  ↑
              day 0              day 3
```

Use `gap` when:
- Your data has reporting delays
- ETL processes cause latency
- You need buffer time for decisions

## Choosing Metrics

Metrics are computed from the forecast output (`y` vs `yhat`) using the metric
functions, so you can report any metric you like from the same run:

```sql
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', id, date, val, 3, 7, MAP{});
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by('cv_folds', id, date, val, 'Naive', MAP{});

SELECT
    id, fold_id,
    ts_rmse(LIST(y ORDER BY date), LIST(yhat ORDER BY date))  AS rmse,
    ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date))   AS mae,
    ts_mape(LIST(y ORDER BY date), LIST(yhat ORDER BY date))  AS mape,
    ts_smape(LIST(y ORDER BY date), LIST(yhat ORDER BY date)) AS smape
FROM cv_results
GROUP BY id, fold_id;

-- Available metric functions: ts_rmse, ts_mae, ts_mse, ts_mape, ts_smape, ts_bias, ts_r2, ts_coverage
```

| Metric | When to Use |
|--------|------------|
| RMSE | Default; penalizes large errors |
| MAE | Robust to outliers |
| MAPE | Percentage-based comparison |
| sMAPE | Symmetric percentage |
| Coverage | Evaluate prediction intervals |

## Analyzing Results

All examples below assume `cv_results` was produced by the two-step workflow:

```sql
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', id, date, val, 5, 7, MAP{});
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by('cv_folds', id, date, val, 'Naive', MAP{});
```

### Aggregate by Model

```sql
SELECT
    model_name,
    COUNT(*) AS n_forecasts,
    ROUND(AVG(abs(y - yhat)), 2) AS avg_mae,
    ROUND(AVG(CASE WHEN y BETWEEN yhat_lower AND yhat_upper THEN 1 ELSE 0 END), 2) AS coverage
FROM cv_results
GROUP BY model_name;
```

### By Forecast Horizon

```sql
WITH results AS (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY fold_id, id ORDER BY date) AS step
    FROM cv_results
)
SELECT
    step,
    ROUND(AVG(abs(y - yhat)), 2) AS avg_mae
FROM results
GROUP BY step
ORDER BY step;
```

Typically, error increases with forecast horizon.

### By Series

```sql
SELECT
    id,
    ROUND(AVG(abs(y - yhat)), 2) AS avg_mae,
    COUNT(*) AS n_obs
FROM cv_results
GROUP BY id
ORDER BY avg_mae DESC;
```

## Model Comparison

```sql
-- Generate the folds ONCE so every model is evaluated on identical splits
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', id, date, val, 3, 7, MAP{});

-- Forecast each model on the same folds
WITH comparisons AS (
    SELECT 'AutoETS' AS model, * FROM ts_cv_forecast_by('cv_folds', id, date, val, 'AutoETS', MAP{})
    UNION ALL
    SELECT 'Theta' AS model, * FROM ts_cv_forecast_by('cv_folds', id, date, val, 'Theta', MAP{})
    UNION ALL
    SELECT 'Naive' AS model, * FROM ts_cv_forecast_by('cv_folds', id, date, val, 'Naive', MAP{})
)
SELECT
    model,
    ROUND(ts_mae(LIST(y), LIST(yhat)), 2)  AS mae,
    ROUND(ts_rmse(LIST(y), LIST(yhat)), 2) AS rmse
FROM comparisons
GROUP BY model
ORDER BY mae;
```

## Best Practices

1. **Use at least 3-5 folds** for reliable estimates
2. **Match horizon to business needs** - don't validate 7-day if you need 30-day
3. **Include a Naive baseline** - if you can't beat Naive, rethink your approach
4. **Check coverage** - prediction intervals should cover ~90% of actuals
5. **Examine errors by horizon** - later horizons are harder
6. **Consider business cost** - sometimes under-forecasting is worse than over-forecasting

## Common Pitfalls

| Pitfall | Solution |
|---------|----------|
| Too few folds | Use at least 3 folds |
| Ignoring data leakage | Use `gap` parameter |
| Single metric | Check multiple metrics |
| No baseline | Always compare to Naive |
| Overfitting to CV | Hold out final test set |

---

*See also: [Model Selection](02-model-selection.md) | [Evaluation Metrics](../api/09-evaluation-metrics.md)*
