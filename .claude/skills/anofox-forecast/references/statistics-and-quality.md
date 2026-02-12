# Statistics & Quality Reference

## ts_stats_by

Compute 36 statistics per series.

```sql
ts_stats_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

Frequency: '1d', '1h', '1w', '1mo', etc.

Returns (36 columns):
| Column | Type | Description |
|--------|------|-------------|
| group_col | (input) | Series identifier |
| length | UBIGINT | Total observations |
| n_nulls | UBIGINT | NULL count |
| n_nan | UBIGINT | NaN count |
| n_zeros | UBIGINT | Zero count |
| n_positive | UBIGINT | Positive count |
| n_negative | UBIGINT | Negative count |
| n_unique_values | UBIGINT | Distinct values |
| is_constant | BOOLEAN | Only one unique value? |
| n_zeros_start | UBIGINT | Leading zeros |
| n_zeros_end | UBIGINT | Trailing zeros |
| plateau_size | UBIGINT | Longest constant run |
| plateau_size_nonzero | UBIGINT | Longest constant non-zero run |
| mean | DOUBLE | Mean |
| median | DOUBLE | Median |
| std_dev | DOUBLE | Standard deviation |
| variance | DOUBLE | Variance |
| min | DOUBLE | Minimum |
| max | DOUBLE | Maximum |
| range | DOUBLE | max - min |
| sum | DOUBLE | Sum |
| skewness | DOUBLE | Skewness |
| kurtosis | DOUBLE | Excess kurtosis |
| tail_index | DOUBLE | Hill estimator |
| bimodality_coef | DOUBLE | Bimodality coefficient |
| trimmed_mean | DOUBLE | 10% trimmed mean |
| coef_variation | DOUBLE | Coefficient of variation |
| q1 | DOUBLE | First quartile |
| q3 | DOUBLE | Third quartile |
| iqr | DOUBLE | Interquartile range |
| autocorr_lag1 | DOUBLE | Autocorrelation at lag 1 |
| trend_strength | DOUBLE | Trend strength (0-1) |
| seasonality_strength | DOUBLE | Seasonality strength (0-1) |
| entropy | DOUBLE | Approximate entropy |
| stability | DOUBLE | Stability measure |
| expected_length | UBIGINT | Expected obs based on date range + frequency |
| n_gaps | UBIGINT | Number of gaps |

Alias: `ts_stats` = `ts_stats_by`

Example:
```sql
SELECT id, length, mean, std_dev, trend_strength, n_gaps
FROM ts_stats_by('sales', product_id, date, quantity, '1d');
```

## ts_data_quality_by

Quality scores across four dimensions.

```sql
ts_data_quality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER, frequency VARCHAR) → TABLE
```

Returns:
| Column | Type | Description |
|--------|------|-------------|
| unique_id | (input) | Series identifier |
| structural_score | DOUBLE | Data completeness (0-1) |
| temporal_score | DOUBLE | Timestamp regularity (0-1) |
| magnitude_score | DOUBLE | Value distribution health (0-1) |
| behavioral_score | DOUBLE | Predictability (0-1) |
| overall_score | DOUBLE | Overall quality (0-1, higher=better) |
| n_gaps | UBIGINT | Gap count |
| n_missing | UBIGINT | Missing values |
| is_constant | BOOLEAN | Constant series? |

Alias: `ts_data_quality` = `ts_data_quality_by`

Example:
```sql
-- High-quality series only
SELECT unique_id FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d')
WHERE overall_score > 0.8;
```

## ts_quality_report

```sql
ts_quality_report(stats_table VARCHAR, min_length INTEGER) → TABLE
```
Returns: n_passed, n_nan_issues, n_missing_issues, n_constant, n_total

## ts_stats_summary

```sql
ts_stats_summary(stats_table VARCHAR) → TABLE
```
Returns: n_series, avg_length, min_length, max_length, total_nulls, total_nans

Example:
```sql
CREATE TABLE stats AS SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');
SELECT * FROM ts_stats_summary('stats');
```

## ts_data_quality_summary

```sql
ts_data_quality_summary(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER) → TABLE
```
