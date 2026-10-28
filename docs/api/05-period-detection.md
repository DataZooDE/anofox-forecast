# Period Detection

> Detect seasonal periods in time series data

## Overview

Period detection identifies recurring patterns in time series data. Use detected periods to configure seasonal forecasting models.

**Use this document to:**
- Detect dominant seasonal periods (e.g., 7 for weekly, 12 for monthly, 365 for yearly)
- Choose from 12 detection methods (FFT, ACF, autoperiod, Lomb-Scargle, etc.)
- Validate detected periods with seasonality classification
- Analyze peak timing regularity within seasonal cycles
- Build workflows that detect seasonality before forecasting

**Detection Methods:**
| Method | Description | Best For |
|--------|-------------|----------|
| `'fft'` | FFT periodogram-based (default) | Clean signals, fast |
| `'acf'` | Autocorrelation function | Cyclical patterns, noise-robust |
| `'autoperiod'` | FFT with ACF validation | General purpose, robust |
| `'auto'` | Auto-select best method | Unknown characteristics |

**Algorithm details (`'fft'`, `'acf'`, `'regression'`, Lomb-Scargle):**

- **`'fft'` (`ts_estimate_period_fft`):** linearly detrends the series first (least squares), then
  computes a one-sided periodogram equal to `scipy.signal.periodogram(x, fs=1,
  window='boxcar', detrend='linear', scaling='spectrum')`. The period is the strongest bin
  `3 <= k <= floor(n/2)`, so it is never `n` and always `< n/2` — e.g. `0.5*t +
  2*sin(2*pi*t/12)` reports period `12.0` for `n=120` and `12.1` for odd `n=121` (before this,
  the undetrended estimator reported period `= n`). Confidence is
  `power[k] / mean(power[1:])` (peak-to-mean-non-DC-power ratio). A constant or purely linear
  series (after detrending it is scale-relative constant) returns no period.
- **`'acf'` (`ts_estimate_period_acf`):** `statsmodels`-definition autocorrelation
  (`acf(x, fft=False, adjusted=False)`). The period is the SMALLEST lag `k >= 2` that is a
  local maximum (`r_k > r_{k-1}`, `r_k >= r_{k+1}`) exceeding the `1.96/sqrt(n)` significance
  bound — e.g. an alternating series reports period `2` (previously lag 2 was unreachable: the
  search started at lag 3 with no significance test). A series whose ACF never clears the
  bound returns no period.
- `'regression'` (`_ts_detect_periods(..., 'regression')`): fits a harmonic (Fourier)
  regression — intercept plus `cos`/`sin` harmonics below Nyquist — at every INTEGER candidate
  period in `2..floor(n/2)` by default, and picks the smallest period whose residual sum of
  squares is within `1e-10 * SST` of the minimum (ties broken toward the smaller period). A
  period-12 shape reports exactly `12` (previously a 50-point linear grid could skip 12
  entirely).
- **Lomb-Scargle (`ts_lomb_scargle`):** raises an error for `n_frequencies < 2`, a non-positive
  or non-finite `min_period`/`max_period`, or `min_period >= max_period` (previously these
  caused a division by zero or an inverted frequency grid). A default-derived range made
  impossible by a too-short series returns `NULL` rather than erroring. Power is normalised to
  match `scipy.signal.lombscargle(..., normalize=True) * n/2`.

**NULL handling:** every period estimator (`_ts_detect_periods` / `ts_estimate_period_fft` /
`ts_estimate_period_acf` / `ts_detect_multiple_periods` / `ts_autoperiod` / `ts_cfd_autoperiod` /
`ts_lomb_scargle` / `ts_aic_period` / `ts_ssa_period` / `ts_stl_period` / `ts_matrix_profile_period` /
`ts_sazed_period`) trims leading/trailing `NULL`/`NaN` values before detecting a period. An
interior `NULL` that remains after trimming is a hard error naming its 1-based position
(e.g. `ts_estimate_period_fft: Invalid input: interior NULL at position 6`); a list with no
non-NULL value returns SQL `NULL`. Impute interior gaps first if your series has them.

```sql
-- Edge trim: leading/trailing NULLs give the same period as the trimmed series.
SELECT ts_estimate_period_fft(list_concat([NULL, NULL]::DOUBLE[], v)) = ts_estimate_period_fft(v)
FROM (SELECT list_transform(range(24), lambda i: sin(2 * pi() * i / 12)::DOUBLE) AS v) t;
-- true
```

---

## Quick Start

Detect periods using table macros:

```sql
-- Multiple series (most common)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With different method
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'autoperiod'});
```

---

## Table Macros

### ts_detect_periods_by

Detect periods for grouped series.

**Signature:**
```sql
ts_detect_periods_by(source, group_col, date_col, value_col, params) → TABLE(id, periods)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | STRUCT/MAP | Configuration options |

**Params options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `method` | VARCHAR | `'fft'` | Detection method (see table below) |
| `max_period` | VARCHAR | `'365'` | Maximum period to search (limits ACF computation on long series) |
| `min_confidence` | VARCHAR | method-specific | Minimum confidence threshold. Periods below this are marked as "no seasonality". Use `'0'` to disable filtering. |

**Supported Methods:**
| Method | Aliases | Description | Best For |
|--------|---------|-------------|----------|
| `'fft'` | `'periodogram'` | FFT periodogram analysis (default) | Clean signals, fast |
| `'acf'` | `'autocorrelation'` | Autocorrelation function | Cyclical patterns |
| `'autoperiod'` | `'ap'` | FFT with ACF validation | General purpose, robust |
| `'cfd'` | `'cfdautoperiod'` | First-differenced FFT + ACF | Trending data |
| `'lombscargle'` | `'lomb_scargle'` | Lomb-Scargle periodogram | Irregular sampling |
| `'aic'` | `'aic_comparison'` | AIC-based model selection | Model comparison |
| `'ssa'` | `'singular_spectrum'` | Singular Spectrum Analysis | Complex patterns |
| `'stl'` | `'stl_period'` | STL decomposition | Decomposition-based |
| `'matrix_profile'` | `'matrixprofile'` | Matrix Profile motifs | Pattern repetition |
| `'sazed'` | `'zero_padded'` | SAZED spectral analysis | High frequency resolution |
| `'auto'` | — | Auto-select best method | Unknown characteristics |
| `'multi'` | `'multiple'` | Multiple periods | Complex seasonality |

**Returns:** TABLE with flattened columns (not one nested STRUCT) — the group column
(named after whatever `group_col` you pass, e.g. `product_id`), `periods`, `n_periods`,
`primary_period`, `method`.

**Output Structure:**
```text
<group_col>     ANY,       -- preserved group column (named after the group_col argument)
periods         STRUCT(
                    period      DOUBLE,   -- Detected period length (in samples/observations)
                    confidence  DOUBLE,   -- Detection confidence (see interpretation below)
                    strength    DOUBLE,   -- Seasonal strength at this period
                    amplitude   DOUBLE,   -- Amplitude of the sinusoidal component
                    phase       DOUBLE,   -- Phase offset (radians)
                    iteration   BIGINT    -- Iteration number (for multi-period detection)
                )[],
n_periods       BIGINT,    -- Number of periods detected
primary_period  DOUBLE,    -- Dominant period (strongest signal); top-level column,
                           -- not nested under `periods`
method          VARCHAR    -- Method used for detection
```

**Understanding Confidence:**

The `confidence` field measures how reliably the period was detected. Its interpretation depends on the method:

| Method | Confidence Meaning | Good Value |
|--------|-------------------|------------|
| `'fft'` | Peak-to-mean-non-DC-power ratio in the detrended periodogram | > 5.0 |
| `'acf'` | Autocorrelation value at the detected lag | > 0.3 |
| `'autoperiod'` | FFT confidence (ACF validation in separate field) | > 5.0 |
| `'regression'` | `min(mean_RSS / best_RSS, 10)` across candidate periods | higher is better, capped at 10 |

**Interpreting ACF Confidence:**
- **0.0 - 0.2**: Weak or no periodicity (may be noise)
- **0.2 - 0.4**: Moderate periodicity (borderline significant)
- **0.4 - 0.6**: Good periodicity (reliable detection)
- **0.6 - 1.0**: Strong periodicity (very confident detection)

Low confidence often indicates sparse data (many zeros), noisy signals, or series without clear seasonal patterns.

**Automatic Filtering:**

By default, periods with confidence below the method-specific threshold are automatically filtered out and the series is marked as having "no seasonality" (`primary_period = 0.0`, `method = "acf (no seasonality)"`).

- **Default thresholds:** ACF uses 0.3, FFT uses 5.0
- **Disable filtering:** Set `min_confidence` to `'0'` in params
- **Custom threshold:** Set `min_confidence` to your preferred value (e.g., `'0.5'`)

**Example:**
```sql
-- Detect periods for each product (default FFT method) -- primary_period/n_periods
-- are top-level output columns, not nested under `periods`
SELECT
    product_id,
    primary_period,
    n_periods
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With different methods
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'autoperiod'});  -- Robust FFT + ACF validation

SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'stl'});  -- STL decomposition-based

SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'auto'});  -- Auto-select best method

-- Filter by confidence (only keep reliable detections) -- periods is a LIST of
-- structs, index into it with [1], it is not itself a struct
SELECT product_id, primary_period, periods[1].confidence
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{'method': 'acf'})
WHERE periods[1].confidence > 0.3;

-- Limit max_period for faster computation on long series
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'acf', 'max_period': '28'});  -- Only search up to 28 days

-- Disable automatic confidence filtering (show all detected periods)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'acf', 'min_confidence': '0'});

-- Use custom confidence threshold
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'acf', 'min_confidence': '0.5'});  -- Stricter threshold
```

---

### ts_detect_periods_long_by

Long form of `ts_detect_periods_by` (#282): one row per period candidate, `<group_col>, rank,
period, confidence, strength, amplitude, phase, iteration, matches_expected,
matched_expected_period, match_deviation, is_primary, method`. Same `params` as
`ts_detect_periods_by`. (Periods are not time points, so there is no date column.)

```sql
SELECT * FROM ts_detect_periods_long_by('sales', product_id, date, quantity);
```

## Seasonality Analysis Workflow

### 1. Detect Seasonal Periods

Before forecasting, identify what periods exist in your data:

```sql
-- Detect periods for multiple products (primary_period/n_periods are top-level
-- output columns, not nested under `periods`)
SELECT
    product_id,
    primary_period,      -- Dominant period (e.g., 7 = weekly)
    n_periods             -- Number of significant periods found
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- Common periods: 7 (weekly), 12 (monthly), 52 (yearly for weekly data), 365 (yearly for daily data)
```

### 2. Validate Seasonality Strength

Not all detected periods are meaningful. Validate before using:

```sql
-- Check if seasonality is strong enough to model (output columns are flattened,
-- not nested under a `classification` struct)
SELECT
    product_id,
    is_seasonal,         -- Is there significant seasonality?
    seasonal_strength    -- 0-1 scale (>0.3 typically meaningful)
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0)
WHERE is_seasonal;
```

### 3. Understand Seasonal Behavior

Classify how seasonality manifests:

```sql
SELECT
    product_id,
    timing_classification,  -- 'early', 'on_time', 'late', 'variable'
    modulation_type,        -- 'stable', 'growing', 'shrinking', 'variable'
    has_stable_timing       -- Consistent peak timing?
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0);
```

### 4. Detect Peak Patterns

Find when peaks occur and how regular they are:

```sql
-- Find peak timing regularity (output columns are flattened, not nested under
-- a `timing` struct)
SELECT
    product_id,
    n_peaks,              -- How many peaks per series
    is_stable,            -- Are peaks at regular intervals?
    variability_score     -- Lower = more regular
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});
```

### 5. Decompose Series

Separate trend, seasonal, and residual components:

```sql
-- MSTL decomposition with multiple seasonal periods (seasonal_periods is a params
-- MAP key, not a separate positional argument; ts_mstl_decomposition_by has 5
-- positional args: source, group_col, date_col, value_col, params)
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, value,
    MAP{'seasonal_periods': '[7, 30]'}   -- weekly and monthly patterns
);
```

### 6. Complete Workflow: Detect → Validate → Forecast

```sql
-- Step 1: Detect periods
WITH detected AS (
    SELECT product_id, primary_period AS period
    FROM ts_detect_periods_by('sales', product_id, date, value, MAP{})
),
-- Step 2: Validate seasonality strength
validated AS (
    SELECT product_id, period
    FROM detected d
    JOIN (
        SELECT product_id, seasonal_strength AS strength
        FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0)
    ) c USING (product_id)
    WHERE strength > 0.3
)
-- Step 3: Forecast with validated period
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value,
    'HoltWinters', 14, '1d',
    MAP{'seasonal_period': '7'}  -- Use validated period
);
```

### Use Case Summary

| Goal | Function | Key Output |
|------|----------|------------|
| Find what periods exist | `ts_detect_periods_by` | `primary_period` |
| Check if seasonality is real | `ts_classify_seasonality_by` | `is_seasonal`, `seasonal_strength` |
| Understand seasonal behavior | `ts_classify_seasonality_by` | `timing_classification`, `modulation_type` |
| Find peak timing patterns | `ts_analyze_peak_timing_by` | `is_stable`, `variability_score` |
| Separate components | `ts_mstl_decomposition_by` | trend, seasonal, remainder |

---

*See also: [Decomposition](05a-decomposition.md) | [Peak Detection](05b-peak-detection.md)*
