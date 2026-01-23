# Streaming API Profiling Results

**Date:** 2026-01-23
**Dataset:** 1M rows, 10K groups (100 days per group)
**Related Issues:** GH#115, GH#113

## Executive Summary

Profiled 17 table macro functions to identify candidates for streaming API (`table_in_out`) conversion. Key findings:

1. **Top memory consumers** should be prioritized for conversion
2. **Fill functions already have native implementations** showing 50x memory reduction
3. **ts_stats_by uses the most memory** (512 MB for 1M rows)

## Profiling Results (1M rows, 10K groups)

### Successful Profiles - Ranked by Memory Usage

| Rank | Function | Memory (MB) | Latency (ms) | Priority | Category |
|------|----------|-------------|--------------|----------|----------|
| 1 | ts_stats_by | 512.3 | 0.2 | P2 | statistics |
| 2 | ts_forecast_by | 357.7 | 0.1 | P2 | forecasting |
| 3 | ts_mstl_decomposition_by | 355.9 | 0.1 | P2 | decomposition |
| 4 | ts_detect_changepoints_by | 355.9 | 0.5 | P3 | detection |
| 5 | ts_data_quality_by | 355.5 | 0.1 | P2 | data_quality |
| 6 | ts_detect_periods_by | 352.4 | 0.2 | P3 | detection |
| 7 | ts_cv_split_by | 211.5 | 0.2 | P1 | cv_forecasting |
| 8 | ts_fill_gaps_by | 180.6 | 0.2 | P1 | fill |
| 9 | ts_fill_forward_by | 126.6 | 0.1 | P1 | fill |
| 10 | ts_features_by | 110.6 | 0.7 | P2 | features |
| 11 | ts_classify_seasonality_by | 40.1 | 0.0 | P3 | detection |

### Fill Function Comparison (GH#113)

Native `table_in_out` implementations vs SQL macros (100K rows, 1K groups):

| Function | SQL Macro (MB) | Native (MB) | Reduction |
|----------|----------------|-------------|-----------|
| ts_fill_gaps | 65.4 | 1.3 | **50x** |
| ts_fill_forward | 49.1 | 1.3 | **38x** |

**Note:** Native implementations have a bug with 10K+ groups (INTERNAL Error: BatchedDataCollection::Merge). Works correctly with 1K groups.

## Prioritized Conversion Candidates

Based on memory usage and conversion complexity:

### Tier 1 - High Impact, Proven Pattern
These use similar patterns to the already-converted `ts_backtest_auto_by`:

| Function | Memory | Expected Reduction | Notes |
|----------|--------|-------------------|-------|
| ts_stats_by | 512 MB | ~30x | Uses LIST() aggregation |
| ts_forecast_by | 358 MB | ~30x | Uses LIST() aggregation |
| ts_cv_split_by | 212 MB | ~30x | Uses LIST() aggregation |

### Tier 2 - High Impact, More Complex
These may require additional work:

| Function | Memory | Notes |
|----------|--------|-------|
| ts_mstl_decomposition_by | 356 MB | Complex decomposition logic |
| ts_data_quality_by | 356 MB | Multiple quality metrics |
| ts_detect_periods_by | 352 MB | FFT-based detection |
| ts_detect_changepoints_by | 356 MB | Bayesian changepoint detection |

### Tier 3 - Lower Priority
Already efficient or lower usage:

| Function | Memory | Notes |
|----------|--------|-------|
| ts_features_by | 111 MB | Already relatively efficient |
| ts_classify_seasonality_by | 40 MB | Very efficient |

## Known Issues

1. **Native fill functions fail with 10K+ groups**
   - Error: `INTERNAL Error: BatchedDataCollection::Merge error`
   - Works with 1K groups
   - Needs investigation before consolidation (GH#113)

2. **Some functions couldn't be profiled**
   - ts_cv_forecast_by: Requires pre-computed CV splits
   - ts_data_quality, ts_quality_report: Signature issues
   - Conformal functions: Require backtest results

## Recommendations

1. **Immediate (GH#113):** Fix native fill function bug, then consolidate APIs
2. **Next:** Convert `ts_stats_by` (highest memory usage)
3. **Then:** Convert `ts_forecast_by` and `ts_cv_split_by`
4. **Later:** Evaluate decomposition and detection functions

## Methodology

- Used DuckDB's JSON profiling (`PRAGMA enable_profiling = 'json'`)
- Captured `system_peak_buffer_memory` and `system_peak_temp_dir_size`
- Test data: Synthetic time series with trend, seasonality, and noise
- Each function run in isolation with fresh profiling context

## Files

- `profile_functions.py` - Profiling script
- `generate_test_data.sql` - Test data generation
- `profile_results_1m.json` - Raw results (1M rows)
- `profile_benchmark.duckdb` - Persistent test database
