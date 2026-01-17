# Seasonality

> Period detection and seasonal decomposition

## Overview

Seasonality functions detect periodic patterns in time series data and decompose series into trend, seasonal, and residual components.

## Period Detection

### ts_detect_periods

Detects seasonal periods using multiple methods.

**Signature:**
```sql
ts_detect_periods(values DOUBLE[]) → STRUCT
ts_detect_periods(values DOUBLE[], method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `method`: Detection method (VARCHAR, optional, default: 'auto')
  - `'fft'` - FFT periodogram-based estimation
  - `'acf'` - Autocorrelation function approach
  - `'regression'` - Fourier regression grid search
  - `'multi'` - Iterative residual subtraction for concurrent periodicities
  - `'wavelet'` - Wavelet-based period detection
  - `'auto'` - Automatic method selection (default)

**Returns:**
```sql
STRUCT(
    periods          INTEGER[],     -- Detected periods sorted by strength
    confidences      DOUBLE[],      -- Confidence score for each period (0-1)
    primary_period   INTEGER,       -- Dominant period
    method_used      VARCHAR        -- Method that was used
)
```

**Example:**
```sql
-- Detect periods using FFT
SELECT ts_detect_periods(LIST(value ORDER BY date), 'fft') AS periods
FROM sales GROUP BY product_id;

-- Default auto-selection
SELECT (ts_detect_periods([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[])).primary_period;
-- Returns: 4
```

---

## Individual Period Detection Methods

The extension provides 11 specialized period detection algorithms, each optimized for different data characteristics.

### Method Comparison

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

**Example:**
```sql
SELECT ts_estimate_period_fft([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Returns: {period: 4.0, frequency: 0.25, power: ..., confidence: ..., method: "fft"}
```

---

### ts_estimate_period_acf

Autocorrelation Function based period detection.

**Signature:**
```sql
ts_estimate_period_acf(values DOUBLE[]) → STRUCT
ts_estimate_period_acf(values DOUBLE[], max_lag INTEGER) → STRUCT
```

**Returns:** Same structure as `ts_estimate_period_fft`

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

**Example:**
```sql
-- Better for data with trends
SELECT ts_cfd_autoperiod([1,3,5,7,2,4,6,8,3,5,7,9]::DOUBLE[]);
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

## Decomposition

### ts_mstl_decomposition

Multiple Seasonal-Trend decomposition using Loess (MSTL).

**Signature:**
```sql
ts_mstl_decomposition(source, group_col, date_col, value_col, seasonal_periods, params)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | IDENTIFIER | Series grouping column |
| `date_col` | IDENTIFIER | Date/timestamp column |
| `value_col` | IDENTIFIER | Value column |
| `seasonal_periods` | INTEGER[] | Array of seasonal periods |
| `params` | MAP | Additional parameters |

**Example:**
```sql
SELECT * FROM ts_mstl_decomposition(
    'sales', product_id, date, quantity,
    [7, 365],  -- weekly and yearly seasonality
    MAP{}
);
```

---

### ts_classify_seasonality

Classifies the type and strength of seasonality in a series.

**Signature:**
```sql
ts_classify_seasonality(source, group_col, date_col, value_col, params)
```

---

*See also: [Statistics](02-statistics.md) | [Forecasting](05-forecasting.md)*
