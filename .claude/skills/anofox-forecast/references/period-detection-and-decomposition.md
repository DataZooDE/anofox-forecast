# Period Detection & Decomposition Reference

## ts_detect_periods_by

Detect seasonal periods for grouped series.

```sql
ts_detect_periods_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, params MAP/STRUCT) → TABLE(id, periods)
```

Params:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| method | VARCHAR | 'fft' | Detection method |
| max_period | VARCHAR | '365' | Max period to search |
| min_confidence | VARCHAR | method-specific | Min confidence threshold; '0' to disable |

Methods (12):
| Method | Aliases | Best For |
|--------|---------|----------|
| 'fft' | 'periodogram' | Clean signals, fast (default) |
| 'acf' | 'autocorrelation' | Cyclical patterns, noise-robust |
| 'autoperiod' | 'ap' | General purpose, robust |
| 'cfd' | 'cfdautoperiod' | Trending data |
| 'lombscargle' | 'lomb_scargle' | Irregular sampling |
| 'aic' | 'aic_comparison' | Model comparison |
| 'ssa' | 'singular_spectrum' | Complex patterns |
| 'stl' | 'stl_period' | Decomposition-based |
| 'matrix_profile' | 'matrixprofile' | Pattern repetition |
| 'sazed' | 'zero_padded' | High frequency resolution |
| 'auto' | — | Unknown characteristics |
| 'multi' | 'multiple' | Complex seasonality |

Returns STRUCT:
- periods[]: array of {period, confidence, strength, amplitude, phase, iteration}
- n_periods: BIGINT
- primary_period: DOUBLE (dominant period)
- method: VARCHAR

Confidence interpretation:
- FFT: peak-to-mean power ratio, good > 5.0
- ACF: autocorrelation at lag, good > 0.3

Default thresholds filter low-confidence periods automatically. Set min_confidence='0' to see all.

Example:
```sql
SELECT id, (periods).primary_period, (periods).n_periods
FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- With ACF method
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value,
    MAP{'method': 'acf', 'max_period': '28'});
```

## ts_classify_seasonality_by

Classify seasonality type per group.

```sql
ts_classify_seasonality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

Returns:
| Column | Type | Description |
|--------|------|-------------|
| group_col | (input) | Series identifier |
| timing_classification | VARCHAR | 'early', 'on_time', 'late', 'variable' |
| modulation_type | VARCHAR | 'stable', 'growing', 'shrinking', 'variable' |
| has_stable_timing | BOOLEAN | Consistent peak timing? |
| timing_variability | DOUBLE | Lower = more stable |
| seasonal_strength | DOUBLE | 0-1 scale |
| is_seasonal | BOOLEAN | Significant seasonality? |
| cycle_strengths | DOUBLE[] | Strength per cycle |
| weak_seasons | INTEGER[] | Indices of weak cycles |

Example:
```sql
SELECT id, seasonal_strength, is_seasonal
FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0)
WHERE is_seasonal AND has_stable_timing;
```

## ts_classify_seasonality (single-series variant)

```sql
ts_classify_seasonality(source VARCHAR, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```
Same columns as _by variant but without group_col.

## ts_mstl_decomposition_by

MSTL decomposition into trend, seasonal, remainder.

```sql
ts_mstl_decomposition_by(source VARCHAR, group_col IDENTIFIER, date_col IDENTIFIER, value_col IDENTIFIER, seasonal_periods INTEGER[], params MAP) → TABLE
```

Returns: group_col, trend (DOUBLE[]), seasonal (DOUBLE[][]), remainder (DOUBLE[]), periods (INTEGER[])

Example:
```sql
SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, quantity, [7, 365], MAP{});
```

## ts_detrend_by

Remove trend using polynomial fitting.

```sql
ts_detrend_by(source VARCHAR, group_col IDENTIFIER, date_col IDENTIFIER, value_col IDENTIFIER, method VARCHAR) → TABLE
```

Methods: 'linear', 'quadratic', 'cubic', 'auto'

Returns: group_col, trend (DOUBLE[]), detrended (DOUBLE[]), method (VARCHAR), coefficients (DOUBLE[]), rss (DOUBLE), n_params (BIGINT)

## ts_detect_peaks_by

Detect peaks in grouped series.

```sql
ts_detect_peaks_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, params MAP) → TABLE(id, peaks)
```

Params: min_distance (def: 1.0), min_prominence (def: 0.0), smooth_first (def: false)

Returns peaks STRUCT: peaks[] (index, time, value, prominence), n_peaks, inter_peak_distances[], mean_period

## ts_analyze_peak_timing_by

Analyze peak timing regularity.

```sql
ts_analyze_peak_timing_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, period DOUBLE, params MAP) → TABLE(id, timing)
```

Returns timing STRUCT: n_peaks, peak_times[], variability_score, is_stable

Example:
```sql
SELECT id, (timing).is_stable, (timing).variability_score
FROM ts_analyze_peak_timing_by('sales', product_id, date, value, 7.0, MAP{});
```
