# Statistical Diagnostics

> Stationarity tests and residual diagnostic functions

## Overview

Diagnostic functions test statistical properties of time series and model residuals.
They operate on a single column of values (no date column required inside the function;
ordering is supplied via `ORDER BY` in `LIST()`).

**This document covers (Phase 1 — Statistical Diagnostics):**
- `ts_adf` / `ts_adf_by`: Augmented Dickey-Fuller unit-root test (STAT-01)
- `ts_kpss` / `ts_kpss_by`: KPSS level-stationarity test (STAT-02)
- `ts_stationarity` / `ts_stationarity_by`: combined ADF + KPSS four-way verdict (STAT-03)

**Residual diagnostics (operate on residuals, not the raw series):**
- `ts_ljung_box` / `ts_ljung_box_by`: Ljung-Box white-noise test (RESID-01)
- `ts_durbin_watson` / `ts_durbin_watson_by`: Durbin-Watson autocorrelation test (RESID-02)
- `ts_jarque_bera` / `ts_jarque_bera_by`: Jarque-Bera normality test (RESID-03)
- `ts_residual_diagnostics` / `ts_residual_diagnostics_by`: combined residual adequacy report (RESID-04)

---

## Quick Start

```sql
LOAD anofox_forecast;

-- ADF test for one series
SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
FROM my_table;

-- Stationarity test across all groups
SELECT
    product_id,
    (adf).statistic     AS t_stat,
    (adf).p_value       AS p_val,
    (adf).is_stationary AS is_stationary
FROM ts_adf_by('my_table', product_id, ds, y)
ORDER BY product_id;
```

---

## Stationarity Tests

### `ts_adf` — Augmented Dickey-Fuller test

Tests the null hypothesis that the series has a unit root (is non-stationary).
Rejecting H₀ (small p-value) implies the series is stationary.

#### Signature

```sql
ts_adf(series LIST(DOUBLE)) → STRUCT(...)
ts_adf(series LIST(DOUBLE), max_lags INTEGER) → STRUCT(...)
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `series` | `LIST(DOUBLE)` | — | Time series values, ordered by date via `LIST(y ORDER BY ds)` |
| `max_lags` | `INTEGER` | `-1` (auto) | Maximum number of lags for AIC lag selection. `-1` = automatic: `⌊(n−1)^(1/3)⌋`, clamped to `min(max_lags, n/2−1).max(1)` |

#### Return STRUCT

Field order is fixed (plans 01-2 / 01-3 depend on this layout):

| Field | Type | Description |
|-------|------|-------------|
| `statistic` | `DOUBLE` | ADF t-statistic. More negative → stronger evidence of stationarity |
| `p_value` | `DOUBLE` | Approximate p-value for the test. See [Caveats](#caveats) |
| `lags` | `BIGINT` | Number of lags used (AIC-selected or overridden by `max_lags`) |
| `is_stationary` | `BOOLEAN` | `true` if `statistic < cv_5pct` (5% significance level) |
| `cv_1pct` | `DOUBLE` | Critical value at 1% significance (`-3.43` for constant regression) |
| `cv_5pct` | `DOUBLE` | Critical value at 5% significance (`-2.86` for constant regression) |
| `cv_10pct` | `DOUBLE` | Critical value at 10% significance (`-2.57` for constant regression) |

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

-- With max_lags override
SELECT ts_adf(LIST(y ORDER BY ds), 3) AS adf
FROM sales_data
WHERE product_id = 'SKU_001';
```

---

### `ts_adf_by` — ADF test per group (table macro)

Runs `ts_adf` for each group and returns one row per group.
Internally uses `LIST(value ORDER BY date) GROUP BY group_col` — fully parallel
via DuckDB's GROUP BY engine.

#### Signature

```sql
ts_adf_by(
    source     VARCHAR,    -- table name (string)
    group_col  <any>,      -- column to group by
    date_col   <any>,      -- date / timestamp / integer column for ordering
    value_col  <any>,      -- numeric value column
    max_lags  := -1        -- named parameter: max lag override (-1 = auto)
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

-- Override lag selection
SELECT product_id, (adf).lags
FROM ts_adf_by('sales_data', product_id, ds, y, max_lags:=2);
```

---

## Caveats

### 1. Constant-only regression (`'c'`) — **current limitation**

The `anofox-forecast` v0.15.3 crate implements only constant-only ADF regression.
The `'ct'` (constant + trend) and `'n'` (no constant) regression modes specified
in the CONTEXT are **not yet functional** in the underlying crate. Exposing them
as parameters is deferred until the crate is updated.

**Impact:** Series with a deterministic trend may appear non-stationary even when
first-differenced, because the trend component is not accounted for. Use first-
differencing (`ts_diff_by`) as a preprocessing step if you expect trend-stationarity.

### 2. Approximate p-values

ADF p-values are computed using the MacKinnon (1994) 9-point lookup table
and piecewise-linear interpolation — the same approximation method used by
`statsmodels.tsa.stattools.adfuller`. They are **not exact** and should be
interpreted as approximate:

- Accuracy is best near p = 0.01, 0.05, 0.10 (table breakpoints)
- Interpolation between breakpoints introduces rounding to the nearest breakpoint
- For series shorter than ~30 observations, the approximation degrades further

**Tolerance vs statsmodels:** Numeric cross-checks show `statistic` agrees within
`rtol=0.01` and `p_value` within `rtol=0.10` against `statsmodels.tsa.stattools.adfuller`.

### 3. Minimum series length

Both `ts_adf` and `ts_kpss` return `NaN` for `statistic` when the series has
fewer than 4 observations. No error is raised; check for `isnan((adf).statistic)`.

---

## `ts_kpss` / `ts_kpss_by` (STAT-02)

KPSS (Kwiatkowski–Phillips–Schmidt–Shin) stationarity test. Its null hypothesis
is the **opposite** of ADF's: `H0 = the series is level-stationary`. A large
statistic rejects that null (evidence of non-stationarity).

```sql
-- scalar form
SELECT (ts_kpss(LIST(y ORDER BY ds))).* FROM sales;

-- optional bandwidth override (number of lags for the long-run variance)
SELECT ts_kpss(LIST(y ORDER BY ds), 4) FROM sales;

-- grouped form
SELECT group_col, (kpss).statistic, (kpss).is_stationary
FROM ts_kpss_by('sales', product_id, ds, y);
```

**Signatures**

- `ts_kpss(series LIST(DOUBLE) [, lags INTEGER]) → STRUCT(...)`
- `ts_kpss_by(source, group_col, date_col, value_col [, lags := -1]) → TABLE(group_col, kpss STRUCT(...))`

**Returned STRUCT** — same layout as `ts_adf`: `statistic DOUBLE`, `p_value DOUBLE`,
`lags BIGINT`, `is_stationary BOOLEAN`, `cv_1pct DOUBLE`, `cv_5pct DOUBLE`, `cv_10pct DOUBLE`.
For KPSS, `is_stationary = true` means the statistic is **below** the 5% critical
value (fails to reject the stationarity null).

**Caveats**

- Level (`'c'`) specification only; the trend (`'ct'`) specification is not exposed in
  `anofox-forecast` v0.15.3. `lags := -1` (default) selects the bandwidth automatically.
- p-values are approximate (piecewise-linear interpolation of the KPSS table, clamped
  to `[0.01, 0.10]`).

## `ts_stationarity` / `ts_stationarity_by` (STAT-03)

Runs **both** ADF and KPSS and derives a four-way verdict by combining the two
per-test stationarity flags.

```sql
SELECT (ts_stationarity(LIST(y ORDER BY ds))).verdict FROM sales;

SELECT group_col, (stationarity).verdict
FROM ts_stationarity_by('sales', product_id, ds, y);
```

**Signatures**

- `ts_stationarity(series LIST(DOUBLE)) → STRUCT(...)`
- `ts_stationarity_by(source, group_col, date_col, value_col) → TABLE(group_col, stationarity STRUCT(...))`

**Returned STRUCT**: `adf_statistic DOUBLE`, `adf_p_value DOUBLE`, `kpss_statistic DOUBLE`,
`kpss_p_value DOUBLE`, `adf_is_stationary BOOLEAN`, `kpss_is_stationary BOOLEAN`,
`verdict VARCHAR`.

**Verdict truth table** (both flags mean "this test judges the series stationary"):

| `adf_is_stationary` | `kpss_is_stationary` | `verdict` | Interpretation |
|---|---|---|---|
| true  | true  | `stationary`            | Both tests agree — use as-is |
| true  | false | `trend_stationary`      | Stationary around a deterministic trend — detrend (e.g. `ts_detrend_by`) |
| false | false | `difference_stationary` | Unit root — apply differencing (`ts_diff_by`) |
| false | true  | `non_stationary`        | Conflicting / inconclusive — treat conservatively as non-stationary |

> Note: this follows the standard ADF+KPSS combination. `trend_stationary` is the case
> where ADF rejects the unit root but KPSS rejects level-stationarity (a deterministic
> trend is present); `difference_stationary` is where both tests point to a unit root.

## Residual Diagnostics

These operate on **residuals** (forecast error series `y - ŷ`), not the raw
series. Supply the residual column as `value_col` to the `_by` macros.

### `ts_ljung_box` / `ts_ljung_box_by` (RESID-01)

Ljung-Box white-noise test — the primary check for leftover autocorrelation in
residuals. A small p-value means the residuals are **not** white noise (the model
missed structure).

- `ts_ljung_box(residuals LIST(DOUBLE) [, lags INTEGER]) → STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, df BIGINT)`
- `ts_ljung_box_by(source, group_col, date_col, value_col [, lags := -1])`

Default `lags = min(10, n/5)`. Residuals are treated as raw (0 fitted params), so
`df == lags`.

```sql
SELECT group_col, (ljung_box).p_value FROM ts_ljung_box_by('resids', series_id, ds, e);
```

### `ts_durbin_watson` / `ts_durbin_watson_by` (RESID-02)

Durbin-Watson first-order autocorrelation statistic (range `[0, 4]`; `≈2` means no
autocorrelation, `<2` positive, `>2` negative).

- `ts_durbin_watson(residuals LIST(DOUBLE)) → STRUCT(statistic DOUBLE, interpretation VARCHAR)`
- `interpretation` ∈ `positive_strong` / `positive_weak` / `none` / `negative_weak` / `negative_strong`.

### `ts_jarque_bera` / `ts_jarque_bera_by` (RESID-03)

Jarque-Bera normality test based on residual skewness and excess kurtosis. A small
p-value rejects normality.

- `ts_jarque_bera(residuals LIST(DOUBLE)) → STRUCT(statistic DOUBLE, p_value DOUBLE, skewness DOUBLE, excess_kurtosis DOUBLE)`

### `ts_residual_diagnostics` / `ts_residual_diagnostics_by` (RESID-04)

One-shot residual adequacy report combining all three tests.

- `ts_residual_diagnostics(residuals LIST(DOUBLE) [, alpha DOUBLE]) → STRUCT(...)`
- `ts_residual_diagnostics_by(source, group_col, date_col, value_col [, alpha := 0.05])`

**Returned STRUCT**: `lb_statistic`, `lb_p_value`, `lb_lags`, `dw_statistic`,
`dw_interpretation`, `jb_statistic`, `jb_p_value`, `jb_skewness`,
`jb_excess_kurtosis`, `adequate BOOLEAN`.

**Adequacy rule**: `adequate = (lb_p_value > alpha)` — the Ljung-Box white-noise
test is the gate (residuals must be free of autocorrelation). Durbin-Watson and
Jarque-Bera are reported as **advisory** fields and do not affect `adequate`.

```sql
SELECT group_col, (rd).adequate, (rd).dw_interpretation
FROM ts_residual_diagnostics_by('resids', series_id, ds, e) AS t(group_col, rd);
```

---

## Reference

- MacKinnon, J. G. (1994). "Approximate asymptotic distribution functions for unit-root and cointegration tests." *Journal of Business & Economic Statistics*, 12(2), 167–176.
- statsmodels `adfuller` documentation: https://www.statsmodels.org/stable/generated/statsmodels.tsa.stattools.adfuller.html
