# Statistical Diagnostics

> Stationarity tests and residual diagnostic functions

## Overview

Diagnostic functions test statistical properties of time series and model residuals.
They operate on a single column of values (no date column required inside the function;
ordering is supplied via `ORDER BY` in `LIST()`).

**NULL handling:** leading/trailing `NULL` (and `NaN`) values inside the ordered
`LIST(...)` are trimmed before the analysis runs. An interior `NULL` — one that remains
after trimming — is a hard error naming its 1-based position in the original list
(e.g. `ts_adf: Invalid input: interior NULL at position 6`), not a silent splice. A list
with no non-NULL value returns SQL `NULL`. If your series has interior gaps, impute them
first (e.g. `ts_fill_nulls_by`) rather than relying on this function to handle them.

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

**This document covers (Phase 1 — Statistical Diagnostics):**
- `ts_adf` / `ts_adf_by`: Dickey-Fuller unit-root test (STAT-01) — currently **non-augmented**
  (constant-only regression of Δy on y₋₁); see [Caveats](#caveats)
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

### `ts_adf` — Dickey-Fuller test

Tests the null hypothesis that the series has a unit root (is non-stationary).
Rejecting H₀ (small p-value) implies the series is stationary.

The underlying `anofox-forecast` ≤0.15.10 implementation is currently a **non-augmented**
Dickey-Fuller regression — it regresses Δy on y₋₁ with a constant only; it does not include
lagged difference terms in the regression. The `max_lags` parameter currently only trims how
much of the sample is used, it does not add augmentation terms.
(fix planned for v0.10.0, Phase 11/17)

#### Signature

```sql
ts_adf(series LIST(DOUBLE)) → STRUCT(...)
ts_adf(series LIST(DOUBLE), max_lags INTEGER) → STRUCT(...)
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `series` | `LIST(DOUBLE)` | — | Time series values, ordered by date via `LIST(y ORDER BY ds)` |
| `max_lags` | `INTEGER` | `-1` (auto) | Maximum lag/sample-trim value (see Caveats — currently selected by minimum RSS, not AIC, and does not add augmentation terms). `-1` = automatic: `⌊(n−1)^(1/3)⌋`, clamped to `min(max_lags, n/2−1).max(1)` |

#### Return STRUCT

Field order is fixed (plans 01-2 / 01-3 depend on this layout):

| Field | Type | Description |
|-------|------|-------------|
| `statistic` | `DOUBLE` | ADF t-statistic. More negative → stronger evidence of stationarity |
| `p_value` | `DOUBLE` | Approximate p-value for the test. See [Caveats](#caveats) |
| `lags` | `BIGINT` | Number of lags used (selected by minimum RSS, not AIC; or overridden by `max_lags`) (fix planned for v0.10.0, Phase 11) |
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

### 1. Non-augmented, constant-only regression (`'c'`) — **current limitation**

The `anofox-forecast` ≤0.15.10 crate implements a **non-augmented** Dickey-Fuller
regression (Δy on y₋₁, constant only) — not the augmented form (no lagged Δy terms added
to the regression), and only the constant-only (`'c'`) specification. The `'ct'` (constant +
trend) and `'n'` (no constant) regression modes specified in the CONTEXT are **not yet
functional** in the underlying crate. `max_lags` currently only trims how much of the sample
is used for the minimum-RSS lag search (see below) — it does not add augmentation terms.
Exposing augmentation and additional regression modes as parameters is deferred until the
crate is updated. (fix planned for v0.10.0, Phase 11/17)

**Impact:** Series with a deterministic trend may appear non-stationary even when
first-differenced, because the trend component is not accounted for. Use first-
differencing (`ts_diff_by`) as a preprocessing step if you expect trend-stationarity.

### 2. Lag selection is by minimum RSS, not AIC

Despite the "AIC lag selection" framing in earlier docs, the current implementation's
`compute_aic` helper returns raw residual sum of squares and ignores the number of
parameters `k` — so the lag actually chosen minimizes RSS, not AIC.
(fix planned for v0.10.0, Phase 11)

### 3. Approximate p-values — coarse step lookup, not MacKinnon interpolation

ADF p-values are currently computed from a 9-point lookup table with a **coarse step
function** (no smoothing or blending between breakpoints) — this is *not* the same
method used by `statsmodels.tsa.stattools.adfuller`, and it is not numerically
cross-validated against statsmodels' p-values. They are **not exact** and should be
interpreted as a rough approximation only:

- Accuracy is best very near the table breakpoints (p = 0.01, 0.05, 0.10)
- Values between breakpoints snap to the nearest breakpoint rather than interpolating,
  so the reported `p_value` can be off by double-digit percentage points
- For series shorter than ~30 observations, the approximation degrades further

(fix planned for v0.10.0, Phase 11/17)

### 4. Minimum series length

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
  `anofox-forecast` ≤0.15.10. `lags := -1` (default) selects the bandwidth automatically.
- p-values are an **unbounded extrapolation** of the KPSS table over `[0, 1]` — they are
  **not** clamped to `[0.01, 0.10]` (that clamping is only documented, not implemented) and
  can report values outside the conventional significance range.
  (fix planned for v0.10.0, Phase 11/17)

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

**Verdict truth table** (both flags mean "this test judges the series stationary") — current
implementation:

| `adf_is_stationary` | `kpss_is_stationary` | `verdict` | Interpretation |
|---|---|---|---|
| true  | true  | `stationary`            | Both tests agree — use as-is |
| true  | false | `trend_stationary`      | Stationary around a deterministic trend — detrend (e.g. `ts_detrend_by`) |
| false | false | `difference_stationary` | Unit root — apply differencing (`ts_diff_by`) |
| false | true  | `non_stationary`        | Conflicting / inconclusive — treat conservatively as non-stationary |

> **Current limitation:** the `trend_stationary` / `non_stationary` labels above are
> currently **inverted relative to the standard statsmodels convention** (where
> ADF-rejects + KPSS-rejects conventionally reads as conflicting/non-stationary, and
> ADF-rejects + KPSS-fails-to-reject reads as trend-stationary). A constant (zero-variance)
> series currently returns `verdict = non_stationary` rather than `stationary`. Treat the
> `verdict` string as implementation-defined today rather than matching the statsmodels
> literature convention; rely on the individual `adf_is_stationary` / `kpss_is_stationary`
> flags if you need the standard interpretation.
> (fix planned for v0.10.0, Phase 17)

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
`df == lags`. This `df == lags` accounting is accurate for raw residuals but does **not**
subtract the number of parameters fitted by the upstream model — if you are testing
residuals from a model with `k` estimated parameters, `df` should conventionally be
`lags - k`, and this function does not make that correction.
(fix planned for v0.10.0, Phase 17)

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

- MacKinnon, J. G. (1994). "Approximate asymptotic distribution functions for unit-root and cointegration tests." *Journal of Business & Economic Statistics*, 12(2), 167–176. (Table used as the lookup basis for the current coarse step-function p-value — see Caveats; the current implementation does not reproduce MacKinnon's interpolation.)
- statsmodels `adfuller` documentation: https://www.statsmodels.org/stable/generated/statsmodels.tsa.stattools.adfuller.html (cited for the test definition only — see Caveats for where the current p-value approximation diverges from statsmodels)
