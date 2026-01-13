# Period Detection Examples

> **Finding the rhythm in your data - the key to seasonal forecasting.**

This folder contains runnable SQL examples demonstrating period detection with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_period_examples.sql`](synthetic_period_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/period_detection/synthetic_period_examples.sql
```

---

## Overview

The extension provides 12 period detection algorithms:

| Function | Algorithm | Best For |
|----------|-----------|----------|
| `ts_detect_periods` | Multi-method ensemble | General use |
| `ts_estimate_period_fft` | Fast Fourier Transform | Long, clean series |
| `ts_estimate_period_acf` | Autocorrelation | Noisy data |
| `ts_autoperiod` | Automatic detection | Unknown patterns |
| `ts_cfd_autoperiod` | CFD-based autoperiod | Robust detection |
| `ts_detect_multiple_periods` | Multi-period | Complex seasonality |
| `ts_aic_period` | AIC-based selection | Model-based |
| `ts_ssa_period` | Singular Spectrum Analysis | Short series |
| `ts_stl_period` | STL decomposition | Robust |
| `ts_matrix_profile_period` | Matrix Profile | Anomalies present |
| `ts_sazed_period` | SAZED algorithm | Fast estimation |
| `ts_instantaneous_period` | Time-varying periods | Evolving seasonality |

---

## Patterns Overview

### Pattern 1: Quick Start (ts_detect_periods)

**Use case:** Automatic multi-method period detection.

```sql
SELECT ts_detect_periods(LIST(value ORDER BY ts)) AS periods
FROM my_series;
```

---

### Pattern 2: FFT-Based Detection

**Use case:** Fast detection on long, clean series.

```sql
SELECT ts_estimate_period_fft(LIST(value ORDER BY ts)) AS result
FROM my_series;
```

---

### Pattern 3: ACF-Based Detection

**Use case:** Robust detection on noisy data.

```sql
SELECT ts_estimate_period_acf(LIST(value ORDER BY ts)) AS result
FROM my_series;
```

---

### Pattern 4: Multiple Periods

**Use case:** Data with multiple seasonal patterns.

```sql
SELECT ts_detect_multiple_periods(LIST(value ORDER BY ts)) AS result
FROM my_series;
```

---

### Pattern 5: Compare Methods

**Use case:** Cross-validate period estimates.

```sql
SELECT
    ts_estimate_period_fft(values).period AS fft_period,
    ts_estimate_period_acf(values).period AS acf_period,
    ts_autoperiod(values).period AS auto_period
FROM my_data;
```
