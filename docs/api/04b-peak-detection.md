# Peak Detection

> Detect peaks and analyze timing patterns in time series

## Overview

Peak detection identifies local maxima in time series data, useful for finding seasonal highs, demand spikes, or cyclical patterns. Peak timing analysis determines if peaks occur at consistent times within each period.

---

## Table Macros

### ts_detect_peaks_by

Detect peaks for multiple series grouped by an identifier.

**Signature:**
```sql
ts_detect_peaks_by(source, group_col, date_col, value_col, params) → TABLE(id, peaks)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | MAP | Configuration options |

**Params options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `min_distance` | DOUBLE | `1.0` | Minimum distance between peaks |
| `min_prominence` | DOUBLE | `0.0` | Minimum peak prominence threshold |
| `smooth_first` | BOOLEAN | `false` | Apply smoothing before detection |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id` | (same as group_col) | Series identifier |
| `peaks` | STRUCT | Peak detection results |

**Peaks STRUCT fields:**
| Field | Type | Description |
|-------|------|-------------|
| `peaks` | STRUCT[] | Array of peak info (index, time, value, prominence) |
| `n_peaks` | BIGINT | Number of peaks detected |
| `inter_peak_distances` | DOUBLE[] | Distances between consecutive peaks |
| `mean_period` | DOUBLE | Mean distance between peaks |

**Example:**
```sql
-- Detect peaks for each product
SELECT
    id,
    (peaks).n_peaks,
    (peaks).mean_period
FROM ts_detect_peaks_by('sales', product_id, date, value, MAP{});

-- With custom parameters (higher prominence threshold)
SELECT * FROM ts_detect_peaks_by(
    'sales', product_id, date, value,
    MAP{'min_distance': '3', 'min_prominence': '10.0'}
);
```

---

### ts_detect_peaks

Detect peaks for a single series.

**Signature:**
```sql
ts_detect_peaks(source, date_col, value_col, params) → TABLE(peaks)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | MAP | Configuration options (same as `_by` version) |

**Example:**
```sql
SELECT
    (peaks).n_peaks,
    (peaks).mean_period
FROM ts_detect_peaks('daily_sales', date, value, MAP{});
```

---

### ts_analyze_peak_timing_by

Analyze peak timing regularity for multiple series. Determines if peaks occur at consistent times within each period.

**Signature:**
```sql
ts_analyze_peak_timing_by(source, group_col, date_col, value_col, period, params) → TABLE(id, timing)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |
| `params` | MAP | Configuration options (reserved) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id` | (same as group_col) | Series identifier |
| `timing` | STRUCT | Peak timing analysis |

**Timing STRUCT fields:**
| Field | Type | Description |
|-------|------|-------------|
| `n_peaks` | BIGINT | Number of peaks detected |
| `peak_times` | DOUBLE[] | Timing of each peak within period |
| `variability_score` | DOUBLE | Timing variability (lower = more regular) |
| `is_stable` | BOOLEAN | True if peak timing is consistent |

**Example:**
```sql
-- Analyze weekly peak timing for each product
SELECT
    id,
    (timing).is_stable,
    (timing).variability_score
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});

-- Find products with stable weekly patterns
SELECT id
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{})
WHERE (timing).is_stable;
```

---

### ts_analyze_peak_timing

Analyze peak timing regularity for a single series.

**Signature:**
```sql
ts_analyze_peak_timing(source, date_col, value_col, period, params) → TABLE(timing)
```

**Example:**
```sql
-- Check if weekly peaks occur at consistent times
SELECT
    (timing).is_stable,
    (timing).variability_score
FROM ts_analyze_peak_timing('daily_sales', date, value, 7.0, MAP{});
```

---

## Internal Scalar Functions

> **Note:** The following scalar functions are internal and used by the table macros above.
> For typical usage, prefer the table macros.

### _ts_detect_peaks

Detect peaks in time series with prominence analysis (internal).

**Signature:**
```sql
_ts_detect_peaks(values DOUBLE[]) → STRUCT
_ts_detect_peaks(values DOUBLE[], min_distance DOUBLE) → STRUCT
_ts_detect_peaks(values DOUBLE[], min_distance DOUBLE, min_prominence DOUBLE) → STRUCT
_ts_detect_peaks(values DOUBLE[], min_distance DOUBLE, min_prominence DOUBLE, smooth_first BOOLEAN) → STRUCT
```

### _ts_analyze_peak_timing

Analyze peak timing regularity (internal).

**Signature:**
```sql
_ts_analyze_peak_timing(values DOUBLE[], period DOUBLE) → STRUCT
```

---

*See also: [Period Detection](04-period-detection.md) | [Decomposition](04a-decomposition.md) | [Advanced Analysis](04c-seasonality-analysis.md)*
