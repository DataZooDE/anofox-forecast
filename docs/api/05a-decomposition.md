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

**Signature (5 args — there is no `seasonal_periods` positional argument):**
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
| `params` | MAP | Only `insufficient_data` is read from `params` today — see Current limitation below |

**Current limitation:** the underlying native implementation does not currently receive or
use any seasonal period information at all. `seasonal` and `periods` are returned as
**empty arrays** on every call — this function currently performs classical moving-average
trend extraction only, with no seasonal decomposition. `remainder` is simply
`value - trend`. (fix planned for v0.10.0, Phase 15)

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Group identifier |
| `trend` | DOUBLE[] | Classical moving-average trend component |
| `seasonal` | DOUBLE[][] | Currently always empty — see Current limitation above |
| `remainder` | DOUBLE[] | `value - trend` (no seasonal component is subtracted) |
| `periods` | INTEGER[] | Currently always empty — see Current limitation above |

**Example:**
```sql
-- Note: there is no seasonal_periods argument to pass today; periods/seasonal come back empty
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, quantity,
    MAP{}
);
```

---

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
| `period` | DOUBLE | Seasonal period; must be positive, else a SQL error (`Period must be positive`) |
| `method` | VARCHAR | `'additive'` (alias `'add'`, default) — `data = trend + seasonal + remainder`; or `'multiplicative'` (alias `'mult'`, `'mul'`) — `data = trend * seasonal * remainder`. Any other name is a SQL error naming both choices. |

Both methods extract the trend via fdars-core's LOESS detrending (bandwidth 0.3, local
degree 2), then fit a 3-harmonic Fourier seasonal model to the detrended/deseasonalised
residual. A constant series decomposes to `trend = ` the constant and `seasonal ≈ 0`
everywhere (within 1e-12); for a non-trivial series, `trend + seasonal + remainder`
reconstructs the input to ~1e-10 (additive) or `trend * seasonal * remainder` reconstructs it
to ~1e-9 (multiplicative).

**Example:**
```sql
SELECT (ts_decompose_seasonal(LIST(quantity ORDER BY date), 12.0)).trend[1:5]
FROM monthly_sales WHERE product_id = 1;

-- Multiplicative, for a series with multiplicative seasonality
SELECT (ts_decompose_seasonal(LIST(quantity ORDER BY date), 12.0, 'multiplicative')).seasonal[1:5]
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
| `seasonal_strength` | DOUBLE | Strength of seasonality (0-1) |
| `is_seasonal` | BOOLEAN | Whether significant seasonality exists |
| `cycle_strengths` | DOUBLE[] | Strength per cycle |
| `weak_seasons` | INTEGER[] | Indices of weak seasonal cycles |

**Phase 15:** a clearly seasonal series (e.g. a clean sine wave with the correct period) now
correctly comes back as `timing_classification = 'stable_seasonal'`, `is_seasonal = true`,
and `seasonal_strength` close to 1.0 — a fdars-core dimension-swap bug that previously made
every call return `'non_seasonal'` / `NaN` has been fixed (WRAP-01).

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
SELECT * FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);

-- Check if seasonality is strong and stable
SELECT
    is_seasonal,
    seasonal_strength,
    has_stable_timing
FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);
```

---

*See also: [Period Detection](05-period-detection.md) | [Peak Detection](05b-peak-detection.md)*
