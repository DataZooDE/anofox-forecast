# Decomposition

> Seasonal decomposition and classification

## Overview

Decomposition functions separate time series into trend, seasonal, and residual components. Classification functions analyze the type and strength of seasonality.

**Use this document to:**
- Decompose series into trend, seasonal, and residual components using MSTL
- Remove trend from series using polynomial fitting (linear, quadratic, cubic)
- Handle multiple seasonal patterns (e.g., weekly + yearly) simultaneously
- Classify seasonality type: timing (early/on_time/late), modulation (stable/growing/shrinking)
- Measure seasonal strength to decide if seasonal models are appropriate
- Identify weak or variable seasonal cycles within your data

**NULL handling (Phase 12, D-03/D-04):** `ts_detrend`, `ts_decompose_seasonal`,
`ts_seasonal_strength`, `ts_seasonal_strength_windowed`, `ts_instantaneous_period` and
`_ts_mstl_decomposition` all share the Rust single-series policy: leading/trailing `NULL`
(and `NaN`) values inside the `LIST(...)` are trimmed before the analysis runs; an interior
`NULL` that survives trimming is a hard error naming its 1-based position in the original
list (e.g. `ts_detrend: interior NULL at position 6`); a list with no non-NULL value
returns SQL `NULL`. Per-observation array outputs (`trend`, `detrended`, `seasonal`,
`remainder`, `periods`, `frequencies`, `amplitudes`, ...) describe the **trimmed series** —
position 1 of every output array corresponds to the first non-NULL input value, not the
original row 0.

```sql
-- Edge trim: leading/trailing NULLs give the same result as the trimmed series.
SELECT ts_detrend([NULL, 1.0, 2.0, 1.5, 2.5, 1.8, NULL]::DOUBLE[], 'linear')
     = ts_detrend([1.0, 2.0, 1.5, 2.5, 1.8]::DOUBLE[], 'linear');
-- true
```

---

## Table Macros

### ts_mstl_decomposition_by

Multiple Seasonal-Trend decomposition using Loess (MSTL).

**Signature (5 args — `seasonal_periods` is a key in `params`, not a positional argument):**
```sql
ts_mstl_decomposition_by(source, group_col, date_col, value_col, params)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | IDENTIFIER | Series grouping column |
| `date_col` | IDENTIFIER | Date/timestamp column |
| `value_col` | IDENTIFIER | Value column |
| `params` | MAP | `seasonal_periods` (alias `periods`) — a list of seasonal periods, e.g. `'[7, 365]'` (string) or `[7, 365]` (list); `insufficient_data` — `'fail'` (default), `'trend'`, or `'none'` |

**Algorithm (Phase 15, D-09):** matches R `forecast::mstl` exactly. Of the requested
periods, those needing `n/2` or more points are dropped (R's own `msts` warning, silently
suppressed here); the survivors are sorted ascending and de-duplicated. With `k >= 1`
usable periods, two passes are made over them in ascending order — each pass adds back the
period's own previous seasonal estimate, re-fits STL with `s.window = 7 + 4*(position+1)`
(so `11, 15, 19, ...`), and subtracts the new estimate before the next period; the returned
`trend` is from the LAST STL fit, `remainder` is the fully-deseasonalised series minus that
trend. With NO usable period (`seasonal_periods` omitted/empty, or every requested period
too large for the series), the trend is `stats::supsmu(seq_len(n), x)` — R `forecast::mstl`'s
own non-seasonal fallback — and `seasonal`/`periods` come back empty, `remainder = value -
trend`. `insufficient_data` only matters when periods WERE requested and NONE of them is
usable: `'fail'` raises a SQL error, `'trend'` falls back to the non-seasonal `supsmu` trend,
`'none'` returns every output column `NULL`.

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Group identifier |
| `trend` | DOUBLE[] | Trend component (STL, or `supsmu` when no period is usable) |
| `seasonal` | DOUBLE[][] | One array per usable period, same order as `periods` |
| `remainder` | DOUBLE[] | Residual after removing trend and every seasonal component |
| `periods` | INTEGER[] | The usable periods actually fit, ascending, de-duplicated (empty if none) |

**Example:**
```sql
-- Weekly + yearly seasonality
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, quantity,
    MAP{'seasonal_periods': '[7, 365]'}
);

-- No seasonal_periods -> non-seasonal supsmu trend, seasonal/periods come back empty
SELECT periods FROM ts_mstl_decomposition_by('sales', product_id, date, quantity, MAP{});
-- []
```

---

### ts_mstl_decomposition_long_by

Long form of `ts_mstl_decomposition_by` (#282): one row per `(group, date, component)`,
`<group_col>, <date_col>, component, value`, with `component` in `observed`, `trend`,
`seasonal_<period>` (one per period) and `remainder` — the anofox contract `decomposition`
shape. Same `params` as `ts_mstl_decomposition_by`; values are the same list elements, mapped
to their source dates.

```sql
SELECT * FROM ts_mstl_decomposition_long_by('sales', product_id, date, quantity,
    params := MAP{'seasonal_periods': '[7]'});
```

### ts_detrend_by

Remove trend from grouped time series using polynomial fitting.

**Signature:**
```sql
ts_detrend_by(source, group_col, date_col, value_col, method)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | IDENTIFIER | Series grouping column |
| `date_col` | IDENTIFIER | Date/timestamp column |
| `value_col` | IDENTIFIER | Value column |
| `method` | VARCHAR | Detrending method: 'linear', 'quadratic', 'cubic', or 'auto' |

**Methods (Phase 15):** `'linear'`; `'quadratic'` (alias `'polynomial'`, `'poly'`, degree 2);
`'cubic'` (degree 3); `'diff'` (alias `'diff1'`, `'difference'`); `'diff2'`; `'loess'` (alias
`'lowess'`); `'auto'` (default — evaluates linear, quadratic, cubic, and loess by AIC and
reports the chosen one as e.g. `method = 'auto(polynomial(3))'`). Any other method name is a
SQL error naming every valid choice (e.g. `expected one of: linear, quadratic (alias
polynomial, poly), cubic, diff (alias diff1, difference), diff2, loess (alias lowess),
auto`) — there is no silent fallback. `coefficients` for `'quadratic'`/`'cubic'` are ascending
powers of the raw 0-based time index (`coefficients[k]` is the coefficient of `t^k`, `t =
0..n-1`), not fdars-core's internal normalised-time basis. `'loess'` and `'auto'` (which
evaluates a loess fit) are `O(n^2)` in series length — prefer `'linear'`/`'quadratic'`/
`'cubic'` on long series (tens of thousands of points) if `'auto'`'s cost is a concern.

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Group identifier |
| `trend` | DOUBLE[] | Fitted trend component |
| `detrended` | DOUBLE[] | Original values minus trend |
| `method` | VARCHAR | Method used (may differ from input when using 'auto') |
| `coefficients` | DOUBLE[] | Polynomial coefficients |
| `rss` | DOUBLE | Residual sum of squares |
| `n_params` | BIGINT | Number of parameters fitted |

**Example:**
```sql
-- Auto-detect and remove trend
SELECT * FROM ts_detrend_by('sales', product_id, date, quantity, 'auto');

-- Force linear detrending
SELECT * FROM ts_detrend_by('sales', product_id, date, quantity, 'linear');

-- Quadratic detrending for curved trends
SELECT * FROM ts_detrend_by('sales', product_id, date, quantity, 'quadratic');

-- Access detrended data (output column is named after the group_col you pass in)
SELECT
    sensor_id,
    detrended[1:5] AS first_5_detrended,
    method,
    n_params
FROM ts_detrend_by('sensor_data', sensor_id, timestamp, reading, 'auto');
```

**When to use:**
- Use `ts_detrend_by` for simple trend removal without seasonal decomposition
- Use `ts_mstl_decomposition_by` when you need full seasonal decomposition (trend + seasonal + remainder)

---

### ts_detrend_long_by

Long form of `ts_detrend_by` (#282): `<group_col>, <date_col>, component, value, method`, with
`component` in `observed`, `trend`, `detrended` (one row each per source row). Rows before the
first / after the last non-NULL value have NULL `trend` and `detrended`.

```sql
SELECT * FROM ts_detrend_long_by('sales', product_id, date, quantity, method := 'linear');
```

### ts_decompose_seasonal

Single-series additive/multiplicative seasonal decomposition (scalar function).

**Signature:**
```sql
ts_decompose_seasonal(values DOUBLE[], period DOUBLE[, method VARCHAR]) → STRUCT(trend DOUBLE[], seasonal DOUBLE[], remainder DOUBLE[], period DOUBLE, method VARCHAR)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `values` | DOUBLE[] | Time series values, ordered |
| `period` | DOUBLE | Seasonal period; must be positive, else a SQL error (`Period must be positive`). `classical_*` methods additionally require an INTEGER `period >= 2` (a non-integer or `< 2` period is a SQL error) |
| `method` | VARCHAR | `'additive'` (alias `'add'`, default), `'multiplicative'` (alias `'mult'`, `'mul'`), `'classical_additive'` (alias `'classical'`), or `'classical_multiplicative'`. Any other name is a SQL error naming every choice. |

**`'additive'` / `'multiplicative'` (fdars LOESS + Fourier):** extract the trend via
fdars-core's LOESS detrending (bandwidth 0.3, local degree 2), then fit a 3-harmonic Fourier
seasonal model to the detrended/deseasonalised residual. `data = trend + seasonal +
remainder` (additive) or `data = trend * seasonal * remainder` (multiplicative). A constant
series decomposes to `trend = ` the constant and `seasonal ≈ 0` everywhere (within 1e-12);
for a non-trivial series, `trend + seasonal + remainder` reconstructs the input to ~1e-10
(additive) or `trend * seasonal * remainder` reconstructs it to ~1e-9 (multiplicative).

**`'classical_additive'` / `'classical_multiplicative'` (R `stats::decompose`, Phase 15,
D-10):** a centred moving-average trend — a `2 x period` filter (weights `0.5, 1x(period-1),
0.5`, divided by `period`) for an even period, or the plain `period`-term mean for an odd
period — then a per-phase seasonal figure averaged over the detrended series and centred (by
subtraction for additive, by division for multiplicative). `trend` (and therefore
`remainder`) is `NaN` (use `isnan(...)`, not `IS NULL`) at the `floor(period/2)` points at
each end, exactly where R's own filter returns `NA` — this is NOT a bug, it matches R
exactly. A constant series of 1.0
(period 12, n >= 24) has an interior trend of exactly `1.0` and `seasonal` exactly `0.0`.
`'classical_multiplicative'` requires every value `> 0` (R itself would silently divide by a
non-positive value and produce `Inf`/`NaN`/negative garbage with no diagnostic — this is a
deliberate improvement, not a parity gap).

**Example:**
```sql
SELECT (ts_decompose_seasonal(LIST(quantity ORDER BY date), 12.0)).trend[1:5]
FROM monthly_sales WHERE product_id = 1;

-- Multiplicative, for a series with multiplicative seasonality
SELECT (ts_decompose_seasonal(LIST(quantity ORDER BY date), 12.0, 'multiplicative')).seasonal[1:5]
FROM monthly_sales WHERE product_id = 1;

-- Classical (R stats::decompose) additive decomposition -- note the NULL/NaN trend edges
SELECT (ts_decompose_seasonal(LIST(quantity ORDER BY date), 12.0, 'classical_additive')).trend[1:7]
FROM monthly_sales WHERE product_id = 1;
```

---

### ts_classify_seasonality_by

Classify seasonality type per group in a multi-series table.

**Signature:**
```sql
ts_classify_seasonality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Group identifier |
| `timing_classification` | VARCHAR | `'stable_seasonal'`, `'variable_timing'`, `'intermittent_seasonal'`, or `'non_seasonal'` (not `'early'`/`'on_time'`/`'late'`/`'variable'` — those are not the real label set) |
| `modulation_type` | VARCHAR | `'stable'`, `'emerging'`, `'fading'`, `'oscillating'`, or `'non_seasonal'` (not `'growing'`/`'shrinking'`/`'variable'` — those are not the real label set) |
| `has_stable_timing` | BOOLEAN | Whether peak timing is consistent |
| `timing_variability` | DOUBLE | Variability score (lower = more stable) |
| `seasonal_strength` | DOUBLE | STL strength of seasonality (0-1), `NULL` if no usable period |
| `is_seasonal` | BOOLEAN | Whether significant seasonality exists (`seasonal_strength >= 0.3` by default) |
| `cycle_strengths` | DOUBLE[] | Strength per cycle |
| `weak_seasons` | INTEGER[] | Indices of weak seasonal cycles |

**Phase 15:** a clearly seasonal series (e.g. a clean sine wave with the correct period) now
correctly comes back as `timing_classification = 'stable_seasonal'`, `is_seasonal = true`,
and `seasonal_strength` close to 1.0 — a fdars-core dimension-swap bug that previously made
every call return `'non_seasonal'` / `NaN` has been fixed (WRAP-01).

**#286 fix:** `seasonal_strength` (and the `is_seasonal`/`timing_classification` that depend
on it) is now the SAME STL strength `ts_stats_by(..., seasonal_period := period)` reports
(tsfeatures' `F_S = max(0, 1 - Var(R)/Var(S+R))` on the R-matching STL decomposition at the
rounded `period`), not fdars's own raw-series variance-explained measure — the two used to
disagree on the same series (a strong weekly series read `is_seasonal = false` with
`seasonal_strength = 0.156` here while `ts_stats_by` reported `0.966` for the identical
series/period). A series with no usable period or a degenerate (near-zero-variance)
decomposition now returns `seasonal_strength = NULL` (previously `NaN`). `ts_detect_amplitude_modulation`'s
seasonality gate and `seasonal_strength` use the same STL strength.

**Example:**
```sql
-- Classify weekly seasonality per product
SELECT * FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0);

-- Find products with strong, stable seasonality
SELECT product_id, seasonal_strength
FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0)
WHERE is_seasonal AND has_stable_timing;
```

---

### ts_classify_seasonality

Classify the type and strength of seasonality for a single-series table.

**Signature:**
```sql
ts_classify_seasonality(source VARCHAR, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:** Single row with classification columns (same columns as `ts_classify_seasonality_by` without `id`).

**Example:**
```sql
-- Classify weekly seasonality for a single series
SELECT * FROM ts_classify_seasonality('daily_sales', date, value, 7.0);

-- Check if seasonality is strong and stable
SELECT
    is_seasonal,
    seasonal_strength,
    has_stable_timing
FROM ts_classify_seasonality('daily_sales', date, value, 7.0);
```

---

### ts_seasonal_strength / ts_seasonal_strength_windowed

Measure how strongly a seasonal pattern dominates a series, at a single period
(`ts_seasonal_strength`) or over a sliding window (`ts_seasonal_strength_windowed`).

**Signatures:**
```sql
ts_seasonal_strength(values LIST(DOUBLE), period DOUBLE[, method VARCHAR]) → DOUBLE
ts_seasonal_strength_windowed(values LIST(DOUBLE), period DOUBLE[, window_size DOUBLE[, method VARCHAR]]) → DOUBLE[]
```

**`method` (DET-06, D-12):**
| Method | Aliases | Description |
|--------|---------|--------------|
| `'variance'` (default) | `'var'` | Variance explained by the seasonal component |
| `'spectral'` | `'spec'`, `'fft'` | Power at seasonal frequencies / total power |

Both methods **linearly detrend the series first**, then measure strength on the residual.
Before this plan (G19), the strength was measured on the RAW series: a trend + seasonal
series (`0.5*t + 2*sin(2*pi*t/12)`, `n=120`) measured `variance` strength `~0.003` instead of
the `>= 0.95` a seasonal signal that strong should report, because the trend dominated total
variance. `'wavelet'` is **removed** — it returned `1.0` even for white noise in fdars-core
0.47 (always-maximal strength regardless of input) — and now raises an error naming the two
remaining methods; any other unrecognized method name, or a non-positive `period`, also
raises an error (previously an unrecognized name silently fell back to `'variance'`).

**Example:**
```sql
-- Strength on the DETRENDED residual (method defaults to 'variance')
SELECT ts_seasonal_strength(list_transform(range(28), lambda i: sin(2 * pi() * i / 7)), 7.0);

-- Spectral method
SELECT ts_seasonal_strength(list_transform(range(28), lambda i: sin(2 * pi() * i / 7)), 7.0, 'spectral');

-- Rolling strength over 3 periods
SELECT ts_seasonal_strength_windowed(list_transform(range(28), lambda i: sin(2 * pi() * i / 7)), 7.0, 21.0);
```

**`ts_analyze_seasonality`'s `trend_strength` column** is `|r|` — the absolute Pearson
correlation between the series and a linear time index (the square root of the linear-fit
`R^2`, not `R^2` itself).

---

*See also: [Period Detection](05-period-detection.md) | [Peak Detection](05b-peak-detection.md)*
