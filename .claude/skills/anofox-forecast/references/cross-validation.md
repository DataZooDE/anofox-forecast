# Cross-Validation Reference

## ts_cv_folds_by

Create train/test splits for backtesting.

```sql
ts_cv_folds_by(source VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN,
    n_folds BIGINT, horizon BIGINT, params MAP/STRUCT) → TABLE
```

**Source must be a table name string, NOT a CTE.** Data must be pre-cleaned (no gaps).

Params:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| gap | BIGINT | 0 | Periods between train end and test start |
| embargo | BIGINT | 0 | Periods excluded from training after previous test |
| window_type | VARCHAR | 'expanding' | 'expanding', 'fixed', or 'sliding' |
| min_train_size | BIGINT | 1 | Min training size (fixed/sliding only) |
| initial_train_size | BIGINT | auto | Periods before first fold |
| skip_length | BIGINT | horizon | Periods between folds (1=dense) |
| clip_horizon | BOOLEAN | false | Allow partial test windows |

Returns: group_col, date_col, target_col (DOUBLE), fold_id (BIGINT), split (VARCHAR: 'train'/'test')

Output has exactly 5 columns. Features NOT passed through — use ts_cv_hydrate_by to add features.

Example:
```sql
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', store_id, date, revenue, 3, 6, MAP{});
```

## ts_cv_forecast_by

Generate univariate forecasts on pre-computed folds. **Univariate only** — no exogenous support.

```sql
ts_cv_forecast_by(ml_folds VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN,
    method VARCHAR, params MAP) → TABLE
```

Horizon is inferred from test rows. No frequency parameter needed.

Returns: fold_id, group_col, date_col, target_col (actual), split ('test'), yhat, yhat_lower, yhat_upper, model_name

Example:
```sql
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'AutoETS',
    MAP{'seasonal_period': '7'});
```

## ts_cv_hydrate_by

Add feature columns to CV folds with automatic masking (train=actual, test=filled).

```sql
ts_cv_hydrate_by(cv_folds VARCHAR, source VARCHAR, group_col COLUMN, date_col COLUMN,
    unknown_features VARCHAR[], params MAP) → TABLE
```

Params:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| strategy | VARCHAR | 'last_value' | 'last_value', 'null', or 'default' |
| fill_value | VARCHAR | '' | Value for 'default' strategy |

Features are output as direct columns — no MAP extraction needed.

Example:
```sql
SELECT store_id, date, revenue, fold_id, split, competitor_sales, actual_temp
FROM ts_cv_hydrate_by('cv_folds', 'sales', store_id, date,
    ['competitor_sales', 'actual_temp'], MAP{'strategy': 'last_value'});
```

## ts_cv_split_by

Split using explicit cutoff dates.

```sql
ts_cv_split_by(source VARCHAR, group_col IDENTIFIER, date_col IDENTIFIER, target_col IDENTIFIER,
    training_end_times DATE[], horizon BIGINT, params MAP) → TABLE
```

Returns: group_col, date_col, target_col, fold_id, split

Example:
```sql
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split_by('sales', store_id, date, revenue,
    ['2024-03-31'::DATE, '2024-06-30'::DATE, '2024-09-30'::DATE], 30, MAP{});
```

## ts_cv_split_folds_by

View fold date ranges before running full split.

```sql
ts_cv_split_folds_by(source VARCHAR, group_col VARCHAR, date_col VARCHAR,
    training_end_times DATE[], horizon BIGINT, frequency VARCHAR) → TABLE
```

Returns: fold_id, train_start, train_end, test_start, test_end, horizon

## ts_cv_split_index_by

Memory-efficient: returns only index columns (no target values).

```sql
ts_cv_split_index_by(source VARCHAR, group_col IDENTIFIER, date_col IDENTIFIER,
    training_end_times DATE[], horizon BIGINT, frequency VARCHAR, params MAP) → TABLE
```

Returns: group_col, date_col, fold_id, split

## Standard Workflow

```sql
-- 1. Create folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y, 3, 12, MAP{});

-- 2. Forecast
CREATE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive', MAP{});

-- 3. Metrics (use scalar functions with GROUP BY)
SELECT unique_id, fold_id,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae
FROM cv_forecasts GROUP BY unique_id, fold_id;
```

## Key Notes
- Source must be a table name string, not a CTE
- Data must be pre-cleaned (use ts_fill_gaps_by first if needed)
- Uses position-based fold assignment (works with all frequencies including monthly/quarterly)
- Unknown parameter names throw informative errors
- Horizon is auto-inferred in ts_cv_forecast_by
