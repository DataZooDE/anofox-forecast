---
name: anofox-forecast-data-prep
description: >
  Data preparation for the anofox_forecast DuckDB extension —
  filling gaps, imputing nulls, dropping bad series, differencing,
  detrending, hierarchical key operations. Use when preparing raw time
  series for downstream forecasting or backtesting with `ts_forecast_by`
  / `ts_cv_folds_by`.
version: 2026.09.26
user-invocable: false
---

# Anofox Forecast — Data Preparation Cheat Sheet

**Extension:** `anofox_forecast` v2026.09.26 (Rust crate `anofox-forecast` v0.16.2) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*` (identical)

Prep raw time series so downstream forecasting / CV has clean input: no gaps, no unwanted NULLs, no degenerate series, correct hierarchy.

## Critical gotcha — materialise between `_by` steps

`_by` table functions **do not chain in CTEs** — under parallel execution they silently return 0 rows. Always `CREATE TABLE` between pipeline steps.

```sql
-- BROKEN (silent 0 rows under parallelism) -- `raw` is (product_id, ds, y):
WITH step1 AS (SELECT * FROM ts_fill_gaps_by('raw', product_id, ds, y, '1d'))
SELECT * FROM ts_fill_nulls_const_by('step1', product_id, ds, y, 0.0);
```

```sql
-- CORRECT:
CREATE OR REPLACE TABLE step1 AS SELECT * FROM ts_fill_gaps_by('raw', product_id, ds, y, '1d');
CREATE OR REPLACE TABLE step2 AS SELECT * FROM ts_fill_nulls_const_by('step1', product_id, ds, y, 0.0);
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

**Month-end rule (v0.10.0, D-09):** monthly/quarterly/yearly gap fill preserves
calendar alignment — a month-end series stays month-end (`2024-01-31` → inserted
`2024-02-29` → `2024-03-31`, not the 1st), a 1st-of-month series stays on the 1st, and
any other day-of-month keeps its own day (clamped to the target month's length). Same
rule `ts_fill_forward_by` uses when forward-filling with a calendar frequency.

```sql
CREATE TABLE gaps_filled AS
SELECT * FROM ts_fill_gaps_by('raw', product_id, ds, y, '1d');

-- Month-end series: the inserted row lands on 2024-02-29, not 2024-02-01
CREATE TABLE month_end_raw AS
SELECT * FROM (VALUES
    (1, TIMESTAMP '2024-01-31', 10.0),
    (1, TIMESTAMP '2024-03-31', 30.0)
) t(product_id, ds, y);

CREATE TABLE monthly_filled AS
SELECT * FROM ts_fill_gaps_by('month_end_raw', product_id, ds, y, '1mo');
```

## NULL imputation

Table macros. Signatures:

| Function | Signature |
|---|---|
| `ts_fill_nulls_forward_by(source, group_col, date_col, value_col)` | Last observation carried forward (LOCF) — **one-directional**: a leading NULL run stays NULL |
| `ts_fill_nulls_backward_by(source, group_col, date_col, value_col)` | Next observation carried backward (NOCB) — mirrors forward: a trailing NULL run stays NULL |
| `ts_fill_nulls_mean_by(source, group_col, date_col, value_col)` | Per-group mean, computed in Rust (NaN counts as missing too; all-missing group stays NULL) |
| `ts_fill_nulls_const_by(source, group_col, date_col, value_col, fill_value)` | Constant (e.g. 0.0 for retail counts) |
| `ts_fill_forward_by(...)` | Streaming per-group forward fill; also `ts_fill_forward_operator` for windowed variants |
| `ts_mark_unknown_by` | Marks values matching a sentinel as unknown |
| `ts_fill_unknown_by` | Fills previously-marked unknowns |

**There is no SQL linear-interpolation function (D-14, G26).** `ts_fill_nulls_forward_by`/
`ts_fill_nulls_backward_by` fill in ONE direction only — chain both (forward then backward, or
vice versa) if you need every NULL filled regardless of position. Internally, where the
extension interpolates (historical exogenous regressors, the panel forecasting grid), interior
gaps are linear but leading/trailing gaps take the NEAREST observed value (constant edge
extension, never extrapolated) — this is documented, unchanged behaviour, not a bug.

```sql
-- Retail: zero-fill absent-day rows produced by ts_fill_gaps_by
CREATE TABLE nulls_filled AS
SELECT * FROM ts_fill_nulls_const_by('gaps_filled', product_id, ds, y, 0.0);
```

## Series filtering — drop degenerate cases

| Function | Drops when |
|---|---|
| `ts_drop_constant_by` | Rust `is_constant`, SCALE-RELATIVE (D-13, G25): `max - min <= 1e-10 * max(1, max\|x\|)` over present values, or **fewer than two present values (incl. all-NULL groups)** — e.g. `[1e12, 1e12 + 0.001]` IS constant at that scale |
| `ts_drop_short_by(source, group_col, min_length)` | Series length < min |
| `ts_drop_gappy_by(source, group_col, value_col, max_gap_ratio)` | Gap ratio exceeds threshold (0.1 = 10 %) |
| `ts_drop_zeros_by` | All zero values |
| `ts_drop_leading_zeros_by` | Trim leading zeros (product not yet launched) |
| `ts_drop_trailing_zeros_by` | Trim trailing zeros (product discontinued) |
| `ts_drop_edge_zeros_by` | Trim both leading and trailing zeros |

```sql
-- Named distinctly from the global `clean` fixture used elsewhere in this skill.
CREATE OR REPLACE TABLE short_filtered AS
SELECT * FROM ts_drop_short_by('nulls_filled', product_id, 24);   -- ≥ 2 years monthly
```

## Transforms

- **`ts_diff_by(source, group_col, date_col, value_col, diff_order INTEGER, seasonal_lag := 1)`**
  — d-th order differencing in Rust (numpy `diff(n=diff_order)`); `diff_order=2` is the true
  second difference `y[t] - 2*y[t-1] + y[t-2]`. Pass `seasonal_lag := k` for the old lag-k
  reading `y[t] - y[t-k]` (breaking change from pre-Phase-12: `diff_order` used to mean lag-k).
  One row per input row; first `diff_order * seasonal_lag` rows per group are NULL; any
  difference touching a NULL/NaN input is NULL (never an error).
- **`ts_detrend_by(source, group_col, date_col, value_col, method)`** — removes a fitted trend
  per group. `method`: `'linear'`; `'quadratic'` (alias `'polynomial'`, `'poly'`, degree 2);
  `'cubic'` (degree 3); `'diff'`/`'diff2'`; `'loess'` (alias `'lowess'`, `O(n^2)`); `'auto'`
  (default, AIC-selects among the above). An unrecognised method name is a SQL error naming
  every valid choice, not a silent fallback.
- **`ts_validate_timestamps_by`** — check for out-of-order / duplicate timestamps.

```sql
CREATE TABLE stationary AS
SELECT * FROM ts_diff_by('clean', product_id, ds, y, 1);   -- first-order difference (same as lag-1)
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
-- Setup: raw_hierarchy(ds, y, region_id, store_id, item_id) -- ts_combine_keys /
-- ts_aggregate_hierarchy take a subquery source (date_col, value_col, id_cols...)
-- and a params MAP, not bare positional group columns.
CREATE OR REPLACE TABLE raw_hierarchy AS
SELECT
    (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
    10.0 + (i % 7) AS y,
    'R1' AS region_id, 'STORE01' AS store_id, 'SKU01' AS item_id
FROM range(0, 60) t(i);

-- Combine two IDs for grouping
CREATE OR REPLACE TABLE with_combined AS
SELECT * FROM ts_combine_keys(
    (SELECT ds, y, region_id, store_id FROM raw_hierarchy),
    MAP{'separator': '|'}
);

-- Aggregate to store level
CREATE OR REPLACE TABLE store_level AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT ds, y, region_id, store_id, item_id FROM raw_hierarchy),
    MAP{}
);
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

Both work everywhere for functions that accept a trailing params argument. STRUCT is
recommended — keeps numeric params typed. Note `ts_fill_gaps_by` itself is a **5-argument**
macro `(source, group_col, date_col, value_col, frequency)` — it takes no `params`/`tolerance`
argument at all; passing a 6th argument raises a Binder Error:

```sql
-- Correct 5-arg call (no params argument exists for this macro)
SELECT * FROM ts_fill_gaps_by('clean', product_id, ds, y, '1d');

-- Functions that DO take a trailing params argument accept either form:
-- STRUCT (recommended)
SELECT * FROM ts_forecast_by('clean', product_id, ds, y, 'AutoETS', 7, '1d', {seasonal_period: 7});
-- MAP (all strings)
SELECT * FROM ts_forecast_by('clean', product_id, ds, y, 'AutoETS', 7, '1d', MAP{'seasonal_period': '7'});
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
