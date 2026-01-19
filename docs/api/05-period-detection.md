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

---

## Quick Start

Detect periods using table macros (recommended):

```sql
-- Multiple series (most common)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With different method
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'autoperiod'});

-- Single series
SELECT * FROM ts_detect_periods('daily_sales', date, value, MAP{});
```

Using the aggregate function with GROUP BY:

```sql
SELECT product_id, ts_detect_periods_agg(date, value) AS periods
FROM sales
GROUP BY product_id;
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

**Returns:** TABLE with `id` and `periods` STRUCT containing detected periods.

**Example:**
```sql
-- Detect periods for each product (default FFT method)
SELECT
    id,
    (periods).primary_period,
    (periods).n_periods
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With different methods
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'autoperiod'});  -- Robust FFT + ACF validation

SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'stl'});  -- STL decomposition-based

SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'auto'});  -- Auto-select best method
```

---

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
| `method` | VARCHAR | `'fft'` | Detection method (see supported methods above) |

**Returns:** TABLE with `periods` STRUCT containing detected periods.

**Example:**
```sql
SELECT
    (periods).primary_period,
    (periods).n_periods
FROM ts_detect_periods('daily_sales', date, value, MAP{});

-- With different methods
SELECT * FROM ts_detect_periods('daily_sales', date, value, MAP{'method': 'autoperiod'});
SELECT * FROM ts_detect_periods('daily_sales', date, value, MAP{'method': 'auto'});
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

## Seasonality Analysis Workflow

### 1. Detect Seasonal Periods

Before forecasting, identify what periods exist in your data:

```sql
-- Detect periods for multiple products
SELECT
    id,
    (periods).primary_period,      -- Dominant period (e.g., 7 = weekly)
    (periods).n_periods            -- Number of significant periods found
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- Common periods: 7 (weekly), 12 (monthly), 52 (yearly for weekly data), 365 (yearly for daily data)
```

### 2. Validate Seasonality Strength

Not all detected periods are meaningful. Validate before using:

```sql
-- Check if seasonality is strong enough to model
SELECT
    id,
    (classification).is_seasonal,         -- Is there significant seasonality?
    (classification).seasonal_strength    -- 0-1 scale (>0.3 typically meaningful)
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0)
WHERE (classification).is_seasonal;
```

### 3. Understand Seasonal Behavior

Classify how seasonality manifests:

```sql
SELECT
    id,
    (classification).timing_classification,  -- 'early', 'on_time', 'late', 'variable'
    (classification).modulation_type,        -- 'stable', 'growing', 'shrinking', 'variable'
    (classification).has_stable_timing       -- Consistent peak timing?
FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0);
```

### 4. Detect Peak Patterns

Find when peaks occur and how regular they are:

```sql
-- Find peak timing regularity
SELECT
    id,
    (timing).n_peaks,              -- How many peaks per series
    (timing).is_stable,            -- Are peaks at regular intervals?
    (timing).variability_score     -- Lower = more regular
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});
```

### 5. Decompose Series

Separate trend, seasonal, and residual components:

```sql
-- MSTL decomposition with multiple seasonal periods
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, value,
    [7, 365],    -- Weekly and yearly patterns
    MAP{}
);
```

### 6. Complete Workflow: Detect → Validate → Forecast

```sql
-- Step 1: Detect periods
WITH detected AS (
    SELECT id, (periods).primary_period AS period
    FROM ts_detect_periods_by('sales', product_id, date, value, MAP{})
),
-- Step 2: Validate seasonality strength
validated AS (
    SELECT id, period
    FROM detected d
    JOIN (
        SELECT id, (classification).seasonal_strength AS strength
        FROM ts_classify_seasonality_by('sales', product_id, date, value, 7.0)
    ) c USING (id)
    WHERE strength > 0.3
)
-- Step 3: Forecast with validated period
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value,
    'HoltWinters', 14,
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

*See also: [Decomposition](05a-decomposition.md) | [Peak Detection](05b-peak-detection.md) | [Internal Reference](05c-internal-period-functions.md)*
