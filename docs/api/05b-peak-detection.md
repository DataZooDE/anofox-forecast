# Peak Detection

> Detect peaks and analyze timing patterns in time series

## Overview

Peak detection identifies local maxima in time series data, useful for finding seasonal highs, demand spikes, or cyclical patterns. Peak timing analysis determines if peaks occur at consistent times within each period.

**Use this document to:**
- Detect peaks with configurable minimum distance and prominence thresholds
- Analyze inter-peak distances to find cyclical patterns
- Determine if peaks occur at consistent times within seasonal periods
- Calculate peak timing variability scores to assess regularity
- Identify products/series with stable vs variable peak patterns

---

## Table Macros

### ts_detect_peaks_by

Detect peaks for multiple series grouped by an identifier.

**Signature:**
```sql
ts_detect_peaks_by(source, group_col, date_col, value_col, params) → TABLE(group_col, peaks, n_peaks, inter_peak_distances, mean_period)
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

**Returns (flat columns — not a nested `peaks` STRUCT despite the signature's `→ TABLE(id, peaks)` shorthand):**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `peaks` | `STRUCT("index" BIGINT, "time" DOUBLE, "value" DOUBLE, prominence DOUBLE)[]` | Array of individual peak info |
| `n_peaks` | BIGINT | Number of peaks detected |
| `inter_peak_distances` | DOUBLE[] | Distances between consecutive peaks |
| `mean_period` | DOUBLE | Mean distance between peaks |

`n_peaks`, `inter_peak_distances`, and `mean_period` are top-level columns, not fields of a
wrapping struct — confirmed via `DESCRIBE SELECT * FROM ts_detect_peaks_by(...)`.

**Example:**
```sql
-- Detect peaks for each product (n_peaks/mean_period are plain top-level columns)
SELECT
    product_id,
    n_peaks,
    mean_period
FROM ts_detect_peaks_by('sales', product_id, date, value, MAP{});

-- With custom parameters (higher prominence threshold)
SELECT * FROM ts_detect_peaks_by(
    'sales', product_id, date, value,
    MAP{'min_distance': '3', 'min_prominence': '10.0'}
);
```

---

### ts_analyze_peak_timing_by

Analyze peak timing regularity for multiple series. Determines if peaks occur at consistent times within each period.

**Signature:**
```sql
ts_analyze_peak_timing_by(source, group_col, date_col, value_col, period, params) → TABLE(group_col, n_peaks, peak_times, variability_score, is_stable)
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

**Returns (flat columns — not a nested `timing` STRUCT despite the signature's `→ TABLE(id, timing)` shorthand):**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `n_peaks` | BIGINT | Number of peaks detected |
| `peak_times` | DOUBLE[] | Timing of each peak within period |
| `variability_score` | DOUBLE | Timing variability (lower = more regular) |
| `is_stable` | BOOLEAN | True if peak timing is consistent |

Confirmed via `DESCRIBE SELECT * FROM ts_analyze_peak_timing_by(...)`.

**Example:**
```sql
-- Analyze weekly peak timing for each product (plain top-level columns)
SELECT
    product_id,
    is_stable,
    variability_score
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});

-- Find products with stable weekly patterns
SELECT product_id
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{})
WHERE is_stable;
```

---

*See also: [Period Detection](05-period-detection.md) | [Decomposition](05a-decomposition.md)*
