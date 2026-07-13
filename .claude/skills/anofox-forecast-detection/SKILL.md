---
name: anofox-forecast-detection
description: >
  Seasonality, changepoint, peak, and decomposition detection for the
  anofox_forecast DuckDB extension. Use when identifying seasonal
  periods before configuring seasonal forecasting models, detecting
  structural breaks, analysing peak timing regularity, or decomposing
  a series into trend / seasonal / residual components.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — Detection & Decomposition Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Detect signal structure — **seasonality is not auto-detected by the forecasters**; you must run detection first and pass `seasonal_period` explicitly to `ts_forecast_by`.

## Requires: json extension

Detection functions use the `json` extension for parameter marshalling. Enable auto-load once per session:

```sql
SET autoinstall_known_extensions = 1;
SET autoload_known_extensions = 1;
```

## Period detection — 12 methods

### `ts_detect_periods_by` — primary entry point

```sql
ts_detect_periods_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN,
                     params MAP/STRUCT) → TABLE
```

Output columns (group column name is preserved):

| Column | Type | Description |
|---|---|---|
| `<group_col>` | (input type) | Preserved group column (e.g. `product_id`) |
| `periods` | `STRUCT(period, confidence, strength, amplitude, phase, iteration, ...)[]` | Array of detected periods |
| `n_periods` | BIGINT | Count of detected periods |
| `primary_period` | DOUBLE | Top-level primary period (convenience, avoids struct indexing) |
| `method` | VARCHAR | Method that produced the result |

Params:

| Key | Default | Description |
|---|---|---|
| `method` | `'fft'` | Detection method (see table below) |
| `max_period` | series length / 2 | Upper bound on detected period |
| `min_period` | 2 | Lower bound |

```sql
-- Default (FFT) — use the top-level primary_period column
SELECT product_id, primary_period, method
FROM ts_detect_periods_by('sales', product_id, ds, y, MAP{});

-- Autoperiod (FFT + ACF validation) — access full period list via struct-array unnest
SELECT product_id, method, unnest(periods).period AS p, unnest(periods).confidence AS conf
FROM ts_detect_periods_by('sales', product_id, ds, y,
    MAP{'method': 'autoperiod'});
```

### Methods available

| Method string | Underlying algorithm | Best for |
|---|---|---|
| `'fft'` | FFT periodogram | Clean signals, fast (default) |
| `'acf'` | Autocorrelation | Noisy signals, cyclical |
| `'autoperiod'` | FFT + ACF cross-check | General purpose, robust |
| `'aic'` | AIC criterion | Model-selection style |
| `'lomb_scargle'` | Lomb-Scargle | Irregularly sampled data |
| `'sazed'` | SAZED | Ensemble of methods |
| `'stl'` | STL decomposition | Trend + seasonal separation |
| `'ssa'` | Singular Spectrum Analysis | Multi-component |
| `'matrix_profile'` | Matrix profile | Motif-based |
| `'cfd_autoperiod'` | Clipped-FD autoperiod | Robust variant |
| `'instantaneous'` | Instantaneous frequency | Time-varying periods |
| `'auto'` | Auto-select | Unknown data |

Also available as scalar functions over `LIST(y ORDER BY ds)`: `ts_autoperiod`, `ts_cfd_autoperiod`, `ts_aic_period`, `ts_lomb_scargle`, `ts_ssa_period`, `ts_stl_period`, `ts_sazed_period`, `ts_matrix_profile_period`, `ts_estimate_period_fft`, `ts_estimate_period_acf`, `ts_instantaneous_period`.

### `ts_detect_multiple_periods` — multi-seasonal series

Some series have both weekly (7) and yearly (365) seasonality — use this for hourly / high-frequency data.

```sql
SELECT id, unnest(periods) AS p
FROM ts_detect_multiple_periods_by('sales', product_id, ds, y, MAP{});
```

## Detect-then-forecast workflow (the standard pattern)

```sql
-- Step 1: Detect per-series period (use the top-level primary_period column)
CREATE OR REPLACE TABLE periods AS
SELECT product_id, primary_period AS sp
FROM ts_detect_periods_by('sales', product_id, ds, y, MAP{});

-- Step 2: Forecast with per-group detected period
--   Common: pick the mode across the panel, apply uniformly
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM ts_forecast_by('sales', product_id, ds, y,
    'AutoETS', 14, '1d',
    MAP{'seasonal_period': (SELECT mode() WITHIN GROUP (ORDER BY sp) FROM periods)::VARCHAR}
);
```

## Changepoint detection

### `ts_detect_changepoints_by` — Bayesian Online Changepoint Detection (BOCD)

```sql
ts_detect_changepoints_by(source, group_col, date_col, value_col, params) → TABLE
```

Params:
| Key | Default | Description |
|---|---|---|
| `hazard_lambda` | 250.0 | Hazard rate. Lower → more changepoints |

Returns one row per input point with `is_changepoint BOOLEAN` and `changepoint_probability DOUBLE`.

```sql
-- Detect changepoints
SELECT product_id, ds, y, is_changepoint, changepoint_probability
FROM ts_detect_changepoints_by('sales', product_id, ds, y,
    MAP{'hazard_lambda': '100'})
WHERE is_changepoint;
```

Also: `ts_detect_changepoints` (scalar) and `ts_detect_changepoints_agg` (aggregate).

## Seasonality analysis — classify / measure strength

### `ts_classify_seasonality_by` — timing / modulation / strength

```sql
ts_classify_seasonality_by(source, group_col, date_col, value_col, period DOUBLE) → TABLE
```

Output columns (group column preserved):

| Column | Type | Description |
|---|---|---|
| `<group_col>` | (input) | Preserved group column |
| `timing_classification` | VARCHAR | e.g. `Regular`, `Weekly`, `Irregular` |
| `modulation_type` | VARCHAR | `Additive` / `Multiplicative` / `None` |
| `has_stable_timing` | BOOLEAN | Peak timing regularity |
| `timing_variability` | DOUBLE | Numeric variability score |
| `seasonal_strength` | DOUBLE | ∈ [0, 1] |
| `is_seasonal` | BOOLEAN | Overall verdict |
| `cycle_strengths` | DOUBLE[] | Per-cycle strengths |
| `weak_seasons` | BIGINT[] | Indices of weak seasonal peaks |

```sql
SELECT product_id, timing_classification, modulation_type, is_seasonal, seasonal_strength
FROM ts_classify_seasonality_by('sales', product_id, ds, y, 7.0);
```

Scalar variants: `ts_classify_seasonality`, `ts_classify_seasonality_agg`.

### `ts_seasonal_strength`, `ts_seasonal_strength_windowed`, `ts_analyze_seasonality`

Numeric measures over `LIST(y ORDER BY ds)`. `windowed` variant tracks strength changes across the series.

### `ts_detect_seasonality`, `ts_detect_seasonality_changes`

Detect the presence and any regime shifts in the seasonal pattern.

## Peak detection & timing

### `ts_detect_peaks_by`

```sql
ts_detect_peaks_by(source, group_col, date_col, value_col, params) → TABLE
```

Returns detected peak indices and values per group.

### `ts_analyze_peak_timing_by`

```sql
ts_analyze_peak_timing_by(source, group_col, date_col, value_col, period, params) → TABLE
```

Measures how tightly peaks cluster at a specific phase of the seasonal cycle. Useful for retail (do peaks land on the same day of the week?).

Also: `ts_detect_peaks`, `ts_analyze_peak_timing`, `ts_detect_amplitude_modulation`.

## Decomposition

### `ts_mstl_decomposition_by` — Multiple Seasonal-Trend decomposition (LOESS)

```sql
ts_mstl_decomposition_by(source, group_col, date_col, value_col, params) → TABLE
```

`seasonal_periods` goes INSIDE the params (JSON-string form): `MAP{'seasonal_periods': '[7, 365]'}`. Returns `trend`, `seasonal_<i>`, `remainder` per point.

```sql
SELECT product_id, ds, trend, seasonal_1, remainder
FROM ts_mstl_decomposition_by('sales', product_id, ds, y,
    MAP{'seasonal_periods': '[7]'});
```

### `ts_decompose_seasonal` (scalar) — classical additive / multiplicative

### `ts_detrend_by`

Remove linear or polynomial trend:

```sql
ts_detrend_by(source, group_col, date_col, value_col, method) → TABLE
```

`method`: `'linear'` (default), `'polynomial'`, `'ols'`.

## Gotchas

- **Seasonality is NOT auto-detected by forecasters.** `AutoETS` / `AutoARIMA` / `AutoTheta` accept a `seasonal_period` param — if you don't set it, they select non-seasonal variants. Run detection first, then pass explicitly.
- **`ts_detect_periods_by` returns one row per group** with a `periods` STRUCT. Access fields with `primary_period`.
- **Detection needs sufficient history**: FFT-family methods need ≥ 2 full cycles; ACF-family needs ≥ 3. On short-history panels, `ts_detect_periods` may return `primary_period = 1` (no seasonality) even when a period exists.
- **`ts_mstl_decomposition_by` seasonal_periods is a JSON string** (`'[7, 365]'`), not a native array.

## Canonical detection pipeline

```sql
-- 1. Detect periods across the panel
CREATE OR REPLACE TABLE detected AS
SELECT id, primary_period AS sp, (periods).confidence AS conf
FROM ts_detect_periods_by('sales', product_id, ds, y, MAP{'method': 'autoperiod'});

-- 2. Classify seasonality mode (additive vs multiplicative) at the modal period
CREATE OR REPLACE TABLE modes AS
SELECT id, classification, confidence
FROM ts_classify_seasonality_by('sales', product_id, ds, y,
    (SELECT mode() WITHIN GROUP (ORDER BY sp) FROM detected));

-- 3. Optional: flag changepoints for post-hoc review
CREATE OR REPLACE TABLE breaks AS
SELECT product_id, ds, is_changepoint
FROM ts_detect_changepoints_by('sales', product_id, ds, y, MAP{'hazard_lambda': '250'})
WHERE is_changepoint;
```

See also: `anofox-forecast-data-prep` (fill gaps before detection — needs regular grid), `anofox-forecast-eda` (`trend_strength` / `seasonality_strength` gate the need for detection), `anofox-forecast-models` (pass detected `seasonal_period` to `ts_forecast_by`).

Reference docs:
- `docs/api/05-period-detection.md`
- `docs/api/05a-decomposition.md`
- `docs/api/05b-peak-detection.md`
- `docs/api/06-changepoint-detection.md`
