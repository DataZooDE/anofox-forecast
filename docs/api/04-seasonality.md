# Seasonality

> Period detection and seasonal decomposition

## Overview

Seasonality functions detect periodic patterns in time series data and decompose series into trend, seasonal, and residual components.

---

## Quick Start

Detect periods using table macros (recommended):

```sql
-- Single series
SELECT * FROM ts_detect_periods('daily_sales', date, value, MAP{});

-- Multiple series
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With specific method
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, {'method': 'acf'});
```

Using the aggregate function with GROUP BY:

```sql
SELECT product_id, ts_detect_periods_agg(date, value) AS periods
FROM sales
GROUP BY product_id;
```

---

## Table Macros

### ts_detect_periods

Detect periods for a single series.

**Signature:**
```sql
ts_detect_periods(source, date_col, value_col, params) → TABLE(periods)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | STRUCT/MAP | Configuration options |

**Params options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `method` | VARCHAR | `'fft'` | Detection method: `'fft'` or `'acf'` |

**Returns:** TABLE with `periods` STRUCT containing detected periods.

**Example:**
```sql
SELECT
    (periods).primary_period,
    (periods).n_periods
FROM ts_detect_periods('daily_sales', date, value, MAP{});

-- With ACF method
SELECT * FROM ts_detect_periods('daily_sales', date, value, {'method': 'acf'});
```

---

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
| `method` | VARCHAR | `'fft'` | Detection method: `'fft'` or `'acf'` |

**Returns:** TABLE with `id` and `periods` STRUCT containing detected periods.

**Example:**
```sql
-- Detect periods for each product
SELECT
    id,
    (periods).primary_period,
    (periods).n_periods
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With ACF method
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, {'method': 'acf'});
```

---

## Aggregate Function

### ts_detect_periods_agg

Aggregate function for period detection with GROUP BY.

**Signature:**
```sql
ts_detect_periods_agg(date_col TIMESTAMP, value_col DOUBLE) → STRUCT
ts_detect_periods_agg(date_col TIMESTAMP, value_col DOUBLE, method VARCHAR) → STRUCT
```

**Parameters:**
- `date_col`: Date/timestamp column
- `value_col`: Value column
- `method`: Detection method (optional, default: 'fft')

**Returns:**
```sql
STRUCT(
    periods          STRUCT[],      -- Detected periods with metadata
    n_periods        BIGINT,        -- Number of periods found
    primary_period   DOUBLE,        -- Dominant period
    method           VARCHAR        -- Method used
)
```

**Example:**
```sql
-- Detect periods per product
SELECT
    product_id,
    ts_detect_periods_agg(date, value) AS periods
FROM sales
GROUP BY product_id;

-- With specific method
SELECT
    product_id,
    (ts_detect_periods_agg(date, value, 'acf')).primary_period
FROM sales
GROUP BY product_id;
```

---

## Internal Scalar Function

> **Note:** The `_ts_detect_periods` scalar function is internal and used by the table macros above.
> For direct usage, prefer the table macros or aggregate function.

### _ts_detect_periods

Detects seasonal periods using multiple methods (internal).

**Signature:**
```sql
_ts_detect_periods(values DOUBLE[]) → STRUCT
_ts_detect_periods(values DOUBLE[], method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `method`: Detection method (VARCHAR, optional, default: 'fft')
  - `'fft'` - FFT periodogram-based estimation
  - `'acf'` - Autocorrelation function approach

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

### ts_mstl_decomposition_by

Multiple Seasonal-Trend decomposition using Loess (MSTL).

**Signature:**
```sql
ts_mstl_decomposition_by(source, group_col, date_col, value_col, seasonal_periods, params)
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
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, quantity,
    [7, 365],  -- weekly and yearly seasonality
    MAP{}
);
```

---

### ts_classify_seasonality

Classify the type and strength of seasonality for a single-series table.

**Signature:**
```sql
ts_classify_seasonality(source VARCHAR, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:** Single row with `classification` STRUCT:
| Field | Type | Description |
|-------|------|-------------|
| `timing_classification` | VARCHAR | 'early', 'on_time', 'late', or 'variable' |
| `modulation_type` | VARCHAR | 'stable', 'growing', 'shrinking', or 'variable' |
| `has_stable_timing` | BOOLEAN | Whether peak timing is consistent |
| `timing_variability` | DOUBLE | Variability score (lower = more stable) |
| `seasonal_strength` | DOUBLE | Strength of seasonality (0-1) |
| `is_seasonal` | BOOLEAN | Whether significant seasonality exists |
| `cycle_strengths` | DOUBLE[] | Strength per cycle |
| `weak_seasons` | INTEGER[] | Indices of weak seasonal cycles |

**Example:**
```sql
-- Classify weekly seasonality for a single series
SELECT * FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);

-- Check if seasonality is strong and stable
SELECT
    (classification).is_seasonal,
    (classification).seasonal_strength,
    (classification).has_stable_timing
FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);
```

---

### ts_classify_seasonality_by

Classify seasonality type per group in a multi-series table.

**Signature:**
```sql
ts_classify_seasonality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id` | (same as group_col) | Group identifier |
| `classification` | STRUCT | Classification results (same fields as above) |

**Example:**
```sql
-- Classify weekly seasonality per product
SELECT * FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0);

-- Find products with strong, stable seasonality
SELECT id, (classification).seasonal_strength
FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0)
WHERE (classification).is_seasonal AND (classification).has_stable_timing;
```

---

### ts_classify_seasonality_agg

Aggregate function for classifying seasonality with GROUP BY.

**Signature:**
```sql
ts_classify_seasonality_agg(date_col TIMESTAMP, value_col DOUBLE, period DOUBLE) → STRUCT
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `date_col` | TIMESTAMP | Date/timestamp column |
| `value_col` | DOUBLE | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:** Same STRUCT as `ts_classify_seasonality`.

**Example:**
```sql
-- Classify seasonality per product using GROUP BY
SELECT
    product_id,
    ts_classify_seasonality_agg(date, value, 7.0) AS classification
FROM sales
GROUP BY product_id;

-- Access specific fields
SELECT
    product_id,
    (ts_classify_seasonality_agg(date, value, 7.0)).is_seasonal,
    (ts_classify_seasonality_agg(date, value, 7.0)).seasonal_strength
FROM sales
GROUP BY product_id;
```

---

## Advanced: Seasonality Analysis

> **Note:** These functions provide detailed seasonality analysis for advanced use cases.
> For simple period detection, use `ts_detect_periods` above.

### ts_detect_seasonality

Detect dominant periods/seasonality in a time series (scalar function).

**Signature:**
```sql
ts_detect_seasonality(values DOUBLE[]) → INTEGER[]
```

**Returns:** List of detected periods sorted by strength.

**Example:**
```sql
SELECT ts_detect_seasonality([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Returns: [4]
```

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

**Example:**
```sql
SELECT
    (ts_analyze_seasonality(values)).primary_period,
    (ts_analyze_seasonality(values)).seasonal_strength
FROM (SELECT LIST(value ORDER BY date) AS values FROM sales);
```

---

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

**Example:**
```sql
SELECT
    (ts_decompose_seasonal(values, 4.0, 'additive')).trend,
    (ts_decompose_seasonal(values, 4.0, 'additive')).seasonal
FROM (SELECT LIST(value ORDER BY date) AS values FROM quarterly_data);
```

---

### ts_seasonal_strength

Calculate seasonality strength (0-1 scale).

**Signature:**
```sql
ts_seasonal_strength(values DOUBLE[], period DOUBLE) → DOUBLE
ts_seasonal_strength(values DOUBLE[], period DOUBLE, method VARCHAR) → DOUBLE
```

**Returns:** Strength score from 0 (no seasonality) to 1 (strong seasonality).

**Example:**
```sql
SELECT ts_seasonal_strength(values, 12.0) AS monthly_strength
FROM (SELECT LIST(value ORDER BY date) AS values FROM monthly_sales)
WHERE monthly_strength > 0.3;  -- Filter series with meaningful seasonality
```

---

### ts_seasonal_strength_windowed

Calculate seasonal strength over sliding windows.

**Signature:**
```sql
ts_seasonal_strength_windowed(values DOUBLE[], period DOUBLE) → DOUBLE[]
```

**Returns:** Array of strength scores for each window position.

**Example:**
```sql
-- Track how seasonality evolves over time
SELECT UNNEST(ts_seasonal_strength_windowed(values, 12.0)) AS window_strength
FROM (SELECT LIST(value ORDER BY date) AS values FROM sales);
```

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

**Example:**
```sql
SELECT
    (ts_detrend(values, 'linear')).detrended,
    (ts_detrend(values, 'linear')).coefficients
FROM (SELECT LIST(value ORDER BY date) AS values FROM trending_data);
```

---

## Advanced: Peak Detection

> **Note:** These functions detect peaks and analyze timing patterns.

### ts_detect_peaks

Detect peaks in time series with prominence analysis.

**Signature:**
```sql
ts_detect_peaks(values DOUBLE[]) → STRUCT
ts_detect_peaks(values DOUBLE[], min_distance DOUBLE) → STRUCT
ts_detect_peaks(values DOUBLE[], min_distance DOUBLE, min_prominence DOUBLE) → STRUCT
ts_detect_peaks(values DOUBLE[], min_distance DOUBLE, min_prominence DOUBLE, smooth_first BOOLEAN) → STRUCT
```

**Parameters:**
- `values`: Time series data
- `min_distance`: Minimum distance between peaks (default: auto)
- `min_prominence`: Minimum peak prominence threshold (default: 0)
- `smooth_first`: Apply smoothing before detection (default: false)

**Returns:**
```sql
STRUCT(
    peaks       STRUCT(index BIGINT, time DOUBLE, value DOUBLE, prominence DOUBLE)[],
    n_peaks     BIGINT,          -- Number of peaks detected
    inter_peak_distances DOUBLE[], -- Distances between consecutive peaks
    mean_period DOUBLE           -- Mean distance between peaks
)
```

**Example:**
```sql
SELECT
    (ts_detect_peaks(values, 2.0, 1.0, false)).n_peaks,
    (ts_detect_peaks(values, 2.0, 1.0, false)).mean_period
FROM (SELECT LIST(value ORDER BY date) AS values FROM daily_sales);
```

---

### ts_analyze_peak_timing

Analyze peak timing regularity within expected period.

**Signature:**
```sql
ts_analyze_peak_timing(values DOUBLE[], period DOUBLE) → STRUCT
```

**Returns:**
```sql
STRUCT(
    n_peaks           BIGINT,      -- Number of peaks detected
    peak_times        DOUBLE[],    -- Timing of each peak
    variability_score DOUBLE,      -- Timing variability (lower = more regular)
    is_stable         BOOLEAN      -- True if timing is regular
)
```

**Example:**
```sql
-- Check if weekly peaks occur at consistent times
SELECT
    (ts_analyze_peak_timing(values, 7.0)).is_stable,
    (ts_analyze_peak_timing(values, 7.0)).variability_score
FROM (SELECT LIST(value ORDER BY date) AS values FROM weekly_data);
```

---

*See also: [Statistics](02-statistics.md) | [Forecasting](05-forecasting.md)*
