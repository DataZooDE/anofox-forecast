# Period Detection Examples

> **Finding the rhythm in your data - the key to seasonal forecasting.**

This folder contains runnable SQL examples demonstrating period detection with the anofox-forecast extension.

## Function

| Function | Description |
|----------|-------------|
| `ts_detect_periods_by` | Detect seasonal periods for multiple series |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_period_examples.sql`](synthetic_period_examples.sql) | Multi-series period detection examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/period_detection/synthetic_period_examples.sql
```

---

## Usage

### Basic Period Detection (Auto)

```sql
-- Detect periods using ensemble method (default)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});
```

### FFT-Based Detection

```sql
-- Fast Fourier Transform method - best for clean signals
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'fft'});
```

### ACF-Based Detection

```sql
-- Autocorrelation method - robust to noise
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'acf'});
```

### Autoperiod Detection

```sql
-- Automatic detection algorithm
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'autoperiod'});
```

### Multiple Periods Detection

```sql
-- Detect multiple seasonal patterns
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'multi'});
```

### Accessing Results

```sql
-- Extract specific fields from the result
SELECT
    id,
    (periods).primary_period AS detected_period,
    (periods).method AS method,
    (periods).n_periods AS n_periods_found
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `method` | VARCHAR | 'ensemble' | Detection algorithm: 'fft', 'acf', 'autoperiod', 'multi', 'ensemble' |
| `max_period` | VARCHAR | auto | Maximum period to search for |
| `min_confidence` | VARCHAR | '0.5' | Minimum confidence threshold |

When `MAP{}` is passed (empty), uses ensemble method with defaults.

---

## Output Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | VARCHAR | Series identifier |
| `periods` | STRUCT | Struct containing detection results |

### Periods Struct Fields

| Field | Type | Description |
|-------|------|-------------|
| `primary_period` | INTEGER | Most likely period |
| `method` | VARCHAR | Detection method used |
| `n_periods` | INTEGER | Number of periods detected |

---

## Detection Methods

| Method | Best For | Description |
|--------|----------|-------------|
| `fft` | Long, clean series | Fast Fourier Transform - spectral analysis |
| `acf` | Noisy data | Autocorrelation Function - correlation-based |
| `autoperiod` | Unknown patterns | Automatic period estimation |
| `multi` | Complex seasonality | Detect multiple seasonal patterns |
| `ensemble` | General use | Combines multiple methods (default) |

---

## Key Concepts

### Choosing a Method

| Situation | Recommended Method |
|-----------|-------------------|
| Clean periodic signal | `fft` |
| Noisy data | `acf` |
| Unknown pattern | `ensemble` (default) |
| Multiple seasonalities | `multi` |
| Short series | `autoperiod` |

### Common Periods by Data Frequency

| Data Frequency | Common Periods |
|----------------|----------------|
| Hourly | 24 (daily), 168 (weekly) |
| Daily | 7 (weekly), 30 (monthly), 365 (yearly) |
| Weekly | 52 (yearly) |
| Monthly | 12 (yearly) |

---

## Tips

1. **Start with defaults** - Use `MAP{}` first to see what ensemble method detects.

2. **Verify results** - Plot your data to visually confirm the detected period makes sense.

3. **Minimum data requirement** - Need at least 2x the period length for reliable detection.

4. **Handle noise** - Use `acf` method for noisy data as it's more robust.

5. **Multiple seasonalities** - Use `multi` method if you expect both weekly and monthly patterns.

---

## Related Functions

- `ts_classify_seasonality_by()` - Determine if seasonality is present and its type
- `ts_mstl_decomposition_by()` - Decompose series using detected periods
- `ts_forecast_by()` - Forecast using appropriate seasonal model
