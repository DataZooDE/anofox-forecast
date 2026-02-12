# Changepoint Detection Reference

Detect structural breaks in time series (mean, variance, trend changes).

## ts_detect_changepoints_by

```sql
ts_detect_changepoints_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, params MAP) → TABLE
```

Params:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| hazard_lambda | DOUBLE | 250.0 | Hazard rate. Lower = more changepoints detected |

Returns (one row per data point):
| Column | Type | Description |
|--------|------|-------------|
| group_col | (input) | Series identifier (name preserved) |
| date_col | TIMESTAMP | Date (name preserved) |
| is_changepoint | BOOLEAN | Detected changepoint? |
| changepoint_probability | DOUBLE | Probability (0-1) |

Row preservation: output rows = input rows. NULL dates, groups with < 2 points, or errors get is_changepoint=false, changepoint_probability=NULL.

Alias: `ts_detect_changepoints` = `ts_detect_changepoints_by`

## Examples

```sql
-- Detect changepoints
SELECT * FROM ts_detect_changepoints_by(
    'sales', product_id, date, quantity, {'hazard_lambda': 100});

-- Filter to only changepoints
SELECT product_id, date, changepoint_probability
FROM ts_detect_changepoints_by('sales', product_id, date, quantity, {'hazard_lambda': 100})
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Count per series
SELECT product_id, COUNT(*) FILTER (WHERE is_changepoint) AS n_changepoints
FROM ts_detect_changepoints_by('sales', product_id, date, quantity, {'hazard_lambda': 100})
GROUP BY product_id;
```

## Sensitivity Tuning

| hazard_lambda | Sensitivity | Use Case |
|--------------|-------------|----------|
| 50-100 | High | Find subtle changes |
| 250 (default) | Medium | General purpose |
| 500-1000 | Low | Only major regime changes |
