# Supported Frequencies

> Frequency string formats for time series operations

## Overview

Many functions accept a `frequency` parameter to specify the time interval between observations. This document lists all supported frequency formats.

**Functions using frequency strings:**
- `ts_stats_by` — gap detection requires frequency
- `ts_data_quality_by` — quality assessment with frequency
- `ts_fill_gaps_by` — fill missing timestamps
- `ts_fill_forward_by` — extend series to target date
- `ts_diff_by` — differencing operations
- `ts_backtest_auto_by` — backtesting with step size

---

## Frequency Formats

### Polars Style (Recommended)

| Format | Description | Example |
|--------|-------------|---------|
| `'1d'` | 1 day | Daily data |
| `'1h'` | 1 hour | Hourly data |
| `'30m'` | 30 minutes | Half-hourly data |
| `'1w'` | 1 week | Weekly data |
| `'1mo'` | 1 month | Monthly data |
| `'1q'` | 1 quarter | Quarterly data |
| `'1y'` | 1 year | Yearly data |

Numeric multipliers are supported:

| Format | Description |
|--------|-------------|
| `'7d'` | 7 days |
| `'24h'` | 24 hours |
| `'15m'` | 15 minutes |
| `'2w'` | 2 weeks |
| `'3mo'` | 3 months |
| `'2y'` | 2 years |

---

### DuckDB INTERVAL Syntax

| Format | Description |
|--------|-------------|
| `'1 day'` | 1 day |
| `'1 hour'` | 1 hour |
| `'1 minute'` | 1 minute |
| `'1 week'` | 1 week |
| `'1 month'` | 1 month |
| `'1 year'` | 1 year |
| `'7 days'` | 7 days (plural) |

---

### Raw Integer

| Format | Description |
|--------|-------------|
| `'1'` | Interpreted as 1 day |
| `'7'` | Interpreted as 7 days |
| `'365'` | Interpreted as 365 days |

---

## Common Use Cases

```sql
-- Daily sales data: fill gaps and compute stats
SELECT * FROM ts_fill_gaps_by('sales', product_id, date, quantity, '1d');
SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Hourly sensor data
SELECT * FROM ts_fill_gaps_by('sensors', sensor_id, timestamp, reading, '1h');

-- Weekly aggregated data
SELECT * FROM ts_stats_by('weekly_sales', store_id, date, amount, '1w');

-- Monthly reporting data
SELECT * FROM ts_fill_gaps_by('monthly_data', region_id, date, value, '1mo');

-- Backtesting with daily step
SELECT * FROM ts_backtest_auto_by(
    'sales', product_id, date, value, 7, 5, '1d',
    MAP{'method': 'AutoETS', 'seasonal_period': '7'}
);
```

---

*See also: [Data Preparation](04-data-preparation.md) | [Statistics](03-statistics.md)*
