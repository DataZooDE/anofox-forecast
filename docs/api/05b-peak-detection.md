# Peak Detection

> Detect peaks and analyze timing patterns in time series

## Overview

Peak detection identifies local maxima in time series data, useful for finding seasonal highs, demand spikes, or cyclical patterns. Peak timing analysis determines if peaks occur at consistent times within each period.

**Use this document to:**
- Detect peaks with configurable minimum distance and prominence thresholds
- Analyze inter-peak distances to find cyclical patterns
- Determine if peaks occur at consistent times within seasonal periods
- Calculate peak timing variability scores to assess regularity
- Identify products/series with stable vs variable peak patterns

## Algorithm (Phase 15 plan 15-02, DET-01)

`ts_detect_peaks`/`ts_detect_peaks_by` are a direct port of
`scipy.signal.find_peaks` 1.18.1 (`wlen=None`), verified against a checked-in
scipy fixture (`validation/reference/fixtures/scipy_peaks/cases.json`).
This replaced fdars-core's own derivative-sign-change local-maxima detector,
which an audit found missed peaks, reported local minima, and gave every
plateau prominence `0`.

- **Indices are 0-based into the trimmed series** (position 1 of the
  original `LIST` after leading/trailing `NULL`s are removed — see the NULL
  handling note below).
- **A plateau (a run of equal values) reports its midpoint index**
  `(left_edge + right_edge) / 2`, not its first sample. A plateau touching
  either end of the series is never a peak, and neither is the first or
  last sample of the series.
- **`prominence` is scipy's own definition** — the peak's height minus the
  higher of its left/right "valley" minima (scanning outward until a
  strictly higher sample is found, or the series boundary), on the *same
  scale as the input data*. It is **not** normalised to `0..1`. Every
  returned peak carries a prominence, even when `min_prominence` is `0`.
- **`min_distance`** is scipy's `distance`: the minimum *index* distance
  between kept peaks (rounded up). When two peaks are closer than this,
  the taller one wins; among equal heights, the rightmost wins. Values
  `<= 1.0` — including the `ts_detect_peaks_by` macro's default of `1.0` —
  apply no distance filter at all (no two local maxima are ever
  index-adjacent, so `distance <= 1` is a no-op by construction).
- **`min_prominence`** is scipy's `prominence`: an inclusive minimum on the
  real prominence scale above, applied after any distance filter.
- **`inter_peak_distances`** are the consecutive differences between kept
  peak indices; **`mean_period`** is their mean, or SQL `NULL`/NaN when
  fewer than 2 peaks are detected.
- **`smooth_first = true`** applies fdars-core's Fourier-basis smoothing to
  the series *before* peak detection (basis size GCV-selected in `5..25`;
  the Rust API also accepts an explicit basis size, not exposed in SQL);
  reported peak values are the smoothed values, not the raw ones.

```sql
-- Plateau midpoint + real (non-normalised) prominence.
SELECT ts_detect_peaks([0, 1, 1, 1, 0, 2, 2, 0, 3, 3, 3, 3, 1, 0]::DOUBLE[]);
-- peaks[0].index = 2, peaks[0].prominence = 1.0 (not 0, not normalised)
```

**NULL handling (Phase 12, D-03/D-04):** `ts_detect_peaks` and `ts_analyze_peak_timing`
share the Rust single-series policy: leading/trailing `NULL` (and `NaN`) values inside the
`LIST(...)` are trimmed before the analysis runs; an interior `NULL` that survives trimming
is a hard error naming its 1-based position in the original list (e.g.
`ts_detect_peaks: interior NULL at position 6`); a list with no non-NULL value returns SQL
`NULL`. Peak `index` values and the `peak_times`/`normalized_timing` arrays describe the
**trimmed series** — position 1 corresponds to the first non-NULL input value.

```sql
-- Edge trim: leading/trailing NULLs give the same result as the trimmed series.
SELECT ts_detect_peaks([NULL, 1.0, 5.0, 1.0, 4.0, 1.0, NULL]::DOUBLE[])
     = ts_detect_peaks([1.0, 5.0, 1.0, 4.0, 1.0]::DOUBLE[]);
-- true
```

---

## Table Macros

### ts_detect_peaks_by

Detect peaks for multiple series grouped by an identifier.

**Signature:**
```sql
ts_detect_peaks_by(source, group_col, date_col, value_col, params) → TABLE(group_col, peaks, n_peaks, inter_peak_distances, mean_period)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | MAP | Configuration options |

**Params options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `min_distance` | DOUBLE | `1.0` | Minimum distance between peaks |
| `min_prominence` | DOUBLE | `0.0` | Minimum peak prominence threshold |
| `smooth_first` | BOOLEAN | `false` | Apply smoothing before detection |

**Returns (flat columns — not a nested `peaks` STRUCT despite the signature's `→ TABLE(id, peaks)` shorthand):**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `peaks` | `STRUCT("index" BIGINT, "time" DOUBLE, "value" DOUBLE, prominence DOUBLE)[]` | Array of individual peak info |
| `n_peaks` | BIGINT | Number of peaks detected |
| `inter_peak_distances` | DOUBLE[] | Distances between consecutive peaks |
| `mean_period` | DOUBLE | Mean distance between peaks |

`n_peaks`, `inter_peak_distances`, and `mean_period` are top-level columns, not fields of a
wrapping struct — confirmed via `DESCRIBE SELECT * FROM ts_detect_peaks_by(...)`.

**Example:**
```sql
-- Detect peaks for each product (n_peaks/mean_period are plain top-level columns)
SELECT
    product_id,
    n_peaks,
    mean_period
FROM ts_detect_peaks_by('sales', product_id, date, value, MAP{});

-- With custom parameters (higher prominence threshold)
SELECT * FROM ts_detect_peaks_by(
    'sales', product_id, date, value,
    MAP{'min_distance': '3', 'min_prominence': '10.0'}
);
```

---

### ts_detect_peaks_long_by

Long form of `ts_detect_peaks_by` (#282): one row per detected peak,
`<group_col>, <date_col>, kind = 'peak', value, prominence` — usable directly as anofox
contract `events`. Same `params` as `ts_detect_peaks_by`; each peak is placed on the source
row it was found at (leading NULLs are accounted for).

```sql
SELECT * FROM ts_detect_peaks_long_by('sales', product_id, date, quantity,
    params := MAP{'min_prominence': '1.0'});
```

### ts_analyze_peak_timing_by

Analyze peak timing regularity for multiple series. Determines if peaks occur at consistent times within each period.

**Signature:**
```sql
ts_analyze_peak_timing_by(source, group_col, date_col, value_col, period, params) → TABLE(group_col, n_peaks, peak_times, variability_score, is_stable)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |
| `params` | MAP | Configuration options (reserved) |

**Returns (flat columns — not a nested `timing` STRUCT despite the signature's `→ TABLE(id, timing)` shorthand):**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `n_peaks` | BIGINT | Number of peaks detected |
| `peak_times` | DOUBLE[] | Timing of each peak within period |
| `variability_score` | DOUBLE | Timing variability (lower = more regular) |
| `is_stable` | BOOLEAN | True if peak timing is consistent |

Confirmed via `DESCRIBE SELECT * FROM ts_analyze_peak_timing_by(...)`.

**Example:**
```sql
-- Analyze weekly peak timing for each product (plain top-level columns)
SELECT
    product_id,
    is_stable,
    variability_score
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});

-- Find products with stable weekly patterns
SELECT product_id
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{})
WHERE is_stable;
```

---

*See also: [Period Detection](05-period-detection.md) | [Decomposition](05a-decomposition.md)*
