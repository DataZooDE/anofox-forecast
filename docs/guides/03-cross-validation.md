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

## Quick Start: One-Liner Backtest

For most use cases, `ts_backtest_auto` does everything in one call:

```sql
SELECT * FROM ts_backtest_auto(
    'sales',        -- table name
    product_id,     -- group column
    date,           -- date column
    quantity,       -- target column
    7,              -- forecast horizon (7 days)
    3,              -- number of folds
    '1d',           -- data frequency
    MAP{}           -- default parameters
);
```

**Output columns:**
- `fold_id` - which CV fold
- `group_col` - series identifier
- `date` - forecast date
- `forecast` / `actual` - predicted vs actual
- `error` / `abs_error` - error metrics
- `fold_metric_score` - RMSE for the fold

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
SELECT * FROM ts_backtest_auto(
    'sales', product_id, date, quantity,
    7,              -- horizon: forecast 7 periods ahead
    5,              -- folds: 5 CV splits
    '1d',           -- frequency: daily data
    {'method': 'AutoETS'}
);
```

### Advanced Parameters

```sql
SELECT * FROM ts_backtest_auto(
    'sales', product_id, date, quantity, 7, 5, '1d',
    {
        'method': 'AutoETS',        -- forecasting model
        'gap': 2,                   -- 2-period gap (simulates data latency)
        'window_type': 'fixed',     -- fixed vs expanding window
        'min_train_size': 30,       -- minimum training observations
        'initial_train_size': 60,   -- first fold training size
        'skip_length': 14           -- periods between fold starts
    }
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

```sql
-- Default metric is RMSE
SELECT * FROM ts_backtest_auto('sales', id, date, val, 7, 3, '1d', MAP{});

-- Use MAE instead
SELECT * FROM ts_backtest_auto('sales', id, date, val, 7, 3, '1d', MAP{},
    NULL, 'mae');

-- Available metrics: rmse, mae, mse, mape, smape, bias, r2, coverage
```

| Metric | When to Use |
|--------|------------|
| RMSE | Default; penalizes large errors |
| MAE | Robust to outliers |
| MAPE | Percentage-based comparison |
| sMAPE | Symmetric percentage |
| Coverage | Evaluate prediction intervals |

## Analyzing Results

### Aggregate by Model

```sql
SELECT
    model_name,
    COUNT(*) AS n_forecasts,
    ROUND(AVG(abs_error), 2) AS avg_mae,
    ROUND(AVG(fold_metric_score), 2) AS avg_rmse,
    ROUND(AVG(CASE WHEN actual BETWEEN yhat_lower AND yhat_upper THEN 1 ELSE 0 END), 2) AS coverage
FROM ts_backtest_auto('sales', id, date, val, 7, 5, '1d', MAP{})
GROUP BY model_name;
```

### By Forecast Horizon

```sql
WITH results AS (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY fold_id, group_col ORDER BY date) AS step
    FROM ts_backtest_auto('sales', id, date, val, 7, 3, '1d', MAP{})
)
SELECT
    step,
    ROUND(AVG(abs_error), 2) AS avg_mae
FROM results
GROUP BY step
ORDER BY step;
```

Typically, error increases with forecast horizon.

### By Series

```sql
SELECT
    group_col,
    ROUND(AVG(abs_error), 2) AS avg_mae,
    COUNT(*) AS n_obs
FROM ts_backtest_auto('sales', id, date, val, 7, 3, '1d', MAP{})
GROUP BY group_col
ORDER BY avg_mae DESC;
```

## Model Comparison

```sql
-- Compare multiple models
WITH comparisons AS (
    SELECT 'AutoETS' AS model, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'AutoETS'})
    UNION ALL
    SELECT 'Theta' AS model, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'Theta'})
    UNION ALL
    SELECT 'Naive' AS model, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'Naive'})
)
SELECT
    model,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(AVG(fold_metric_score), 2) AS rmse
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

*See also: [Model Selection](02-model-selection.md) | [Evaluation Metrics](../api/07-evaluation-metrics.md)*
