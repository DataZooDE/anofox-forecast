# Peak Detection and Timing Analysis Example

This example demonstrates peak detection and timing analysis using the anofox_forecast DuckDB extension.

## Overview

Peak detection identifies local maxima in time series data, which is essential for:

- **Demand Planning**: Find demand peaks to optimize inventory and staffing
- **Anomaly Detection**: Identify unusual spikes that may indicate problems
- **Seasonality Analysis**: Understand when cyclical peaks occur
- **Signal Processing**: Detect events in sensor and IoT data
- **Financial Analysis**: Find price peaks for trading strategies

## Files

Run the files in order:

| File | Description |
|------|-------------|
| `01_sample_data.sql` | Creates sample datasets (traffic, sensors, sales, heart rate) |
| `02_basic_peak_detection.sql` | Basic `ts_detect_peaks()` usage with various parameters |
| `03_peak_timing_analysis.sql` | `ts_analyze_peak_timing()` for timing consistency analysis |

## Quick Start

```sql
-- Load extension
LOAD anofox_forecast;

-- Run sample data creation
.read examples/peak_detection/01_sample_data.sql

-- Run peak detection examples
.read examples/peak_detection/02_basic_peak_detection.sql

-- Run timing analysis examples
.read examples/peak_detection/03_peak_timing_analysis.sql
```

## Sample Datasets

| Dataset | Description | Rows | Use Case |
|---------|-------------|------|----------|
| `website_traffic` | Hourly visitor counts (7 days) | 168 | Daily pattern analysis |
| `sensor_readings` | Temperature with anomaly spikes | 600 | Anomaly detection |
| `monthly_sales` | Retail sales with seasonality (5 years) | 60 | Seasonal peak analysis |
| `heart_rate` | ECG-like signal (10 seconds) | 1000 | Physiological peak detection |

## Key Functions

### ts_detect_peaks

Detects local maxima in time series data.

```sql
ts_detect_peaks(values DOUBLE[]) → STRUCT
ts_detect_peaks(values DOUBLE[], min_prominence DOUBLE) → STRUCT
ts_detect_peaks(values DOUBLE[], min_prominence DOUBLE, min_distance INTEGER) → STRUCT
```

**Parameters:**
- `values`: Time series values as array
- `min_prominence`: Minimum peak prominence threshold (default: 0.0)
- `min_distance`: Minimum observations between peaks (default: 1)

**Returns:**
```sql
STRUCT(
    peaks STRUCT(index BIGINT, time DOUBLE, value DOUBLE, prominence DOUBLE)[],
    n_peaks BIGINT,
    inter_peak_distances DOUBLE[],
    mean_period DOUBLE
)
```

- `peaks`: Array of peak structs, each containing index, time, value, and prominence
- `n_peaks`: Number of peaks detected
- `inter_peak_distances`: Distances between consecutive peaks
- `mean_period`: Average period between peaks

### ts_analyze_peak_timing

Analyzes peak timing consistency within seasonal cycles.

```sql
ts_analyze_peak_timing(values DOUBLE[], period INTEGER) → STRUCT
```

**Parameters:**
- `values`: Time series values as array
- `period`: Expected seasonal period length

**Returns:**
```sql
STRUCT(
    peak_times DOUBLE[],
    peak_values DOUBLE[],
    normalized_timing DOUBLE[],
    n_peaks BIGINT,
    mean_timing DOUBLE,
    std_timing DOUBLE,
    range_timing DOUBLE,
    variability_score DOUBLE,
    timing_trend DOUBLE,
    is_stable BOOLEAN
)
```

- `peak_times`: Raw time positions of detected peaks
- `peak_values`: Values at peak positions
- `normalized_timing`: Peak positions normalized to 0-1 scale within each cycle
- `n_peaks`: Number of peaks analyzed
- `mean_timing`: Mean normalized timing (0-1)
- `std_timing`: Standard deviation of normalized timing
- `range_timing`: Max - min of normalized timing
- `variability_score`: 0 = stable, 1 = highly variable
- `timing_trend`: Positive = peaks getting later over time
- `is_stable`: TRUE if variability_score < 0.3

## Common Patterns

### Finding Significant Peaks Only

```sql
-- Filter by prominence to ignore minor fluctuations
WITH data_array AS (
    SELECT LIST(value ORDER BY time) AS values FROM my_data
)
SELECT (ts_detect_peaks(values, 100.0)).n_peaks AS significant_peaks
FROM data_array;
```

### Ensuring Minimum Peak Spacing

```sql
-- Require at least 12 observations between peaks
WITH data_array AS (
    SELECT LIST(value ORDER BY time) AS values FROM hourly_data
)
SELECT (ts_detect_peaks(values, 50.0, 12)).peaks AS spaced_peaks
FROM data_array;
```

### Mapping Peaks Back to Timestamps

```sql
WITH data_array AS (
    SELECT LIST(value ORDER BY time) AS values FROM my_data
),
result AS (
    SELECT ts_detect_peaks(values) AS detection FROM data_array
),
peaks_unnested AS (
    SELECT UNNEST(detection.peaks) AS peak FROM result
)
SELECT
    d.*,
    p.peak.prominence
FROM my_data d
JOIN peaks_unnested p ON d.row_number = p.peak.index + 1;  -- +1 for 0-based index
```

### Checking Timing Consistency

```sql
-- Check if weekly peaks are consistent
WITH data_array AS (
    SELECT series_id, LIST(value ORDER BY time) AS values
    FROM daily_data
    GROUP BY series_id
)
SELECT
    series_id,
    (ts_analyze_peak_timing(values, 7)).is_stable AS stable_pattern,
    ROUND((ts_analyze_peak_timing(values, 7)).variability_score, 3) AS variability
FROM data_array;
```

## Parameter Guidelines

### Prominence Selection

Prominence is normalized to a 0-1 scale:

| Prominence Level | Use Case |
|------------------|----------|
| 0 (default) | Find all local maxima |
| 0.01-0.1 | Include minor peaks |
| 0.1-0.3 | Moderate peaks |
| 0.3-0.5 | Significant peaks only |
| 0.5+ | Only major peaks/anomalies |

### Period Selection for Timing Analysis

| Data Frequency | Typical Period |
|----------------|----------------|
| Hourly | 24 (daily pattern) |
| Daily | 7 (weekly) or 30 (monthly) |
| Weekly | 52 (yearly) |
| Monthly | 12 (yearly) |

## Interpreting Results

### Peak Prominence

Prominence measures how much a peak "stands out" from surrounding data:
- **High prominence**: Clear, significant peak
- **Low prominence**: Minor fluctuation, possibly noise

### Variability Score

The variability score (0-1) indicates timing regularity:
- **< 0.1**: Very consistent (peaks at same position each cycle)
- **0.1-0.3**: Consistent (is_stable = TRUE)
- **0.3-0.5**: Moderately variable
- **> 0.5**: Highly variable timing

### Timing Trend

The timing trend indicates if peaks are shifting over time:
- **Positive**: Peaks occurring later in each successive cycle
- **Negative**: Peaks occurring earlier in each successive cycle
- **Near zero**: Stable peak timing

## Related Functions

- `ts_detect_changepoints_by()` - Detect structural changes (different from peaks)
- `ts_detect_seasonality()` - Detect seasonal period automatically
- `ts_detrend()` - Remove trend before peak detection for cleaner results
