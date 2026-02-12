# Conformal Prediction Reference

Distribution-free prediction intervals with guaranteed coverage.

## Three Approaches

| Approach | Functions | Use When |
|----------|-----------|----------|
| One-Step | ts_conformal_by | Have backtest results, want intervals immediately |
| Modular | ts_conformal_calibrate + ts_conformal_apply_by | Reuse calibration across forecasts |
| Array-Based | ts_conformal_predict, ts_conformal_quantile, etc. | Custom pipelines, array data |

## Table Macros

### ts_conformal_by (One-Step)
```sql
ts_conformal_by(backtest_results VARCHAR, group_col COLUMN, actual_col COLUMN, forecast_col COLUMN, point_forecast_col COLUMN, params STRUCT) → TABLE
```

Params:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| alpha | DOUBLE | 0.1 | Miscoverage rate (0.1=90%, 0.05=95%) |
| method | VARCHAR | 'split' | 'split' (symmetric) or 'asymmetric' |

Use 'asymmetric' when residuals are skewed (e.g., demand forecasts under-predict).

### ts_conformal_calibrate
```sql
ts_conformal_calibrate(backtest_results VARCHAR, actual_col COLUMN, forecast_col COLUMN, params STRUCT) → TABLE
```
Params: alpha (DOUBLE, def: 0.1)

Returns: conformity_score (DOUBLE), coverage (DOUBLE), n_residuals (INTEGER)

### ts_conformal_apply_by
```sql
ts_conformal_apply_by(forecast_results VARCHAR, group_col COLUMN, forecast_col COLUMN, conformity_score DOUBLE) → TABLE
```

### ts_interval_width_by
```sql
ts_interval_width_by(results VARCHAR, group_col COLUMN, lower_col COLUMN, upper_col COLUMN) → TABLE
```
Returns: group_col, mean_width, n_intervals

## Scalar Functions

### ts_conformal_quantile
```sql
ts_conformal_quantile(residuals DOUBLE[], alpha DOUBLE) → DOUBLE
```

### ts_conformal_intervals
```sql
ts_conformal_intervals(forecasts DOUBLE[], conformity_score DOUBLE) → STRUCT(lower DOUBLE[], upper DOUBLE[])
```

### ts_conformal_predict
```sql
ts_conformal_predict(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE) → STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE, conformity_score DOUBLE, method VARCHAR)
```

### ts_conformal_predict_asymmetric
Same signature and return as ts_conformal_predict but uses separate upper/lower quantiles.

### ts_conformal_coverage
```sql
ts_conformal_coverage(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

### ts_conformal_evaluate
```sql
ts_conformal_evaluate(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[], alpha DOUBLE) → STRUCT(coverage DOUBLE, violation_rate DOUBLE, mean_width DOUBLE, winkler_score DOUBLE, n_observations INTEGER)
```

### ts_mean_interval_width
```sql
ts_mean_interval_width(lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

### ts_conformal_learn
```sql
ts_conformal_learn(residuals DOUBLE[], alphas DOUBLE[], method VARCHAR, strategy VARCHAR) → STRUCT(method, strategy, alphas, state_vector, scores_lower, scores_upper)
```
Methods: 'symmetric', 'asymmetric'. Strategies: 'naive', 'cv', 'adaptive'.

## Workflow Example

```sql
-- 1. Calibrate from backtest
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate('backtest', actual, forecast, {'alpha': 0.1});

-- 2. Generate forecasts
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'AutoETS', 14, MAP{'seasonal_period': '7'});

-- 3. Apply intervals
SELECT * FROM ts_conformal_apply_by('forecasts', product_id, yhat,
    (SELECT conformity_score FROM calibration));
```
