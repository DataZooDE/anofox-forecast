# Peak Detection Examples

> **Peak detection identifies local maxima in time series data.**

This folder contains runnable SQL examples demonstrating peak detection and timing analysis with the anofox-forecast extension.

## Functions

| Function | Description |
|----------|-------------|
| `ts_detect_peaks_by` | Detect local maxima for multiple series |
| `ts_analyze_peak_timing_by` | Analyze timing consistency for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_peak_examples.sql`](synthetic_peak_examples.sql) | Multi-series peak detection examples | Synthetic |
| [`m5_peak_examples.sql`](m5_peak_examples.sql) | Real-world examples with M5 dataset | [M5 Competition](https://www.kaggle.com/c/m5-forecasting-accuracy) |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/peak_detection/synthetic_peak_examples.sql

# Run M5 examples (requires httpfs for remote data)
./build/release/duckdb < examples/peak_detection/m5_peak_examples.sql
```

---

## Usage

### Basic Peak Detection

```sql
-- Detect peaks for all series (default parameters)
SELECT * FROM ts_detect_peaks_by('sales', product_id, date, value, MAP{});

-- With prominence filter (only significant peaks)
SELECT * FROM ts_detect_peaks_by('sales', product_id, date, value,
    MAP{'min_prominence': '0.3'});

-- With minimum distance between peaks
SELECT * FROM ts_detect_peaks_by('sales', product_id, date, value,
    MAP{'min_distance': '7'});

-- Combined filters
SELECT * FROM ts_detect_peaks_by('sales', product_id, date, value,
    MAP{'min_prominence': '0.2', 'min_distance': '7'});
```

### Peak Timing Analysis

```sql
-- Analyze when peaks occur within weekly cycles
SELECT * FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});

-- Use detected period for timing analysis
WITH periods AS (
    SELECT id, (periods).primary_period AS period
    FROM ts_detect_periods_by('sales', product_id, date, value, MAP{})
)
SELECT t.*
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{}) t;
```

---

## Parameters

### ts_detect_peaks_by

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min_prominence` | VARCHAR | `'0'` | Filter peaks by significance (0-1 normalized) |
| `min_distance` | VARCHAR | `'1'` | Minimum observations between peaks |

### ts_analyze_peak_timing_by

| Parameter | Description |
|-----------|-------------|
| `period` | Expected seasonal cycle length (e.g., 7 for weekly) |

---

## Output Columns

### ts_detect_peaks_by

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `n_peaks` | BIGINT | Number of peaks detected |
| `mean_period` | DOUBLE | Average distance between peaks |
| `peaks` | STRUCT[] | Array of peak details (index, value, prominence) |
| `inter_peak_distances` | DOUBLE[] | Distances between consecutive peaks |

### ts_analyze_peak_timing_by

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `n_peaks` | BIGINT | Number of peaks analyzed |
| `mean_timing` | DOUBLE | Average position within cycle (0-1) |
| `std_timing` | DOUBLE | Standard deviation of timing |
| `variability_score` | DOUBLE | 0 = stable, 1 = highly variable |
| `is_stable` | BOOLEAN | TRUE if variability_score < 0.3 |
| `timing_trend` | DOUBLE | Positive = peaks shifting later |

---

## Key Concepts

### Peak Prominence

Prominence measures how much a peak "stands out" from surrounding data (normalized 0-1 scale):

| Prominence Level | Use Case |
|------------------|----------|
| 0 (default) | Find all local maxima |
| 0.1-0.3 | Moderate peaks |
| 0.3-0.5 | Significant peaks only |
| 0.5+ | Only major peaks/anomalies |

### Minimum Distance

Controls peak spacing to prevent detecting minor fluctuations:

| Data Frequency | Typical min_distance |
|----------------|---------------------|
| Hourly | 6-12 (half day) |
| Daily | 5-7 (one week) |
| Weekly | 4 (one month) |

### Variability Score

The variability score (0-1) indicates timing regularity:

| Score Range | Interpretation |
|-------------|----------------|
| < 0.1 | Very consistent |
| 0.1-0.3 | Consistent (`is_stable = TRUE`) |
| 0.3-0.5 | Moderately variable |
| > 0.5 | Highly variable timing |

---

## Tips

1. **Start with no filtering** - Run without parameters first to see all peaks, then add filters.

2. **Use prominence for noise filtering** - Real data often has small fluctuations; prominence > 0.1 usually filters noise.

3. **Detect period first** - Use `ts_detect_periods_by()` to find the actual period before timing analysis.

4. **Check timing stability** - `is_stable = TRUE` means peaks are predictable for scheduling.

5. **Watch for drift** - `timing_trend` shows if peaks are shifting over time.

---

## Related Functions

- `ts_detect_periods_by()` - Detect seasonal periods
- `ts_detect_changepoints_by()` - Detect structural changes
- `ts_mstl_decomposition_by()` - Decompose series before peak detection
