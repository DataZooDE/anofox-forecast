# Internal: Seasonality Scalar Functions

> Internal scalar functions for period detection and seasonality analysis

## Overview

These scalar functions are internal building blocks used by the table macros. For typical usage, prefer the `_by` table macros:

| Instead of... | Use... |
|---------------|--------|
| `ts_detect_seasonality` | `ts_detect_periods_by` |
| `ts_analyze_seasonality` | `ts_classify_seasonality_by` |
| `ts_decompose_seasonal` | `ts_mstl_decomposition_by` |
| `ts_seasonal_strength` | `ts_classify_seasonality_by` (returns `.seasonal_strength`) |

---

## Period Detection

### _ts_detect_periods

Detects seasonal periods using multiple methods (internal).

**Signature:**
```sql
_ts_detect_periods(values DOUBLE[]) → STRUCT
_ts_detect_periods(values DOUBLE[], method VARCHAR) → STRUCT
```

**Returns:**
```sql
STRUCT(
    periods          STRUCT[],      -- Detected periods with metadata
    n_periods        BIGINT,        -- Number of periods found
    primary_period   DOUBLE,        -- Dominant period
    method           VARCHAR        -- Method used
)
```

---

### Method Reference

| Function | Speed | Noise Robustness | Best Use Case | Min Observations |
|----------|-------|------------------|---------------|------------------|
| `ts_estimate_period_fft` | Very Fast | Low | Clean signals | 4 |
| `ts_estimate_period_acf` | Fast | Medium | Cyclical patterns | 4 |
| `ts_autoperiod` | Fast | High | General purpose | 8 |
| `ts_cfd_autoperiod` | Fast | Very High | Trending data | 9 |
| `ts_lomb_scargle` | Medium | High | Irregular sampling | 4 |
| `ts_aic_period` | Slow | High | Model selection | 8 |
| `ts_ssa_period` | Medium | Medium | Complex patterns | 16 |
| `ts_stl_period` | Slow | Medium | Decomposition | 16 |
| `ts_matrix_profile_period` | Slow | Very High | Pattern repetition | 32 |
| `ts_sazed_period` | Medium | High | Frequency resolution | 16 |
| `ts_detect_multiple_periods` | Medium | High | Multiple seasonalities | 8 |

---

### ts_estimate_period_fft

FFT-based periodogram analysis that identifies the dominant frequency.

**Signature:**
```sql
ts_estimate_period_fft(values DOUBLE[]) → STRUCT
```

**Returns:**
```sql
STRUCT(
    period     DOUBLE,   -- Estimated period (in samples)
    frequency  DOUBLE,   -- Dominant frequency (1/period)
    power      DOUBLE,   -- Power at the dominant frequency
    confidence DOUBLE,   -- Ratio of peak power to mean power
    method     VARCHAR   -- "fft"
)
```

---

### ts_estimate_period_acf

Autocorrelation Function based period detection.

**Signature:**
```sql
ts_estimate_period_acf(values DOUBLE[]) → STRUCT
ts_estimate_period_acf(values DOUBLE[], max_lag INTEGER) → STRUCT
```

---

### ts_autoperiod

Hybrid two-stage approach combining FFT with ACF validation.

**Signature:**
```sql
ts_autoperiod(values DOUBLE[]) → STRUCT
ts_autoperiod(values DOUBLE[], acf_threshold DOUBLE) → STRUCT
```

**Returns:**
```sql
STRUCT(
    period         DOUBLE,   -- Detected period (from FFT)
    fft_confidence DOUBLE,   -- FFT peak-to-mean power ratio
    acf_validation DOUBLE,   -- ACF value at detected period
    detected       BOOLEAN,  -- TRUE if acf_validation > threshold
    method         VARCHAR   -- "autoperiod"
)
```

---

### ts_cfd_autoperiod

Clustered Filtered Detrended variant that applies first-differencing before FFT analysis.

**Signature:**
```sql
ts_cfd_autoperiod(values DOUBLE[]) → STRUCT
ts_cfd_autoperiod(values DOUBLE[], acf_threshold DOUBLE) → STRUCT
```

---

### ts_lomb_scargle

Lomb-Scargle periodogram for unevenly sampled data.

**Signature:**
```sql
ts_lomb_scargle(values DOUBLE[]) → STRUCT
ts_lomb_scargle(values DOUBLE[], min_freq DOUBLE) → STRUCT
ts_lomb_scargle(values DOUBLE[], min_freq DOUBLE, max_freq DOUBLE) → STRUCT
ts_lomb_scargle(values DOUBLE[], min_freq DOUBLE, max_freq DOUBLE, n_freqs INTEGER) → STRUCT
```

**Returns:**
```sql
STRUCT(
    period           DOUBLE,   -- Detected period
    frequency        DOUBLE,   -- Corresponding frequency
    power            DOUBLE,   -- Normalized power at peak
    false_alarm_prob DOUBLE,   -- FAP (lower = more significant)
    method           VARCHAR   -- "lomb_scargle"
)
```

---

### ts_aic_period

Information criterion-based period selection using AIC.

**Signature:**
```sql
ts_aic_period(values DOUBLE[]) → STRUCT
ts_aic_period(values DOUBLE[], min_period INTEGER) → STRUCT
ts_aic_period(values DOUBLE[], min_period INTEGER, max_period INTEGER) → STRUCT
```

---

### ts_ssa_period

Singular Spectrum Analysis based period detection.

**Signature:**
```sql
ts_ssa_period(values DOUBLE[]) → STRUCT
ts_ssa_period(values DOUBLE[], window_size INTEGER) → STRUCT
```

---

### ts_stl_period

STL decomposition-based period detection.

**Signature:**
```sql
ts_stl_period(values DOUBLE[]) → STRUCT
ts_stl_period(values DOUBLE[], min_period INTEGER, max_period INTEGER) → STRUCT
```

---

### ts_matrix_profile_period

Matrix Profile based period detection for finding repeated patterns.

**Signature:**
```sql
ts_matrix_profile_period(values DOUBLE[]) → STRUCT
ts_matrix_profile_period(values DOUBLE[], min_period INTEGER, max_period INTEGER) → STRUCT
```

---

### ts_sazed_period

SAZED (Spectral Analysis with Zero-crossing Enhanced Detection) period detection.

**Signature:**
```sql
ts_sazed_period(values DOUBLE[]) → STRUCT
```

---

### ts_detect_multiple_periods

Detects multiple concurrent periodicities using iterative residual analysis.

**Signature:**
```sql
ts_detect_multiple_periods(values DOUBLE[]) → STRUCT
ts_detect_multiple_periods(values DOUBLE[], max_periods INTEGER) → STRUCT
```

---

## Seasonality Analysis

> These functions are covered by `ts_classify_seasonality_by` for typical usage.

### ts_detect_seasonality

Detect dominant periods in a time series.

**Signature:**
```sql
ts_detect_seasonality(values DOUBLE[]) → INTEGER[]
```

**Returns:** List of detected periods sorted by strength.

---

### ts_analyze_seasonality

Comprehensive seasonality analysis with strength metrics.

**Signature:**
```sql
ts_analyze_seasonality(values DOUBLE[]) → STRUCT
```

**Returns:**
```sql
STRUCT(
    detected_periods    INTEGER[],   -- All detected periods
    primary_period      INTEGER,     -- Dominant period
    seasonal_strength   DOUBLE,      -- Seasonality strength (0-1)
    trend_strength      DOUBLE       -- Trend strength (0-1)
)
```

---

## Decomposition

> For typical usage, prefer `ts_mstl_decomposition_by`.

### ts_decompose_seasonal

Decompose series into trend, seasonal, and remainder components.

**Signature:**
```sql
ts_decompose_seasonal(values DOUBLE[], period DOUBLE) → STRUCT
ts_decompose_seasonal(values DOUBLE[], period DOUBLE, method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series data
- `period`: Seasonal period
- `method`: `'additive'` (default) or `'multiplicative'`

**Returns:**
```sql
STRUCT(
    trend       DOUBLE[],    -- Trend component
    seasonal    DOUBLE[],    -- Seasonal component
    remainder   DOUBLE[],    -- Residual component
    period      DOUBLE,      -- Period used
    method      VARCHAR      -- Method used
)
```

---

### ts_detrend

Remove trend from time series data.

**Signature:**
```sql
ts_detrend(values DOUBLE[]) → STRUCT
ts_detrend(values DOUBLE[], method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series data
- `method`: `'linear'` (default) or other methods

**Returns:**
```sql
STRUCT(
    trend        DOUBLE[],    -- Extracted trend
    detrended    DOUBLE[],    -- Series with trend removed
    method       VARCHAR,     -- Method used
    coefficients DOUBLE[],    -- Trend coefficients (for linear: [intercept, slope])
    rss          DOUBLE,      -- Residual sum of squares
    n_params     BIGINT       -- Number of parameters
)
```

---

## Advanced Analysis (No `_by` Wrapper)

> These functions provide unique capabilities not available via table macros.

### ts_seasonal_strength

Calculate seasonality strength (0-1 scale).

**Signature:**
```sql
ts_seasonal_strength(values DOUBLE[], period DOUBLE) → DOUBLE
ts_seasonal_strength(values DOUBLE[], period DOUBLE, method VARCHAR) → DOUBLE
```

**Returns:** Strength score from 0 (no seasonality) to 1 (strong seasonality).

---

### ts_seasonal_strength_windowed

Calculate seasonal strength over sliding windows.

**Signature:**
```sql
ts_seasonal_strength_windowed(values DOUBLE[], period DOUBLE) → DOUBLE[]
```

**Returns:** Array of strength scores for each window position.

---

### ts_detect_seasonality_changes

Detect changes in seasonality pattern over time.

**Signature:**
```sql
ts_detect_seasonality_changes(values DOUBLE[], period DOUBLE) → STRUCT
```

**Returns:** Information about detected changes in seasonal patterns.

---

### ts_instantaneous_period

Calculate instantaneous period at each point in the time series.

**Signature:**
```sql
ts_instantaneous_period(values DOUBLE[]) → DOUBLE[]
```

**Returns:** Array of instantaneous periods for each observation.

---

### ts_detect_amplitude_modulation

Detect amplitude modulation in seasonal patterns.

**Signature:**
```sql
ts_detect_amplitude_modulation(values DOUBLE[], period DOUBLE) → STRUCT
```

**Returns:** Amplitude modulation characteristics.

---

*See also: [Period Detection](05-period-detection.md) | [Decomposition](05a-decomposition.md) | [Peak Detection](05b-peak-detection.md)*
