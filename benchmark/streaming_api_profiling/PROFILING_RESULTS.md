# Streaming API Profiling Results

**Date:** 2026-01-23 (Updated after GH#113 completion)
**Dataset:** 1M rows, 10K groups (100 days per group)
**Related Issues:** GH#115, GH#113

## Executive Summary

Profiled 17 table macro functions to identify candidates for streaming API (`table_in_out`) conversion. Key findings:

1. **Fill functions now use native implementations** - achieved 15x and 10x memory reduction
2. **Top memory consumers** (`ts_forecast_by`, `ts_cv_split_by`) should be prioritized for next conversion
3. **Native implementations are dramatically more efficient** for large datasets

## GH#113 Completion - Fill Functions Consolidated

The `ts_fill_gaps_by` and `ts_fill_forward_by` macros now call native streaming implementations:

| Function | Before (MB) | After (MB) | Reduction | Status |
|----------|-------------|------------|-----------|--------|
| ts_fill_gaps_by | 181 | 12 | **15x** | COMPLETED |
| ts_fill_forward_by | 127 | 13 | **10x** | COMPLETED |
| ts_fill_gaps_operator_by | 181 | 12 | **15x** | COMPLETED (alias) |

## Profiling Results (1M rows, 10K groups)

### Successful Profiles - Ranked by Memory Usage

| Rank | Function | Memory (MB) | Latency (ms) | Priority | Category |
|------|----------|-------------|--------------|----------|----------|
| 1 | ts_forecast_by | 358.0 | 0.1 | P2 | forecasting |
| 2 | ts_cv_split_by | 211.8 | 0.2 | P1 | cv_forecasting |
| 3 | ts_stats_by | 35.5 | 0.0 | P2 | statistics |
| 4 | ts_features_by | 35.3 | 0.0 | P2 | features |
| 5 | ts_mstl_decomposition_by | 35.2 | 0.0 | P2 | decomposition |
| 6 | ts_detect_changepoints_by | 35.2 | 0.0 | P3 | detection |
| 7 | ts_classify_seasonality_by | 32.0 | 0.0 | P3 | detection |
| 8 | ts_data_quality_by | 31.8 | 0.0 | P2 | data_quality |
| 9 | ts_detect_periods_by | 31.8 | 0.0 | P3 | detection |
| 10 | **ts_fill_forward_by** | **13.0** | 0.2 | DONE | fill |
| 11 | **ts_fill_gaps_by** | **12.4** | 0.2 | DONE | fill |
| 12 | ts_fill_gaps_native | 10.9 | 0.2 | DONE | fill (native) |
| 13 | ts_fill_forward_native | 10.9 | 0.2 | DONE | fill (native) |

**Note:** DuckDB has a profiling bug where `SELECT *` with profiling fails for table_in_out functions. Queries wrapped in `COUNT(*)` to work around this.

## Next Conversion Candidates

Based on memory usage and conversion complexity:

### Tier 1 - High Impact, Need Native Implementation

| Function | Memory | Expected Reduction | Notes |
|----------|--------|-------------------|-------|
| ts_forecast_by | 358 MB | ~15-30x | Uses LIST() aggregation |
| ts_cv_split_by | 212 MB | ~15-30x | Uses LIST() aggregation |

### Tier 2 - Lower Priority
Already relatively efficient:

| Function | Memory | Notes |
|----------|--------|-------|
| ts_stats_by | 36 MB | Efficient with COUNT(*) wrapper |
| ts_features_by | 35 MB | Already efficient |
| ts_mstl_decomposition_by | 35 MB | Already efficient |
| Detection functions | 32-35 MB | Already efficient |

## Known Issues

1. **DuckDB profiling bug with table_in_out**
   - `SELECT *` with profiling fails for table_in_out functions
   - Error: `BatchedDataCollection::Merge`
   - Workaround: Wrap queries in `COUNT(*)`
   - Native functions work correctly in normal usage

2. **Some functions couldn't be profiled**
   - ts_cv_forecast_by: Requires pre-computed CV splits
   - ts_data_quality, ts_quality_report: Signature issues
   - Conformal functions: Require backtest results

## Recommendations

1. **COMPLETED (GH#113):** Fill function APIs consolidated
2. **Next:** Convert `ts_forecast_by` (highest memory at 358 MB)
3. **Then:** Convert `ts_cv_split_by` (212 MB)
4. **Lower Priority:** Other functions already efficient (~35 MB)

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
