---
name: anofox-forecast-detection
description: >
  Seasonality, changepoint, peak, and decomposition detection for the
  anofox_forecast DuckDB extension. Use when identifying seasonal
  periods before configuring seasonal forecasting models, detecting
  structural breaks, analysing peak timing regularity, or decomposing
  a series into trend / seasonal / residual components.
version: 2026.09.26
user-invocable: false
---

# Anofox Forecast — Detection & Decomposition Cheat Sheet

**Extension:** `anofox_forecast` v2026.09.26 (Rust crate `anofox-forecast` v0.16.2) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Detect signal structure. When both `seasonal_period` and `seasonal_periods` are omitted,
`ts_forecast_by` / `ts_cv_forecast_by` / the scalar paths / `ts_forecast_agg` all auto-detect
the seasonal period internally (first detected period, 1 if none found) — same rule on every
surface. Running detection first and passing `seasonal_period` explicitly is still recommended
for control and reproducibility over what silent auto-detection would pick.

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

> ⚠️ **KNOWN BUG (build 9061428): `ts_detect_periods_by` returns all-NULL for FFT-based detection.**
> On clean multi-cycle data (reproduced at 48 points / 4 cycles of a period-12 sine), the `_by` table macro returns a NULL row (`periods = NULL`, `n_periods = NULL`, `primary_period = NULL`) with both `MAP{}` and `MAP{'method': 'fft'}`. The scalar `ts_estimate_period_fft` / `ts_estimate_period_acf` **and** the aggregate `ts_detect_periods_agg` all return `period = 12.0` correctly on the *same* data. **Until the macro is fixed, use the scalar or aggregate path** for period detection.
>
> ```sql
> CREATE TABLE sea AS
> SELECT 'A' AS id, TIMESTAMP '2024-01-01' + INTERVAL (g) HOUR AS ts,
>        10 + 5*sin(2*pi()*g/12) AS y
> FROM range(0,48) t(g);
>
> -- BUG: returns a NULL row
> SELECT * FROM ts_detect_periods_by('sea', id, ts, y, MAP{});
>
> -- WORKAROUND (reliable): period = 12.0
> SELECT ts_detect_periods_agg(ts, y, 'fft') FROM sea;                    -- aggregate
> SELECT ts_estimate_period_fft(LIST(y ORDER BY ts)) FROM sea;           -- scalar
> ```

### `ts_detect_periods_agg` — aggregate period detection

The `GROUP BY` aggregate form of period detection — and the reliable path while the `_by` macro FFT bug (above) is open. Overloads: `ts_detect_periods_agg(ts TIMESTAMP, value DOUBLE)` and `ts_detect_periods_agg(ts, value, method VARCHAR)`. Returns the same STRUCT as `ts_detect_multiple_periods` (`periods[]`, `n_periods`, `primary_period`, `method`).

```sql
-- Setup: sea(id, ts, y) -- same shape as the blockquoted known-bug repro above
-- (restated here as a runnable fence; the blockquoted one above is narrative only).
CREATE OR REPLACE TABLE sea AS
SELECT 'A' AS id, TIMESTAMP '2024-01-01' + INTERVAL (g) HOUR AS ts,
       10 + 5*sin(2*pi()*g/12) AS y
FROM range(0,48) t(g);
```

```sql
SELECT id, ts_detect_periods_agg(ts, y, 'fft').primary_period AS sp
FROM sea GROUP BY id;
```

### Methods available

| Method string | Underlying algorithm | Best for |
|---|---|---|
| `'fft'` | FFT periodogram, linearly detrended first (D-11); picks the strongest bin `3 <= k <= floor(n/2)`, so period is never `n` | Clean signals, fast (default) |
| `'acf'` | `statsmodels`-definition autocorrelation; smallest significant local-max lag `>= 2` (D-11) | Noisy signals, cyclical |
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

`ts_detect_multiple_periods` is a **scalar aggregate** over `LIST(value ORDER BY date)`, used with `GROUP BY`. It returns a STRUCT with fields:
- `periods STRUCT(period DOUBLE, confidence DOUBLE, strength DOUBLE, amplitude DOUBLE, phase DOUBLE, iteration BIGINT, ...)[]`
- `n_periods BIGINT`
- `primary_period DOUBLE`
- `method VARCHAR`

Overloads: `ts_detect_multiple_periods(list)`, `ts_detect_multiple_periods(list, max_periods INTEGER)`, `ts_detect_multiple_periods(list, max_periods, min_confidence DOUBLE, min_strength DOUBLE)`.

```sql
-- Scalar-aggregate GROUP BY form (canonical)
SELECT product_id,
       (mp).primary_period,
       (mp).n_periods,
       unnest((mp).periods).period AS detected_period,
       unnest((mp).periods).confidence AS confidence
FROM (
    SELECT product_id,
           ts_detect_multiple_periods(LIST(y ORDER BY ds)) AS mp
    FROM sales
    GROUP BY product_id
)
ORDER BY product_id;
```

Alternatively, use the `ts_detect_periods_by` table macro with `MAP{'method': 'multi'}` — same result, one-liner:

```sql
-- Setup: dual_seasonal(series_id, ds, value), 120 days, weekly + monthly pattern
-- (deterministic variant of examples/period_detection/synthetic_period_examples.sql's
-- Section 4 fixture -- that one uses RANDOM(), this one doesn't).
CREATE OR REPLACE TABLE dual_seasonal AS
SELECT
    'dual_pattern' AS series_id,
    DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
    100.0 + 30.0 * sin(2 * pi() * i / 7.0) + 15.0 * sin(2 * pi() * i / 30.0) AS value
FROM range(0, 120) t(i);
```

```sql
-- Table-macro form via ts_detect_periods_by (source: synthetic_period_examples.sql lines 175-179)
SELECT series_id, primary_period, n_periods
FROM ts_detect_periods_by('dual_seasonal', series_id, ds, value, MAP{'method': 'multi'});
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

### `ts_detect_changepoints_by` — Bayesian Online Changepoint Detection (Adams & MacKay 2007)

```sql
ts_detect_changepoints_by(source, group_col, date_col, value_col, params) → TABLE
```

Params:
| Key | Default | Description |
|---|---|---|
| `hazard_lambda` | 250.0 | Expected run length between changepoints. Lower → more changepoints (and more false alarms) |

Returns one row per input point with `<group_col>`, `<date_col>`, `is_changepoint BOOLEAN`,
and `changepoint_probability DOUBLE` only — the value column is **not** included in the
output.

```sql
-- Detect changepoints (value column is NOT in the output; select from the source if needed)
SELECT product_id, ds, is_changepoint, changepoint_probability
FROM ts_detect_changepoints_by('sales', product_id, ds, y,
    MAP{'hazard_lambda': '100'})
WHERE is_changepoint;
```

Note: the single-series `ts_detect_changepoints` (no `group_col`) has a **different** output
shape that DOES include the value column: `(date_col, value_col, is_changepoint,
changepoint_probability)`. It is not a simple alias of the `_by` form.

Also: `ts_detect_changepoints` (scalar) and `ts_detect_changepoints_agg` (aggregate).

**Algorithm (Phase 15 plan 15-03, DET-02):** the series is standardised first
(`z = (x - median(x)) / sigma_hat`, `sigma_hat` a MAD-of-first-differences robust
scale), making detection scale/shift invariant. The run-length posterior uses a
fully normalised Student-t predictive and a Normal-Gamma update on each run's
sample mean; run lengths beyond 500 are merged (not dropped) into the last kept
entry. A changepoint is flagged on a MAP run-length *reset* (the point that
started the new, shorter run) — `changepoint_probability` is the separate
quantity `P(r_t=1)` (this point opened a brand-new run). Measured on 100
independent `N(0,1)` series (no real changepoint): `hazard_lambda=250` (default)
flags ~20 false alarms total, `500` flags ~11, `1000` flags ~5 — while a 3-sigma
step is still detected within 3 samples in ~94/100 cases at every one of those
hazard values. See `docs/api/06-changepoint-detection.md` for the full writeup.

**PELT has no SQL surface.** The crate's PELT implementation
(`anofox_fcst_core::changepoint::detect_changepoints`) is sigma-normalised and
exact-safe-pruned, but is Rust/FFI-only — its SQL wrapper was removed as dead
code in Phase 12, and `ts_detect_changepoints` always dispatches to BOCPD above.

## Seasonality analysis — classify / measure strength

### `ts_classify_seasonality_by` — timing / modulation / strength

```sql
ts_classify_seasonality_by(source, group_col, date_col, value_col, period DOUBLE) → TABLE
```

Output columns (group column preserved):

| Column | Type | Description |
|---|---|---|
| `<group_col>` | (input) | Preserved group column |
| `timing_classification` | VARCHAR | `'stable_seasonal'` / `'variable_timing'` / `'intermittent_seasonal'` / `'non_seasonal'` (NOT `Regular`/`Weekly`/`Irregular` — that is not the real label set) |
| `modulation_type` | VARCHAR | `'stable'` / `'emerging'` / `'fading'` / `'oscillating'` / `'non_seasonal'` (NOT `Additive`/`Multiplicative`/`None` — that is not the real label set) |
| `has_stable_timing` | BOOLEAN | Peak timing regularity |
| `timing_variability` | DOUBLE | Numeric variability score |
| `seasonal_strength` | DOUBLE | ∈ [0, 1] |
| `is_seasonal` | BOOLEAN | Overall verdict |
| `cycle_strengths` | DOUBLE[] | Per-cycle strengths |
| `weak_seasons` | BIGINT[] | Indices of weak seasonal peaks |

**Phase 15:** a clean, clearly-seasonal series (e.g. a sine wave at the correct period) now
correctly comes back as `timing_classification = 'stable_seasonal'`, `is_seasonal = true`,
with `seasonal_strength` close to 1.0 (previously a fdars-core dimension-swap bug made every
call return `'non_seasonal'` / `NaN`).

```sql
SELECT product_id, timing_classification, modulation_type, is_seasonal, seasonal_strength
FROM ts_classify_seasonality_by('sales', product_id, ds, y, 7.0);
```

Scalar variants: `ts_classify_seasonality`, `ts_classify_seasonality_agg`.

### `ts_seasonal_strength`, `ts_seasonal_strength_windowed`, `ts_analyze_seasonality`

Numeric measures over `LIST(y ORDER BY ds)`. `windowed` variant tracks strength changes across the series.

`method` is `'variance'` (default, alias `var`) or `'spectral'` (alias `spec`, `fft`). Both
LINEARLY DETREND the series first (D-12) — a trend + seasonal series previously measured
`variance` strength `~0.003` on the raw series; detrended it reports `>= 0.95`. `'wavelet'` is **removed** (it returned `1.0` even for white noise in fdars-core 0.47) and now raises an
error naming the two remaining methods; any other unrecognized method name, or a non-positive
`period`, also raises. `ts_analyze_seasonality`'s `trend_strength` is `|r|` (absolute
correlation with time, the square root of the linear-fit `R^2`).

### `ts_detect_seasonality`, `ts_detect_seasonality_changes`

Detect the presence and any regime shifts in the seasonal pattern.

## Peak detection & timing

### `ts_detect_peaks_by`

```sql
ts_detect_peaks_by(source, group_col, date_col, value_col, params) → TABLE(group_col, peaks, n_peaks, inter_peak_distances, mean_period)
```

Returns flat top-level columns `n_peaks` / `inter_peak_distances` / `mean_period` alongside
`peaks` (the array of individual peak info) — NOT a single nested `peaks` STRUCT wrapping
everything.

**Algorithm (Phase 15, DET-01):** a direct port of `scipy.signal.find_peaks` 1.18.1
(`wlen=None`), not a normalised-score heuristic. `prominence` is on the **same scale as the
input data** (not `0..1`); a plateau reports its midpoint index; `min_distance` <= `1.0`
(the macro default) applies no distance filter. See `docs/api/05b-peak-detection.md` for the
full parameter mapping.

### `ts_analyze_peak_timing_by`

```sql
ts_analyze_peak_timing_by(source, group_col, date_col, value_col, period, params) → TABLE
```

Measures how tightly peaks cluster at a specific phase of the seasonal cycle. Useful for retail (do peaks land on the same day of the week?).

Also: `ts_detect_peaks`, `ts_analyze_peak_timing`, `ts_detect_amplitude_modulation`.

## Decomposition

### `ts_mstl_decomposition_by` — Multiple Seasonal-Trend decomposition (LOESS)

```sql
ts_mstl_decomposition_by(source, group_col, date_col, value_col, params) → TABLE(group_col, trend, seasonal, remainder, periods)
```

**Algorithm (Phase 15, D-09):** matches R `forecast::mstl`. `params.seasonal_periods` (alias
`periods`, a `'[7, 365]'` string or `[7, 365]` list) honours every period that needs fewer
than `n/2` points — those that don't are silently dropped (R's own behaviour); survivors are
sorted ascending and fit in two passes (`s.window = 7 + 4*position`-stepped STL, trend from
the last fit). With `seasonal_periods` omitted or every requested period too large, the trend
is `stats::supsmu` (non-seasonal fallback) and `seasonal`/`periods` come back empty. There is
no `seasonal_1`/`seasonal_2`-style per-period column naming — `seasonal` is `DOUBLE[][]`,
one array per entry of `periods`, same order.

```sql
-- One row per group with array-valued trend/seasonal/remainder (no per-point date column)
SELECT product_id, trend, seasonal, remainder, periods
FROM ts_mstl_decomposition_by('sales', product_id, ds, y, MAP{'seasonal_periods': '[7, 365]'});

-- No seasonal_periods -> non-seasonal supsmu trend, seasonal/periods come back empty
SELECT periods FROM ts_mstl_decomposition_by('sales', product_id, ds, y, MAP{});
-- []
```

### `ts_decompose_seasonal` (scalar) — fdars LOESS+Fourier, or R classical (centred moving-average)

Signature `ts_decompose_seasonal(DOUBLE[] values, DOUBLE period [, VARCHAR method]) → STRUCT(trend DOUBLE[], seasonal DOUBLE[], remainder DOUBLE[], period DOUBLE, method VARCHAR)`.
`method`: `'additive'` (alias `'add'`, default, `data = trend + seasonal + remainder`) or
`'multiplicative'` (alias `'mult'`/`'mul'`, `data = trend * seasonal * remainder`) — fdars
LOESS trend (bandwidth 0.3, degree 2) + a 3-harmonic Fourier seasonal fit to the detrended
residual; or `'classical_additive'` (alias `'classical'`) / `'classical_multiplicative'` — R
`stats::decompose`'s centred moving-average trend (Phase 15, D-10): a `2 x period` filter for
an even period, the plain `period`-term mean for an odd period; `trend`/`remainder` are `NaN`
(check with `isnan(...)`, not `IS NULL`) at the `floor(period/2)` points at each end, matching
R's own `NA` edges exactly. `classical_*` requires an INTEGER `period >= 2`;
`classical_multiplicative` additionally requires every value `> 0` (unlike R, which would
silently divide by a non-positive value). Any other method name, or a non-positive `period`,
is a SQL error naming the valid choices.

### `ts_detrend_by`

Remove linear or polynomial trend:

```sql
ts_detrend_by(source, group_col, date_col, value_col, method) → TABLE
```

`method`: `'linear'`; `'quadratic'` (alias `'polynomial'`, `'poly'`, degree 2); `'cubic'`
(degree 3); `'diff'` (alias `'diff1'`, `'difference'`); `'diff2'`; `'loess'` (alias
`'lowess'`); `'auto'` (default — AIC-selects among linear/quadratic/cubic/loess). Any other
name is a SQL error naming every valid choice — there is no silent fallback. `'loess'` and
`'auto'` are `O(n^2)` in series length; prefer `'linear'`/`'quadratic'`/`'cubic'` on long
series.

### `ts_detrend` (scalar) — array-level trend removal

The array-level primitive behind `ts_detrend_by`. Signature `ts_detrend(DOUBLE[] values [, VARCHAR method]) → STRUCT(trend DOUBLE[], detrended DOUBLE[], method VARCHAR, coefficients DOUBLE[], rss DOUBLE, n_params BIGINT)`. `coefficients` for `'quadratic'`/`'cubic'` are ascending powers of the raw 0-based time index. **For grouped series prefer `ts_detrend_by`** — it handles the per-group `GROUP BY` shape and is the documented path.

## Gotchas

- **Single-series NULL policy (Phase 12 D-03/D-04) applies to every period/seasonality/diagnostic/
  decomposition/peak/changepoint function** (period estimators, `ts_detect_seasonality`/
  `ts_analyze_seasonality`/`ts_classify_seasonality`, the diagnostics in
  `anofox-forecast-backtest`'s `ts_adf`/`ts_kpss`/etc. family, `ts_detrend`/`ts_decompose_seasonal`/
  `ts_seasonal_strength`/`ts_seasonal_strength_windowed`/`ts_instantaneous_period`,
  `ts_detect_peaks`/`ts_analyze_peak_timing`, `_ts_mstl_decomposition`, and
  `_ts_detect_changepoints_bocpd`): leading/trailing NULLs are trimmed before the analysis, an
  interior NULL is a hard error naming its 1-based position (e.g. `interior NULL at position 6`),
  and an all-NULL list returns SQL NULL rather than erroring. Per-observation array outputs
  (`trend`, `detrended`, `seasonal`, peak `index`, `is_changepoint`, ...) describe the **trimmed
  series** — position 1 is the first non-NULL input value, not the original row 0; the
  single-series `ts_detect_changepoints` macro shifts its row-level output back by the
  leading-NULL row count so those rows get NULL flags. Impute interior gaps first
  (e.g. `ts_fill_nulls_mean_by`) if your series has them; wrap in `try(...)` to convert the error
  to NULL instead of failing the query.
- **Seasonality auto-detection applies uniformly across surfaces.** `AutoETS` / `AutoARIMA` / `AutoTheta` (and other models) via `ts_forecast_by`, `ts_cv_forecast_by`, the scalar paths, and `ts_forecast_agg` all auto-detect when `seasonal_period`/`seasonal_periods` are both omitted (10-10, FCST-06). Run detection first and pass `seasonal_period` explicitly if you want control/reproducibility rather than relying on silent auto-detection.
- **`ts_detect_periods_by` returns one row per group** with a `periods` STRUCT. Access fields with `primary_period`.
- **Detection needs sufficient history**: FFT-family methods need ≥ 2 full cycles; ACF-family needs ≥ 3. On short-history panels, `ts_detect_periods` may return `primary_period = 1` (no seasonality) even when a period exists.
- **`_ts_mstl_decomposition`/`ts_mstl_decomposition_by` return `periods` ascending, de-duplicated** — if you request `[28, 7]` you get `periods = [7, 28]` back, with `seasonal[1]` corresponding to period 7 and `seasonal[2]` to period 28, not the request order.
- **FFT/ACF/regression period estimators and seasonal-strength methods were fixed in Phase 15
  (D-11, D-12):** `ts_estimate_period_fft` now linearly detrends first and never returns
  `period = n` or `>= n/2` (previously a trend + seasonal series reported period `= n`);
  `ts_estimate_period_acf` can now return lag 2 and requires statistical significance
  (previously the search started at lag 3 with no significance test); `_ts_detect_periods(...,
  'regression')`'s default grid is every integer, so an exact period like 12 is never skipped.
  `ts_seasonal_strength`/`ts_seasonal_strength_windowed` now detrend first — a trend-dominated
  series that is ALSO strongly seasonal now correctly reports a high strength instead of a
  near-zero one. `'wavelet'` as a strength method is **removed** (always returned `1.0`,
  even for white noise); use `'variance'` (default) or `'spectral'`.

## Canonical detection pipeline

```sql
-- 1. Detect periods across the panel (use the aggregate form, not ts_detect_periods_by --
-- see the KNOWN BUG callout above: the _by macro returns all-NULL on this build for every
-- method, not just 'fft'; ts_detect_periods_agg is the reliable path)
CREATE OR REPLACE TABLE detected AS
SELECT product_id,
       ts_detect_periods_agg(ds, y, 'fft').primary_period AS sp,
       ts_detect_periods_agg(ds, y, 'fft').periods[1].confidence AS conf
FROM sales
GROUP BY product_id;

-- 2. Classify seasonality mode at the modal period (real column names are
-- timing_classification / modulation_type, not classification / confidence)
CREATE OR REPLACE TABLE modes AS
SELECT product_id, timing_classification, modulation_type
FROM ts_classify_seasonality_by('sales', product_id, ds, y,
    (SELECT mode() WITHIN GROUP (ORDER BY sp) FROM detected));

-- 3. Optional: flag changepoints for post-hoc review
CREATE OR REPLACE TABLE breaks AS
SELECT product_id, ds, is_changepoint
FROM ts_detect_changepoints_by('sales', product_id, ds, y, MAP{'hazard_lambda': '250'})
WHERE is_changepoint;
```

See also: `anofox-forecast-data-prep` (fill gaps before detection — needs regular grid), `anofox-forecast-eda` (`ts_stats`'s STL-based `trend_strength` / `seasonality_strength`, D-02, gate the need for detection — 0.3/0.6 rule-of-thumb thresholds, consistent with this skill's own strength measures), `anofox-forecast-models` (pass detected `seasonal_period` to `ts_forecast_by` or to `ts_stats`'s own `seasonal_period` parameter).

Reference docs:
- `docs/api/05-period-detection.md`
- `docs/api/05a-decomposition.md`
- `docs/api/05b-peak-detection.md`
- `docs/api/06-changepoint-detection.md`
