# Statistical Diagnostics

> Stationarity tests and residual diagnostic functions

## Overview

Diagnostic functions test statistical properties of time series and model residuals.
They operate on a single column of values (no date column required inside the function;
ordering is supplied via `ORDER BY` in `LIST()`).

**This document covers (Phase 1, Plan 01-1 — STAT-01 ADF):**
- `ts_adf` / `ts_adf_by`: Augmented Dickey-Fuller unit-root test (stationarity)

**Placeholders for Plans 01-2 and 01-3 (sections to be filled in):**
- `ts_kpss` / `ts_kpss_by`: KPSS level-stationarity test (STAT-02)
- `ts_stationarity` / `ts_stationarity_by`: combined ADF + KPSS verdict (STAT-03)
- `ts_ljung_box` / `ts_ljung_box_by`: Ljung-Box white-noise test (RESID-01)
- `ts_durbin_watson` / `ts_durbin_watson_by`: Durbin-Watson autocorrelation test (RESID-02)
- `ts_jarque_bera` / `ts_jarque_bera_by`: Jarque-Bera normality test (RESID-03)
- `ts_residual_diagnostics_by`: combined residual adequacy report (RESID-04)

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

## Placeholders (Plans 01-2 and 01-3)

The following functions will be documented here once implemented:

### `ts_kpss` / `ts_kpss_by` *(Plan 01-2 — STAT-02)*

> KPSS level-stationarity test. Tests the null that the series **is** stationary
> (opposite null from ADF). Returns same STRUCT layout as `ts_adf`.

### `ts_stationarity` / `ts_stationarity_by` *(Plan 01-2 — STAT-03)*

> Combined ADF + KPSS verdict. Returns extended STRUCT with both test results
> and a four-way verdict string.

### `ts_ljung_box` / `ts_ljung_box_by` *(Plan 01-3 — RESID-01)*

> Ljung-Box white-noise test for residual autocorrelation.

### `ts_durbin_watson` / `ts_durbin_watson_by` *(Plan 01-3 — RESID-02)*

> Durbin-Watson first-order autocorrelation statistic.

### `ts_jarque_bera` / `ts_jarque_bera_by` *(Plan 01-3 — RESID-03)*

> Jarque-Bera normality test (skewness + excess kurtosis).

### `ts_residual_diagnostics_by` *(Plan 01-3 — RESID-04)*

> Combined residual adequacy report: Ljung-Box gate + Durbin-Watson + Jarque-Bera.

---

## Reference

- MacKinnon, J. G. (1994). "Approximate asymptotic distribution functions for unit-root and cointegration tests." *Journal of Business & Economic Statistics*, 12(2), 167–176.
- statsmodels `adfuller` documentation: https://www.statsmodels.org/stable/generated/statsmodels.tsa.stattools.adfuller.html
