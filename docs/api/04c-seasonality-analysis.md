# Advanced Seasonality Analysis

> Detailed seasonality analysis functions for advanced use cases

## Overview

These functions provide detailed seasonality analysis including strength metrics, windowed analysis, detrending, and amplitude modulation detection. For simple period detection, use `ts_detect_periods_by` instead.

---

## Scalar Functions

### ts_detect_seasonality

Detect dominant periods/seasonality in a time series.

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

*See also: [Period Detection](04-period-detection.md) | [Decomposition](04a-decomposition.md) | [Internal Reference](04d-internal-period-functions.md)*
