---
name: anofox-forecast-eda
description: >
  Exploratory data analysis, data quality, and statistical diagnostics for
  the anofox_forecast DuckDB extension — 36 per-series statistics,
  data-quality scoring, quality-report summaries, 117 tsfresh-inspired
  feature extraction, and 7 diagnostic functions covering stationarity
  (ADF, KPSS, combined verdict) and residual adequacy (Ljung-Box,
  Durbin-Watson, Jarque-Bera, combined report). Use before forecasting to
  understand series characteristics (length, gaps, trend, seasonality
  strength, intermittency) or to validate model residuals after fitting.
version: 2026.09.26
user-invocable: false
---

# Anofox Forecast — EDA & Data Quality Cheat Sheet

**Extension:** `anofox_forecast` v2026.09.26 (Rust crate `anofox-forecast` v0.16.2) | **DuckDB:** v1.5.6 / v2.0 | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Understand your data before modelling it: per-series statistics, data-quality scores, feature vectors for ML.

## Statistics

### `ts_stats` (table function) — 36 metrics per series

```sql
ts_stats(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN,
         frequency VARCHAR, seasonal_period := 0) → TABLE
```

Returns 36 columns: `length`, `n_nulls`, `n_nan`, `n_zeros`, `n_positive`, `n_negative`, `n_unique_values`, `is_constant`, `n_zeros_start`, `n_zeros_end`, `plateau_size`, `plateau_size_nonzero`, `mean`, `median`, `std_dev`, `variance`, `min`, `max`, `range`, `sum`, `skewness`, `kurtosis`, `tail_index`, `bimodality_coef`, `trimmed_mean`, `coef_variation`, `q1`, `q3`, `iqr`, `autocorr_lag1`, `trend_strength`, `seasonality_strength`, `entropy`, `stability`, plus date-derived `expected_length`, `n_gaps`, and `n_gap_runs`.

**v2026.10.16:** `skewness`/`kurtosis` are now textbook scipy `bias=False` G1/
excess-G2 (previously mixed a population moment with a sample std-dev); `bimodality_coef`
uses the SAS small-sample formula; `entropy` is now Pincus (1991) approximate entropy
(`m=2`, `r=0.2*sample sd`; `NULL` for an interior-missing or fewer-than-3-present series) —
previously a 10-bin Shannon entropy; `n_gaps` is now the number of **missing periods**
(previously gap *occurrences*) — the old occurrence count is the new `n_gap_runs` column.
`trend_strength`/`seasonality_strength` are now STL-based (`F_T`/`F_S`, Wang/Hyndman) via a
new `seasonal_period` parameter (see "Strength thresholds" below). `is_constant` (here and in
`ts_data_quality`) is SCALE-RELATIVE (D-13, G25): `max - min <= 1e-10 * max(1, max|x|)` over
present (non-NULL, non-NaN) values — NOT the same thing as `n_unique_values == 1`, which stays
an exact bit-identity count. `[1e12, 1e12 + 0.001]` is now constant; `[1.0, 1.0 + 1e-9]` is
not (the absolute-epsilon tolerance before this plan was the inverse on both).

```sql
-- Every stat for every series
SELECT * FROM ts_stats('sales', product_id, ds, y, '1d');

-- Quick sanity check: n_gaps = missing periods, n_gap_runs = gap occurrences
SELECT product_id, length, n_nulls, n_gaps, n_gap_runs, trend_strength, seasonality_strength
FROM ts_stats('sales', product_id, ds, y, '1d')
WHERE length < 30 OR n_gaps > 0
ORDER BY n_gaps DESC;

-- Explicit weekly period instead of auto-detection
SELECT product_id, trend_strength, seasonality_strength
FROM ts_stats('sales', product_id, ds, y, '1d', seasonal_period := 7);
```

### `ts_stats_agg` (aggregate)

For custom `GROUP BY` shapes. Takes `(date, value)` — DO NOT wrap in `LIST(...)`.

```sql
ts_stats_agg(date_col TIMESTAMP, value_col DOUBLE)
    → STRUCT(length UBIGINT, n_nulls UBIGINT, …, mean DOUBLE, …)
```

```sql
SELECT product_id,
       ts_stats_agg(ds, y).mean AS mean,
       ts_stats_agg(ds, y).seasonality_strength AS seas_str
FROM sales GROUP BY product_id;
```

### `ts_stats_by` — alias for `ts_stats`

### `ts_stats_summary` — aggregate stats across all groups

Summarise the per-group stats into panel-level statistics (mean, std, min, max of each metric). Signature is `(stats_table)` — a single argument naming an already-materialized `ts_stats_by` output table, not a raw source:

```sql
CREATE OR REPLACE TABLE sales_stats AS
SELECT * FROM ts_stats_by('sales', product_id, ds, y, '1d');

SELECT * FROM ts_stats_summary('sales_stats');
```

## Data quality

### `ts_data_quality` (table function) — per-series score card

```sql
ts_data_quality(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN,
                n_short INTEGER, frequency VARCHAR) → TABLE
```

Returns `unique_id` (group column **renamed** — the extension normalises it to `unique_id` in this output, even if the input col was `product_id`), plus `overall_score`, `structural_score`, `temporal_score`, `magnitude_score`, `behavioral_score`, `n_gaps`, `n_gap_runs`, `n_missing`, `is_constant`, `is_short`.

`n_short` is the minimum "not short" present-value count (typical: 14 for daily, 12 for
monthly) — it drives `structural_score`'s length factor and the `is_short` flag.
`frequency` (`'1d'`, `'1mo'`, ... or `NULL`/`''` to auto-detect) is used, together with
each series' real dates, for `n_gaps`/`n_gap_runs`/`temporal_score`:

- `n_gaps` = missing periods (expected timestamps absent under the frequency) — same
  meaning as `ts_stats`'s `n_gaps`.
- `n_gap_runs` = gap occurrences (contiguous runs of missing periods).
- `temporal_score` = `clamp(1 - 5 * n_gaps / expected_length, 0, 1)`.
- `behavioral_score` scores constancy, robust (Iglewicz-Hoaglin) outliers, and long
  zero-runs only — a smooth/predictable series carries no autocorrelation penalty.

```sql
SELECT * FROM ts_data_quality('sales', product_id, ds, y, 14, '1d');

-- Rank series by quality (note: output col is `unique_id`, not `product_id`)
SELECT unique_id, overall_score, structural_score, temporal_score, n_gaps, is_short
FROM ts_data_quality('sales', product_id, ds, y, 14, '1d')
ORDER BY overall_score;
```

### `ts_data_quality_by` — table-macro variant

Grouped macro sibling of `ts_data_quality` — same per-series quality scoring by group. Signature `ts_data_quality_by(source, unique_id_col, date_col, value_col, n_short, frequency)`.

### `ts_data_quality_agg` (aggregate)

Takes `(date, value)`, returns a STRUCT — no `n_short`/`frequency` arguments: `n_short`
is always `30` and frequency always auto-detects from the group's own timestamps.
Keeps `NULL`-valued rows (counted in `n_missing`); skips only `NULL`-timestamp rows.

```sql
SELECT product_id,
       ts_data_quality_agg(ds, y).overall_score AS q
FROM sales GROUP BY product_id;
```

### `ts_data_quality_summary` — panel-level roll-up

Returns `n_total`, `n_good`/`n_fair`/`n_poor` (`overall_score` buckets: `>=0.8`, `0.5-0.8`,
`<0.5`), `n_unassessed` (groups with no usable dates, e.g. all-NULL `date_col` — excluded
from the other buckets and from `avg_score`; D-10) and `avg_score` (mean over assessed
groups only). Invariant: `n_good + n_fair + n_poor + n_unassessed = n_total`.

```sql
SELECT * FROM ts_data_quality_summary('sales', product_id, ds, y, 14);
```

### `ts_quality_report` — human-readable report

### `ts_validate_timestamps_summary_by` — per-group timestamp validation

Summarises timestamp regularity per group against an expected grid — flags gaps, duplicates, and irregular spacing before forecasting. Signature `ts_validate_timestamps_summary_by(source, group_col, date_col, expected_timestamps)`.

## Diagnostics & validation (v0.7.0)

**Requires: json extension** (same as detection). Enable auto-load once per session:

```sql
SET autoinstall_known_extensions = 1;
SET autoload_known_extensions = 1;
```

Seven functions in two groups. The combined-verdict functions (`ts_stationarity`, `ts_residual_diagnostics`) are the recommended entry points — they run the component tests internally and return a single adequacy flag.

### Stationarity trio

Full detail in `docs/api/10-diagnostics.md`. `ts_adf`/`ts_kpss`/`ts_stationarity` (and their
`_by` macros) take a `regression := 'c' | 'ct'` parameter (default `'c'`): `'c'` tests around a
constant mean (statsmodels/tseries' default), `'ct'` tests around a constant + linear time
trend. `ts_adf` matches `statsmodels.tsa.stattools.adfuller(autolag="AIC")` exactly (AIC lag
selection, MacKinnon finite-sample critical values/p-values); `ts_kpss` matches R
`tseries::kpss.test()` for both `'c'` (`null = "Level"`) and `'ct'` (`null = "Trend"`, a trend
KPSS implemented in this extension's Rust core since the upstream crate has no trend variant);
KPSS p-values are bounded to `[0.01, 0.10]`.

#### `ts_adf` / `ts_adf_by` — Dickey-Fuller unit-root test (statsmodels-exact, AIC lag selection)

H0: series has a unit root (non-stationary). Reject H0 (p < 0.05) → stationary.

```sql
-- Scalar form: takes LIST(value ORDER BY date) (sourced from stationarity.sql Section 2)
SELECT
    (adf).statistic       AS t_statistic,
    (adf).p_value         AS p_value,
    (adf).lags            AS lags_used,
    (adf).is_stationary   AS is_stationary,
    ROUND((adf).cv_5pct, 3) AS critical_value_5pct
FROM (
    SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
    FROM sales_data
    WHERE product_id = 'mean_revert'
);

-- Grouped macro (sourced from stationarity.sql Section 3)
SELECT product_id,
       ROUND((adf).statistic, 4) AS t_statistic,
       (adf).p_value,
       (adf).lags,
       (adf).is_stationary,
       ROUND((adf).cv_1pct, 2) AS cv_1pct,
       ROUND((adf).cv_5pct, 2) AS cv_5pct,
       ROUND((adf).cv_10pct, 2) AS cv_10pct
FROM ts_adf_by('sales_data', product_id, ds, y)
ORDER BY product_id;
```

Optional second/third args to scalar form: `ts_adf(LIST(y ORDER BY ds), max_lags INTEGER, regression VARCHAR)` —
`max_lags := -1` = auto (default: `⌊(n−1)^(1/3)⌋`, AIC-selected up to that bound); `regression := 'c'` (default) or `'ct'`.

#### `ts_kpss` / `ts_kpss_by` — KPSS level/trend-stationarity test

H0: series IS level-stationary (opposite direction from ADF). Fail to reject (p > 0.05) → stationary.

```sql
-- Scalar form (sourced from stationarity.sql Section 5)
WITH s AS (SELECT i AS ds, sin(i/6.0) + (i%5)*0.01 AS y FROM range(1, 80) t(i))
SELECT (ts_kpss(LIST(y ORDER BY ds))).statistic     AS kpss_stat,
       (ts_kpss(LIST(y ORDER BY ds))).is_stationary AS is_stationary
FROM s;

-- Grouped macro (sourced from stationarity.sql Section 5)
SELECT product_id, (kpss).statistic, (kpss).is_stationary
FROM ts_kpss_by('sales_data', product_id, ds, y)
ORDER BY product_id;
```

#### `ts_stationarity` / `ts_stationarity_by` — combined four-way verdict (recommended entry point)

Runs ADF + KPSS internally; returns a combined `verdict` field plus the individual flags.

```sql
-- Grouped macro (sourced from stationarity.sql Section 6)
SELECT product_id,
       (stationarity).verdict,
       (stationarity).adf_is_stationary,
       (stationarity).kpss_is_stationary
FROM ts_stationarity_by('sales_data', product_id, ds, y)
ORDER BY product_id;
```

Verdict strings (the standard ADF+KPSS four-way mapping): `'stationary'` (ADF rejects the unit
root AND KPSS fails to reject level-stationarity), `'trend_stationary'` (ADF does NOT reject
but KPSS fails to reject — stationary only after removing a trend; use `regression := 'ct'` to
see this), `'non_stationary'` (ADF does NOT reject AND KPSS rejects), `'difference_stationary'`
(ADF rejects AND KPSS also rejects — conflicting; difference the series). Plus two explicit
non-ambiguous cases: `'constant'` for a zero-variance series, and SQL `NULL` (never a
misleading label) for a degenerate or too-short series. `regression := 'ct'` applies to both
component tests, so a deterministic-trend series that reads `non_stationary` under `'c'`
typically reads `trend_stationary` under `'ct'`.

---

### Residual-adequacy set

Apply to **residuals** from a fitted model (forecast errors), not to the raw series.

#### `ts_ljung_box` / `ts_ljung_box_by` — autocorrelation test (white-noise check, model-aware)

H0: residuals are white noise (no autocorrelation). Reject (p < 0.05) → residuals are correlated; model under-fits.

```sql
-- Grouped macro (sourced from residuals.sql)
SELECT series_id, (lb).statistic AS q_stat, (lb).p_value, (lb).lags
FROM ts_ljung_box_by('resids', series_id, ds, e) AS t(series_id, lb)
ORDER BY series_id;
```

Pass `fitted_params := p + q` for ARMA(p, q) residuals (or any count of estimated model
parameters): `df = lags - fitted_params`, and the p-value matches R
`Box.test(type = "Ljung-Box", fitdf = p + q)` exactly (default `fitted_params := 0` keeps
`df == lags`, correct only for raw/unmodeled residuals).

```sql
SELECT series_id, (lb).df, (lb).p_value
FROM ts_ljung_box_by('resids', series_id, ds, e, lags := 10, fitted_params := 1) AS t(series_id, lb)
ORDER BY series_id;
```

`fitted_params` at or above the effective (resolved/clamped) lag count is a SQL error naming
`fitted_params` and "degrees of freedom" — it never silently floors to `df = 1`.

#### `ts_durbin_watson` / `ts_durbin_watson_by` — first-order autocorrelation

DW statistic near 2 = no autocorrelation; < 2 = positive; > 2 = negative.

```sql
-- Grouped macro (sourced from residuals.sql)
SELECT series_id, (dw).statistic, (dw).interpretation
FROM ts_durbin_watson_by('resids', series_id, ds, e) AS t(series_id, dw)
ORDER BY series_id;
```

#### `ts_jarque_bera` / `ts_jarque_bera_by` — residual normality

H0: residuals are normally distributed. Reject (p < 0.05) → non-normal (may affect interval coverage).

```sql
-- Grouped macro (sourced from residuals.sql)
SELECT series_id, (jb).statistic, (jb).p_value, (jb).skewness, (jb).excess_kurtosis
FROM ts_jarque_bera_by('resids', series_id, ds, e) AS t(series_id, jb)
ORDER BY series_id;
```

#### `ts_residual_diagnostics` / `ts_residual_diagnostics_by` — combined adequacy report (recommended entry point)

Runs Ljung-Box, Durbin-Watson, and Jarque-Bera internally; returns a single `adequate BOOLEAN` verdict plus component fields.

```sql
-- Grouped macro (sourced from residuals.sql)
SELECT series_id, (rd).lb_p_value, (rd).dw_interpretation, (rd).adequate
FROM ts_residual_diagnostics_by('resids', series_id, ds, e) AS t(series_id, rd)
ORDER BY series_id;
```

### Diagnostics gotchas

- **Stationarity tests need sufficient length**: ADF / KPSS require at least ~20-30 observations for reliable p-values. On very short series, results are inconclusive by default.
- **Apply residual tests to residuals, not raw values**: Pass `(y - yhat)` as the value column, not the original series — `ts_ljung_box` on raw series will almost always reject, which is expected and uninformative.
- **`ts_adf_by` output column names** are the group column name (preserved) plus the `adf` STRUCT. Access sub-fields with `(adf).statistic`, etc.
- **KPSS H0 direction is opposite to ADF**: KPSS non-rejection means stationary; ADF non-rejection means non-stationary. Use `ts_stationarity_by` to avoid this confusion.

---

## Feature extraction (117 tsfresh-inspired features)

### `ts_features_by` — extract all 117 features

```sql
ts_features_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

Returns the group column + 117 feature columns: `mean`, `standard_deviation`, `skewness`, `kurtosis`, `length`, `linear_trend_slope`, `autocorrelation_lag1`, …

Every feature matches tsfresh 0.21.2 (or a direct numpy/scipy reference for the 9 features with no tsfresh counterpart) on a 9-series fixture suite, or carries a documented, intentional deviation — see [`docs/api/21-feature-reference.md`](../../../../docs/api/21-feature-reference.md)'s Deviation column.

```sql
-- Extract all features
SELECT * FROM ts_features_by('sales', product_id, ds, y);

-- Filter by feature values
SELECT product_id, mean, linear_trend_slope
FROM ts_features_by('sales', product_id, ds, y)
WHERE length > 30 AND autocorrelation_lag1 > 0.5;
```

### `ts_features` (scalar)

Scalar over `(date, value)`. Same 117 outputs as struct fields.

### `ts_features_list` / `ts_features_table`

Discover the feature catalogue:

```sql
SELECT column_name, feature_name, parameter_suffix
FROM ts_features_list()
WHERE feature_name LIKE '%autocorr%';
```

### `ts_features_config_template` — starter config

Table function (no args) returning a template config row for feature extraction — a scaffold to edit down into a custom feature subset (feeds `ts_features_config_from_json` / `ts_features_config_from_csv` below).

```sql
SELECT * FROM ts_features_config_template();
```

### Custom feature subsets

`ts_features_by`/`ts_features_table` take no config argument (4/3 positional args
only). Configure via JSON or CSV against the `ts_features_agg` aggregate's
`feature_selection`/`feature_params` arguments instead, unpacking the config
scalar's `feature_names`/`overrides` fields:

```sql
-- Load a JSON config
SELECT product_id,
    ts_features_agg(ds, y,
        (ts_features_config_from_json('{"features": ["mean", "std", "autocorrelation_lag1"]}')).feature_names,
        (ts_features_config_from_json('{"features": ["mean", "std", "autocorrelation_lag1"]}')).overrides
    ) AS feats
FROM sales GROUP BY product_id;

-- Or CSV
SELECT product_id,
    ts_features_agg(ds, y,
        (ts_features_config_from_csv('mean,std,skewness')).feature_names,
        (ts_features_config_from_csv('mean,std,skewness')).overrides
    ) AS feats
FROM sales GROUP BY product_id;
```

> **Note:** in this build, the `"features"` allow-list in the JSON/CSV config did not
> actually narrow the returned feature set (every call above still returned all 117
> features) — logged as a deferred finding, not fixed by this docs sweep.

### `ts_features_agg` — aggregate variant

For custom GROUP BY:

```sql
SELECT product_id, ts_features_agg(ds, y).mean, ts_features_agg(ds, y).autocorrelation_lag1
FROM sales GROUP BY product_id;
```

## Gotchas

- **`ts_stats` and `ts_data_quality` are TABLE functions** — call from FROM, not SELECT. Older docs incorrectly showed them as scalars over `LIST(...)`.
- **`_agg` variants take `(date, value)` directly** — no `LIST()` wrapping. Wrapping in `LIST(...)` errors as "No function matches …".
- **`trend_strength` / `seasonality_strength` formulas (v2026.10.16, STAT-02, D-02)**: both are
  Wang/Hyndman STL decomposition strengths from an R-faithful STL —
  `trend_strength = max(0, 1 - Var(R)/Var(T+R))`,
  `seasonality_strength = max(0, 1 - Var(R)/Var(S+R))` — computed via
  `stats::stl` (one period), the `forecast::mstl` iteration (several periods,
  reporting the **maximum** per-period strength), or a `supsmu` trend when no
  period is usable. The period comes from the new `seasonal_period` parameter
  (`0` auto-detects, `1` forces non-seasonal, `>= 2` is explicit).
  `seasonality_strength` is `NULL` when no period is usable (none detected,
  `seasonal_period := 1`, or `n <= 2 * seasonal_period`) or when the series
  decomposes to zero seasonal variance (e.g. a constant series).
  **Rule-of-thumb thresholds** (heuristics, not statistical tests — validate
  against your own data): **below 0.3** weak, **0.3 to 0.6** moderate, **0.6
  and above** strong. Example: the audit's 150-point random walk gives
  `seasonality_strength` about 0.09 (weak, matching "mostly noise");
  `AirPassengers` gives about 0.94 (strong, matching its well-known yearly
  cycle).
- **JSON extension is required** by a subset of quality / feature functions. Enable auto-load once per session: `SET autoinstall_known_extensions=1; SET autoload_known_extensions=1;`.
- **`ts_r2(DOUBLE[] actual, DOUBLE[] predicted) → DOUBLE`** is a scalar **regression R² metric**, not an EDA statistic — it lives with the scalar metrics family in `anofox-forecast-backtest`. Use it under `GROUP BY` to score fit quality, e.g. `SELECT ts_r2([1.0,2,3,4], [1.1,2.0,2.9,4.2]);`.
- **`ts_features_agg` / `ts_features` / `ts_features_by` share one Rust NULL policy** (Phase 12, D-11): a NULL-valued row is no longer spliced out independently — leading/trailing NULLs are trimmed, an interior NULL is a hard error naming its 1-based position, and an all-NULL series returns NULL (the whole struct for the aggregate, a row of NULL feature columns for the table function). All three surfaces agree on the same series.

## Canonical EDA pipeline

```sql
-- 1. Quality gate
CREATE OR REPLACE TABLE quality AS
SELECT * FROM ts_data_quality('raw', product_id, ds, y, 14, '1d');

-- 2. Series-level stats
CREATE OR REPLACE TABLE stats AS
SELECT * FROM ts_stats('raw', product_id, ds, y, '1d');

-- 3. Filter — keep only high-quality, forecastable series
CREATE OR REPLACE TABLE forecastable AS
SELECT r.*
FROM raw r
JOIN quality q ON r.product_id = q.unique_id  -- ts_data_quality renames the group col to unique_id
JOIN stats  s ON r.product_id = s.product_id
WHERE q.overall_score >= 0.6
  AND s.length >= 30
  AND NOT s.is_constant;

-- 4. Extract features for ML (optional)
CREATE OR REPLACE TABLE features AS
SELECT * FROM ts_features_by('forecastable', product_id, ds, y);
```

See also: `anofox-forecast-data-prep` (impute + drop bad series based on these stats), `anofox-forecast-detection` (`trend_strength` / `seasonality_strength` inform detection thresholds), `anofox-forecast-models` (use quality score / features to gate model selection).

Reference docs:
- `docs/api/03-statistics.md`
- `docs/api/20-feature-extraction.md`
- `docs/api/21-feature-reference.md`
