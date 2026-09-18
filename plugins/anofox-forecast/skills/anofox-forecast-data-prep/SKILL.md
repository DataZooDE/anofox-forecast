---
name: anofox-forecast-data-prep
description: >
  Data preparation for the anofox_forecast DuckDB extension —
  filling gaps, imputing nulls, dropping bad series, differencing,
  detrending, hierarchical key operations. Use when preparing raw time
  series for downstream forecasting or backtesting with `ts_forecast_by`
  / `ts_cv_folds_by`.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — Data Preparation Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*` (identical)

Prep raw time series so downstream forecasting / CV has clean input: no gaps, no unwanted NULLs, no degenerate series, correct hierarchy.

## Critical gotcha — materialise between `_by` steps

`_by` table functions **do not chain in CTEs** — under parallel execution they silently return 0 rows. Always `CREATE TABLE` between pipeline steps.

```sql
-- BROKEN (silent 0 rows under parallelism):
WITH step1 AS (SELECT * FROM ts_fill_gaps_by('raw', id, ds, y, '1d', MAP{}))
SELECT * FROM ts_fill_nulls_const_by('step1', id, ds, y, 0.0);

-- CORRECT:
CREATE TABLE step1 AS SELECT * FROM ts_fill_gaps_by('raw', id, ds, y, '1d', MAP{});
CREATE TABLE step2 AS SELECT * FROM ts_fill_nulls_const_by('step1', id, ds, y, 0.0);
```

## Gap filling

### `ts_fill_gaps_by`
Insert missing date rows (NULL value) so every series has a complete regular grid.

```sql
ts_fill_gaps_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN,
                frequency VARCHAR) → TABLE
```

No params — pure frequency-driven grid completion. Inserted rows have NULL in `value_col`; use `ts_fill_nulls_*_by` next to impute.

Frequencies: `'1d'`, `'1h'`, `'30m'`, `'1w'`, `'1mo'`, `'1q'`, `'1y'` (Polars-style) or `'1 day'` (DuckDB INTERVAL) or raw int (days).

```sql
CREATE TABLE gaps_filled AS
SELECT * FROM ts_fill_gaps_by('raw', product_id, ds, y, '1d');
```

## NULL imputation

Table macros. Signatures:

| Function | Signature |
|---|---|
| `ts_fill_nulls_forward_by(source, group_col, date_col, value_col)` | Last observation carried forward |
| `ts_fill_nulls_backward_by(source, group_col, date_col, value_col)` | Next observation carried backward |
| `ts_fill_nulls_mean_by(source, group_col, date_col, value_col)` | Per-group mean (skips NULLs) |
| `ts_fill_nulls_const_by(source, group_col, date_col, value_col, fill_value)` | Constant (e.g. 0.0 for retail counts) |
| `ts_fill_forward_by(...)` | Streaming per-group forward fill; also `ts_fill_forward_operator` for windowed variants |
| `ts_mark_unknown_by` | Marks values matching a sentinel as unknown |
| `ts_fill_unknown_by` | Fills previously-marked unknowns |

```sql
-- Retail: zero-fill absent-day rows produced by ts_fill_gaps_by
CREATE TABLE nulls_filled AS
SELECT * FROM ts_fill_nulls_const_by('gaps_filled', product_id, ds, y, 0.0);
```

## Series filtering — drop degenerate cases

| Function | Drops when |
|---|---|
| `ts_drop_constant_by` | All values equal (no signal to forecast) |
| `ts_drop_short_by(source, group_col, min_length)` | Series length < min |
| `ts_drop_gappy_by(source, group_col, value_col, max_gap_ratio)` | Gap ratio exceeds threshold (0.1 = 10 %) |
| `ts_drop_zeros_by` | All zero values |
| `ts_drop_leading_zeros_by` | Trim leading zeros (product not yet launched) |
| `ts_drop_trailing_zeros_by` | Trim trailing zeros (product discontinued) |
| `ts_drop_edge_zeros_by` | Trim both leading and trailing zeros |

```sql
CREATE TABLE clean AS
SELECT * FROM ts_drop_short_by('nulls_filled', product_id, 24);   -- ≥ 2 years monthly
```

## Transforms

- **`ts_diff_by(source, group_col, date_col, value_col, diff_order INTEGER)`** — first / second differencing.
- **`ts_detrend_by(source, group_col, date_col, value_col, method)`** — remove linear or polynomial trend.
- **`ts_validate_timestamps_by`** — check for out-of-order / duplicate timestamps.

```sql
CREATE TABLE stationary AS
SELECT * FROM ts_diff_by('clean', product_id, ds, y, 1);   -- first-difference
```

## Hierarchical key operations

Retail and IoT panels usually have composite keys (region × store × item). These macros manipulate them:

| Function | Purpose |
|---|---|
| `ts_combine_keys` | Concatenate multiple ID columns into one, `sep`-delimited |
| `ts_split_keys` | Reverse — split back into components |
| `ts_aggregate_hierarchy` | Sum-aggregate up a hierarchy level (region+store+item → region+store) |
| `ts_validate_separator` | Verify separator string isn't present in the source columns |

```sql
-- Combine two IDs for grouping
CREATE TABLE with_combined AS
SELECT * FROM ts_combine_keys('raw', region_id, store_id, ds, y, MAP{'sep': '|'});

-- Aggregate to store level
CREATE TABLE store_level AS
SELECT * FROM ts_aggregate_hierarchy('raw', region_id, store_id, item_id, ds, y, 2);
```

## Canonical prep pipeline

```sql
-- 1. Regularise the time grid
CREATE OR REPLACE TABLE p1 AS
SELECT * FROM ts_fill_gaps_by('raw', product_id, ds, y, '1d');

-- 2. Impute (retail → zero; continuous → forward)
CREATE OR REPLACE TABLE p2 AS
SELECT * FROM ts_fill_nulls_const_by('p1', product_id, ds, y, 0.0);

-- 3. Drop what can't be forecast
CREATE OR REPLACE TABLE p3 AS
SELECT * FROM ts_drop_short_by('p2', product_id, 30);

CREATE OR REPLACE TABLE clean AS
SELECT * FROM ts_drop_constant_by('p3', product_id, y);
```

## STRUCT vs MAP params

Both work everywhere. STRUCT is recommended — keeps numeric params typed:

```sql
-- STRUCT (recommended)
ts_fill_gaps_by('raw', id, ds, y, '1d', {tolerance: 0.1})

-- MAP (all strings)
ts_fill_gaps_by('raw', id, ds, y, '1d', MAP{'tolerance': '0.1'})
```

Empty params: `MAP{}` or `{}`.

## Common patterns

- **Kaggle-style split**: prep on train + test together, then split at the cutoff. Prevents inconsistent gap-fill / drop decisions between splits.
- **CV-ready input**: `ts_cv_folds_by` requires the input to already be gap-filled and NULL-imputed. Always run the prep pipeline first.
- **JSON extension required** for a subset of prep-related functions (period detection auto-loading, etc.). Enable `SET autoinstall_known_extensions=1; SET autoload_known_extensions=1;` in your session.

See also: `anofox-forecast-eda` (quality check before prep), `anofox-forecast-detection` (fill gaps before detecting periods), `anofox-forecast-backtest` (cv_folds requires clean input), `anofox-forecast-models` (forecast requires clean input).

Reference docs:
- `docs/api/04-data-preparation.md`
- `docs/guides/01-getting-started.md`
