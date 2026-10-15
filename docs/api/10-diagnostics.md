# Statistical Diagnostics

> Stationarity tests and residual diagnostic functions — statsmodels/R-tseries-exact,
> fixture-verified

## Overview

Diagnostic functions test statistical properties of time series and model residuals.
They operate on a single column of values (no date column required inside the function;
ordering is supplied via `ORDER BY` in `LIST()`).

**NULL handling:** leading/trailing `NULL` (and `NaN`) values inside the ordered
`LIST(...)` are trimmed before the analysis runs. An interior `NULL` — one that remains
after trimming — is a hard error naming its 1-based position in the original list
(e.g. `ts_adf: Invalid input: interior NULL at position 6`), not a silent splice. A list
with no non-NULL value returns SQL `NULL`. If your series has interior gaps, impute them
first (e.g. `ts_fill_nulls_mean_by`) rather than relying on this function to handle them.
Verified by `test/sql/ts_series_null_policy.test`.

```sql
-- Edge trim: leading/trailing NULLs give the same result as the trimmed series.
SELECT ts_adf([NULL, 1.0, 2.0, 1.5, 2.5, 1.8, NULL]::DOUBLE[])
     = ts_adf([1.0, 2.0, 1.5, 2.5, 1.8]::DOUBLE[]);
-- true
```

An interior NULL that survives trimming is a hard error naming its 1-based position
(wrap in `try(...)` to get `NULL` instead of an error):

```sql
SELECT ts_adf([1.0, 2.0, NULL, 1.5, 2.5]::DOUBLE[]);
-- Invalid Input Error: ts_adf: Invalid input: interior NULL at position 3
```

**This document covers:**
- `ts_adf` / `ts_adf_by`: Dickey-Fuller unit-root test (STAT-01)
- `ts_kpss` / `ts_kpss_by`: KPSS level/trend-stationarity test (STAT-02)
- `ts_stationarity` / `ts_stationarity_by`: combined ADF + KPSS four-way verdict (STAT-03)
- `ts_ljung_box` / `ts_ljung_box_by`: Ljung-Box white-noise test (RESID-01)
- `ts_durbin_watson` / `ts_durbin_watson_by`: Durbin-Watson autocorrelation test (RESID-02)
- `ts_jarque_bera` / `ts_jarque_bera_by`: Jarque-Bera normality test (RESID-03)
- `ts_residual_diagnostics` / `ts_residual_diagnostics_by`: combined residual adequacy report (RESID-04)

Every behavioural claim in this document names the fixture-backed test that verifies it:
`crates/anofox-fcst-core/tests/stationarity_reference.rs`,
`crates/anofox-fcst-core/tests/portmanteau_reference.rs`, and the non-json-gated
`test/sql/ts_diagnostics_reference.test`.

---

## Quick Start

```sql
LOAD anofox_forecast;

-- ADF test for one series
SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
FROM sales_data
WHERE product_id = 'SKU_001';

-- Stationarity test across all groups
SELECT
    product_id,
    (adf).statistic     AS t_stat,
    (adf).p_value       AS p_val,
    (adf).is_stationary AS is_stationary
FROM ts_adf_by('sales_data', product_id, ds, y)
ORDER BY product_id;
```

---

## Stationarity Tests

### `ts_adf` — Dickey-Fuller test

Tests the null hypothesis that the series has a unit root (is non-stationary).
Rejecting H0 (small p-value) implies the series is stationary.

Matches `statsmodels.tsa.stattools.adfuller(autolag="AIC")` exactly: an augmented
regression of Δy on y₋₁ (plus a constant, and a linear trend under `regression := 'ct'`)
with lagged Δy terms selected by AIC over `0..max_lags`, MacKinnon (1994, 2010) p-values
and finite-sample critical values. Verified to 1e-8 (statistic/p-value) and 1e-10
(critical values) against 8 series × `{'c', 'ct'}` in
`stationarity_reference.rs#adf_matches_statsmodels_for_every_series_and_regression`
and `test/sql/ts_diagnostics_reference.test`.

#### Signature

```text
ts_adf(series LIST(DOUBLE)) → STRUCT(...)
ts_adf(series LIST(DOUBLE), max_lags INTEGER) → STRUCT(...)
ts_adf(series LIST(DOUBLE), max_lags INTEGER, regression VARCHAR) → STRUCT(...)
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `series` | `LIST(DOUBLE)` | — | Time series values, ordered by date via `LIST(y ORDER BY ds)` |
| `max_lags` | `INTEGER` | `-1` (auto) | Maximum lag count for the AIC search. `-1` = `⌊(n−1)^(1/3)⌋`, clamped to `n/2 − ntrend − 1` the way statsmodels does |
| `regression` | `VARCHAR` | `'c'` | `'c'` (constant) or `'ct'` (constant + linear trend), case-insensitive. Any other value is a SQL error: `"expected one of: c, ct"` |

#### Return STRUCT

Field order is fixed:

| Field | Type | Description |
|-------|------|-------------|
| `statistic` | `DOUBLE` | ADF t-statistic. More negative → stronger evidence of stationarity. `NaN` for a degenerate series (e.g. `n < 4`) |
| `p_value` | `DOUBLE` | MacKinnon (1994, 2010) p-value |
| `lags` | `BIGINT` | AIC-selected lag count (or the `max_lags` override) |
| `is_stationary` | `BOOLEAN` | `true` if `statistic < cv_5pct` (5% significance level) |
| `cv_1pct` | `DOUBLE` | MacKinnon finite-sample critical value at 1%, depends on `nobs` and `regression` |
| `cv_5pct` | `DOUBLE` | Critical value at 5% |
| `cv_10pct` | `DOUBLE` | Critical value at 10% |

A degenerate/too-short series (e.g. `n < 4`) returns `statistic`/`p_value`/critical values
all `NaN`, never a crash: `SELECT isnan((ts_adf([1.0, 2.0, 3.0]::DOUBLE[])).statistic)` → `true`.

#### Example

```sql
-- ADF test on a single series
SELECT
    (adf).statistic     AS t_statistic,
    (adf).p_value       AS p_value,
    (adf).lags          AS lags,
    (adf).is_stationary AS is_stationary
FROM (
    SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
    FROM sales_data
    WHERE product_id = 'SKU_001'
);

-- With max_lags override, and regression := 'ct' for a trending series
SELECT ts_adf(LIST(y ORDER BY ds), 3) AS adf_default
FROM sales_data
WHERE product_id = 'SKU_001';

SELECT ts_adf(LIST(y ORDER BY ds), -1, 'ct') AS adf_trend
FROM sales_data
WHERE product_id = 'SKU_001';
```

---

### `ts_adf_by` — ADF test per group (table macro)

Runs `ts_adf` for each group and returns one row per group.
Internally uses `LIST(value ORDER BY date) GROUP BY group_col` — fully parallel
via DuckDB's GROUP BY engine.

#### Signature

```text
ts_adf_by(
    source     VARCHAR,    -- table name (string)
    group_col  <any>,      -- column to group by
    date_col   <any>,      -- date / timestamp / integer column for ordering
    value_col  <any>,      -- numeric value column
    max_lags    := -1,     -- named parameter: max lag override (-1 = auto)
    regression  := 'c'     -- named parameter: 'c' or 'ct'
) → TABLE(group_col <any>, adf STRUCT(...))
```

#### Example

```sql
-- All groups with default lag selection
SELECT
    product_id,
    (adf).statistic     AS t_stat,
    (adf).p_value       AS p_val,
    (adf).is_stationary AS is_stationary
FROM ts_adf_by('sales_data', product_id, ds, y)
ORDER BY product_id;

-- Override lag selection and regression
SELECT product_id, (adf).lags
FROM ts_adf_by('sales_data', product_id, ds, y, max_lags := 2, regression := 'ct');
```

---

## `ts_kpss` / `ts_kpss_by` (STAT-02)

KPSS (Kwiatkowski–Phillips–Schmidt–Shin) stationarity test. Its null hypothesis
is the **opposite** of ADF's: `H0 = the series is stationary` around the chosen
`regression` (level under `'c'`, around a linear trend under `'ct'`). A large
statistic rejects that null (evidence of non-stationarity).

`regression := 'c'` matches both `statsmodels.tsa.stattools.kpss` and R
`tseries::kpss.test(null = "Level", lshort = TRUE)`. `regression := 'ct'` dispatches to a
trend-KPSS implemented in this extension's Rust core (the upstream crate has no trend
variant): OLS-detrended residuals, the crate's own Bartlett long-run-variance kernel, and R
`tseries::kpss.test(null = "Trend")`-matching critical values. Both are verified to 1e-10
against statsmodels/R in `stationarity_reference.rs#kpss_matches_statsmodels_for_every_series_and_regression`
/ `#kpss_matches_r_tseries_for_r_backed_series` and `test/sql/ts_diagnostics_reference.test`.
p-values are bounded to `[0.01, 0.10]` for both `'c'` and `'ct'` (interpolated tseries tables).

```sql
-- scalar form
SELECT kpss.* FROM (SELECT ts_kpss(LIST(y ORDER BY ds)) AS kpss FROM sales);

-- optional bandwidth override (number of lags for the long-run variance)
SELECT ts_kpss(LIST(y ORDER BY ds), 4) FROM sales;

-- regression := 'ct' for a trending series
SELECT ts_kpss(LIST(y ORDER BY ds), -1, 'ct') FROM sales;

-- grouped form
SELECT product_id, (kpss).statistic, (kpss).is_stationary
FROM ts_kpss_by('sales', product_id, ds, y, regression := 'ct');
```

**Signatures**

- `ts_kpss(series LIST(DOUBLE) [, lags INTEGER [, regression VARCHAR]]) → STRUCT(...)`
- `ts_kpss_by(source, group_col, date_col, value_col [, lags := -1] [, regression := 'c']) → TABLE(group_col, kpss STRUCT(...))`

**Returned STRUCT** — same layout as `ts_adf`: `statistic DOUBLE`, `p_value DOUBLE`,
`lags BIGINT`, `is_stationary BOOLEAN`, `cv_1pct DOUBLE`, `cv_5pct DOUBLE`, `cv_10pct DOUBLE`.
For KPSS, `is_stationary = true` means the statistic is **below** the 5% critical
value (fails to reject the stationarity null).

## `ts_stationarity` / `ts_stationarity_by` (STAT-03)

Runs **both** ADF and KPSS (under the same `regression`) and derives a four-way verdict
by combining the two per-test stationarity flags.

```sql
SELECT (ts_stationarity(LIST(y ORDER BY ds))).verdict FROM sales;

SELECT product_id, (stationarity).verdict
FROM ts_stationarity_by('sales', product_id, ds, y, regression := 'ct');
```

**Signatures**

- `ts_stationarity(series LIST(DOUBLE) [, regression VARCHAR]) → STRUCT(...)`
- `ts_stationarity_by(source, group_col, date_col, value_col [, regression := 'c']) → TABLE(group_col, stationarity STRUCT(...))`

**Returned STRUCT**: `adf_statistic DOUBLE`, `adf_p_value DOUBLE`, `kpss_statistic DOUBLE`,
`kpss_p_value DOUBLE`, `adf_is_stationary BOOLEAN`, `kpss_is_stationary BOOLEAN`,
`verdict VARCHAR`.

**Verdict truth table** (the standard ADF+KPSS mapping, D-04 — verified in
`validation.rs#classify_stationarity_truth_table` and
`stationarity_reference.rs#stationarity_verdict_matches_fixture_for_every_comparable_series`):

| `adf_is_stationary` | `kpss_is_stationary` | `verdict` | Interpretation |
|---|---|---|---|
| true  | true  | `stationary`            | Both tests agree — use as-is |
| false | true  | `trend_stationary`      | ADF doesn't reject but KPSS doesn't reject either — stationary only after removing a trend; use `regression := 'ct'` to surface this |
| false | false | `non_stationary`        | Neither test rejects — treat as non-stationary |
| true  | false | `difference_stationary` | Conflicting (ADF rejects, KPSS also rejects) — apply differencing (`ts_diff_by`) |

Two additional explicit cases (`validation.rs#stationarity_constant_and_degenerate_verdicts`):

- A **constant** (zero-variance) series returns `verdict = 'constant'`, not a misleading
  label.
- A **degenerate or too-short** series (e.g. `n < 4`, or a collinear regression) returns
  `verdict IS NULL` — never a label implying a real result — with `adf_statistic` and the
  critical values all `NaN`.

#### Quick guide

Under `regression := 'c'` (the default), a series with a deterministic trend usually reads
`non_stationary` (ADF can't reject around a flat mean when the series obviously isn't flat).
Under `regression := 'ct'`, `'trend_stationary'` means neither ADF nor KPSS rejects the
trend-adjusted series, and `'stationary'` means the series is stationary around the fitted
trend. See `examples/diagnostics/stationarity.sql` Section 7 for a worked
`non_stationary` ('c') → `trend_stationary` ('ct') example.

## Edge Cases

- **Short series (`n < 4`)**: `ts_adf`/`ts_kpss` return `NaN` for every numeric field
  (statistic, p-value, critical values), never a crash or a misleading real-looking value.
  `ts_stationarity`'s `verdict` is `NULL` in this case, not a label.
- **Constant series**: `ts_stationarity` returns `verdict = 'constant'`.
- **Unknown `regression`**: any value other than `'c'`/`'ct'` (case-insensitive) is a SQL
  error: `"expected one of: c, ct"`. A `NULL` `regression` argument means `'c'` (the
  default), not an error.
- **Interior `NULL`/`NaN`** (after edge-trimming): a hard error naming the 1-based position,
  e.g. `"interior NULL at position 6"`.

---

## Residual Diagnostics

These operate on **residuals** (forecast error series `y - ŷ`), not the raw
series. Supply the residual column as `value_col` to the `_by` macros.

### `ts_ljung_box` / `ts_ljung_box_by` (RESID-01, model-aware)

Ljung-Box white-noise test — the primary check for leftover autocorrelation in
residuals. A small p-value means the residuals are **not** white noise (the model
missed structure).

- `ts_ljung_box(residuals LIST(DOUBLE) [, lags INTEGER [, fitted_params INTEGER]]) → STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, df BIGINT)`
- `ts_ljung_box_by(source, group_col, date_col, value_col [, lags := -1] [, fitted_params := 0])`

Default `lags = min(10, n/5).max(1)`, then clamped to `n - 1`. `df = lags - fitted_params`:
pass `fitted_params := p + q` for ARMA(p, q) residuals (or any count of estimated model
parameters) to get R `Box.test(type = "Ljung-Box", fitdf = p + q)`-matching results — the
p-value comes from the exact chi-square survival function
(`statrs::distribution::ChiSquared`), matching R `pchisq` exactly even for `df > 30`.
Verified against R on an ARMA(1,1) residual series and first-differenced random-walk series
in `portmanteau_reference.rs#ljung_box_matches_r_box_test_fitdf_for_portmanteau_cases` and
`test/sql/ts_diagnostics_reference.test`.

**Default `fitted_params := 0`** keeps `df == lags` — correct only when `residuals` are raw,
unmodeled data (which is also what `ts_residual_diagnostics`'s Ljung-Box gate uses).

```sql
SELECT series_id, (ljung_box).p_value FROM ts_ljung_box_by('resids', series_id, ds, e);

-- model-aware: residuals from an ARMA(1,1) fit have 2 estimated parameters
SELECT series_id, (ljung_box).df, (ljung_box).p_value
FROM ts_ljung_box_by('resids', series_id, ds, e, lags := 10, fitted_params := 2);
```

**Errors and edge cases** (verified in `validation.rs`'s `ljung_box_*` unit tests and
`test/sql/ts_diagnostics_reference.test`'s "17-02 Ljung-Box edge cases" section):

- `fitted_params` at or above the **effective** (resolved/clamped) lag count — whether
  `lags` was given explicitly or defaulted — is a SQL error naming `fitted_params`, the
  lags, and "degrees of freedom". It never silently floors to `df = 1`.
- A negative `fitted_params` is a SQL error: `"fitted_params must be >= 0, got <value>"`.
- `lags` far larger than the series length is clamped to `n - 1` and reported in `.lags`,
  not a panic.
- `n < 3` returns `NaN` statistic/p-value (lags 0, df 0), not an error — there is no
  usable lag count to validate `fitted_params` against yet.
- A constant residual series returns `statistic = 0`, `p_value = 1`.

### `ts_durbin_watson` / `ts_durbin_watson_by` (RESID-02)

Durbin-Watson first-order autocorrelation statistic: the closed form
`sum(diff(e)^2) / sum(e^2)` on the raw input (no demeaning), range `[0, 4]`. Verified against
the closed-form fixture value (which matches this formula exactly on both the R and Rust
sides) in `portmanteau_reference.rs#durbin_watson_matches_closed_form_for_every_portmanteau_case`.

- `ts_durbin_watson(residuals LIST(DOUBLE)) → STRUCT(statistic DOUBLE, interpretation VARCHAR)`

`interpretation` is a heuristic label from fixed thresholds on `statistic`: `< 0.5` →
`positive_strong`, `< 1.5` → `positive_weak`, `<= 2.5` → `none`, `< 3.5` → `negative_weak`,
otherwise `negative_strong`.

### `ts_jarque_bera` / `ts_jarque_bera_by` (RESID-03)

Jarque-Bera normality test based on residual skewness and excess kurtosis. A small
p-value rejects normality. Matches R `tseries::jarque.bera.test` to ~1e-13 on five
reference series, verified in
`portmanteau_reference.rs#jarque_bera_matches_tseries_for_every_r_diagnostics_series`.

- `ts_jarque_bera(residuals LIST(DOUBLE)) → STRUCT(statistic DOUBLE, p_value DOUBLE, skewness DOUBLE, excess_kurtosis DOUBLE)`

### `ts_residual_diagnostics` / `ts_residual_diagnostics_by` (RESID-04)

One-shot residual adequacy report combining all three tests.

- `ts_residual_diagnostics(residuals LIST(DOUBLE) [, alpha DOUBLE]) → STRUCT(...)`
- `ts_residual_diagnostics_by(source, group_col, date_col, value_col [, alpha := 0.05])`

**Returned STRUCT**: `lb_statistic`, `lb_p_value`, `lb_lags`, `dw_statistic`,
`dw_interpretation`, `jb_statistic`, `jb_p_value`, `jb_skewness`,
`jb_excess_kurtosis`, `adequate BOOLEAN`.

**Adequacy rule**: `adequate = (lb_p_value > alpha)`, using the Ljung-Box gate with
`fitted_params = 0` — the gate assumes raw residuals, not a model-corrected `df`. Durbin-Watson
and Jarque-Bera are reported as **advisory** fields and do not affect `adequate`.
`(ts_residual_diagnostics(v)).lb_p_value` is identical to `(ts_ljung_box(v)).p_value` on the
same input (verified in `test/sql/ts_diagnostics_reference.test`).

```sql
SELECT group_col, (rd).adequate, (rd).dw_interpretation
FROM ts_residual_diagnostics_by('resids', series_id, ds, e) AS t(group_col, rd);
```

---

## Reference

- MacKinnon, J. G. (1994). "Approximate asymptotic distribution functions for unit-root
  and cointegration tests." *Journal of Business & Economic Statistics*, 12(2), 167–176.
- MacKinnon, J. G. (2010). "Critical values for cointegration tests." *Queen's Economics
  Department Working Paper* No. 1227.
- Kwiatkowski, D., Phillips, P. C. B., Schmidt, P., & Shin, Y. (1992). "Testing the null
  hypothesis of stationarity against the alternative of a unit root." *Journal of
  Econometrics*, 54(1-3), 159–178.
- Ljung, G. M., & Box, G. E. P. (1978). "On a measure of lack of fit in time series
  models." *Biometrika*, 65(2), 297–303.
- statsmodels `adfuller`/`kpss` documentation:
  https://www.statsmodels.org/stable/generated/statsmodels.tsa.stattools.adfuller.html,
  https://www.statsmodels.org/stable/generated/statsmodels.tsa.stattools.kpss.html
- R `tseries::kpss.test`, `tseries::jarque.bera.test`, `stats::Box.test`:
  https://cran.r-project.org/web/packages/tseries/
