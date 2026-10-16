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
- `ts_cv_folds_by` — cross-validation fold generation

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

## Calendar Stepping for Forecast Dates (D-12)

`ts_forecast_by` and `ts_forecast_agg` compute monthly/quarterly/yearly forecast dates
(`'1mo'`/`'3mo'`/`'1q'`/`'1y'`) with a calendar-aware rule instead of fixed-microsecond
arithmetic, so a series ending in a short month does not drift off month-end:

- If every observed date is a month-end, every forecast date is also a month-end (clamped
  to each target month's actual length — e.g. February 29 in a leap year).
- Otherwise the last observed day-of-month is kept, clamped to the target month's length
  (e.g. a series ending on the 30th of a 30-day month forecasts the 30th of each
  following month, including months that have a 30th).
- `'3mo'`/`'1q'` and `'1y'` apply the same rule with 3- and 12-month steps.
- Each step is computed from the last observation, never cumulatively (step *k* is always
  `last_observation_month + k * step_months`, not `step_k-1 + step_months`).

```sql
CREATE OR REPLACE TABLE me AS
SELECT 'A' AS id,
       (last_day(DATE '2023-01-01' + INTERVAL (i) MONTH)) AS ds,
       (100 + i)::DOUBLE AS y
FROM range(11) t(i);

SELECT ds FROM ts_forecast_by('me', id, ds, y, 'Naive', 3, '1mo', MAP{}) ORDER BY forecast_step;
-- ds = 2023-12-31, 2024-01-31, 2024-02-29 (no drift into a 30-day month)
```

`ts_forecast_agg` applies the identical rule with the frequency auto-detected from the
observed dates (no `frequency` argument), and agrees with `ts_forecast_by` on every
forecast date for the same series (D-11 parity).

---

## Frequency Auto-Detection

Passing `NULL` or `''` as `frequency` (`ts_data_quality_by`, `ts_data_quality`,
`ts_stats_by`'s `seasonal_period := 0` path, `ts_forecast_agg`, and any other surface
documented as "auto-detects") derives the step from the series' own dates instead of
a declared frequency:

1. Sort the distinct dates and take every consecutive difference.
2. The **modal** (most common) difference wins. **Ties are broken by the SMALLER
   interval** — deterministic regardless of map/hash iteration order (e.g. alternating
   1-day/2-day steps with equal counts resolve to 1 day, never 2).
3. The resulting step is in **microseconds** (the same unit every SQL/FFI date array
   uses internally — see `detect_frequency`'s doc comment in
   `crates/anofox-fcst-core/src/gaps.rs`).
4. For calendar-stepping surfaces (`ts_forecast_agg`'s date math, and the gap/temporal
   scoring in `ts_data_quality_by`/`ts_stats_by`), the microsecond step is then
   classified: a whole number of days in `28..=31` → monthly, `89..=92` → quarterly,
   `365..=366` → yearly; anything else stays a fixed microsecond step.

**Month-end rule:** once classified as monthly/quarterly/yearly, every date-stepping
calculation (`ts_forecast_by`/`ts_forecast_agg`'s future dates, D-12; `ts_fill_gaps_by`/
`ts_fill_forward_by`'s calendar gap fill, D-09) preserves calendar alignment: if every
observed date in the series is a month-end, every stepped date is also a month-end
(clamped to the target month's actual length, e.g. February 29 in a leap year);
otherwise the day-of-month is kept (also clamped). See [Calendar Stepping for Forecast
Dates](#calendar-stepping-for-forecast-dates-d-12) above and [Data
Preparation](04-data-preparation.md#ts_fill_gaps_by) for worked examples.

---

## Common Use Cases

```sql
-- Setup: weekly_sales(store_id, date, amount), monthly_data(region_id, date, value) --
-- sensor_data and sales are already-shared fixtures (reused as-is).
CREATE OR REPLACE TABLE weekly_sales AS
SELECT store_id, date, revenue AS amount FROM weekly_data;
CREATE OR REPLACE TABLE monthly_data AS
SELECT product_id AS region_id, date, quantity AS value FROM monthly_sales;
```

```sql
-- Daily sales data: fill gaps and compute stats
SELECT * FROM ts_fill_gaps_by('sales', product_id, date, quantity, '1d');
SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Hourly sensor data
SELECT * FROM ts_fill_gaps_by('sensor_data', sensor_id, timestamp, reading, '1h');

-- Weekly aggregated data
SELECT * FROM ts_stats_by('weekly_sales', store_id, date, amount, '1w');

-- Monthly reporting data
SELECT * FROM ts_fill_gaps_by('monthly_data', region_id, date, value, '1mo');

-- Backtesting via two-step cross-validation
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', product_id, date, value, 5, 7, MAP{});
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, value,
    'AutoETS', MAP{'seasonal_period': '7'}
);
```

---

*See also: [Data Preparation](04-data-preparation.md) | [Statistics](03-statistics.md)*
