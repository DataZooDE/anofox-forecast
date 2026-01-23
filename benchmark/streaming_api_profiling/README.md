# Streaming API Profiling Framework

This directory contains tools for profiling table macro functions to measure CPU time and memory usage, helping identify candidates for streaming API (`table_in_out`) conversion.

## Background

In #105/#114, `ts_backtest_auto_by` was refactored to use native streaming, achieving:
- **31x memory reduction** (1,951 MB -> 63 MB for 1M rows)
- **1.7x faster execution** (0.54s -> 0.31s)

This framework helps identify other functions that could benefit from similar optimization.

## Quick Start

```bash
# Build the extension first
make release

# Run quick profiling (100K rows, ~1 min)
python benchmark/streaming_api_profiling/profile_functions.py --quick

# Run full profiling (1M rows, ~10 min)
python benchmark/streaming_api_profiling/profile_functions.py

# Profile specific function
python benchmark/streaming_api_profiling/profile_functions.py --function ts_cv_forecast_by

# Profile by category
python benchmark/streaming_api_profiling/profile_functions.py --category fill
```

## Files

| File | Description |
|------|-------------|
| `generate_test_data.sql` | Creates test datasets (1M and 100K rows) |
| `profile_functions.py` | Main profiling script |
| `profile_results.json` | Output with detailed metrics |

## Metrics Captured

| Metric | Description |
|--------|-------------|
| `latency_ms` | Query execution time in milliseconds |
| `memory_peak_mb` | Peak buffer memory usage |
| `temp_dir_mb` | Temporary directory usage (spill to disk) |
| `total_memory_mb` | Combined memory + temp usage |

## Function Categories

| Category | Priority | Functions |
|----------|----------|-----------|
| `cv_forecasting` | P1 | ts_cv_forecast_by, ts_cv_split_by |
| `fill` | P1 | ts_fill_gaps_by, ts_fill_forward_by (+ native versions) |
| `forecasting` | P2 | ts_forecast_by |
| `statistics` | P2 | ts_stats_by |
| `features` | P2 | ts_features_by |
| `decomposition` | P2 | ts_mstl_decomposition_by |
| `conformal` | P2 | ts_conformal_by, ts_conformal_calibrate |
| `data_quality` | P2 | ts_data_quality_by, ts_data_quality_summary, ts_quality_report |
| `detection` | P3 | ts_detect_periods_by, ts_classify_seasonality_by, ts_detect_changepoints_by |

## Identifying Bottlenecks

After profiling, look for:

1. **High memory usage** - Functions using `LIST()` aggregations
2. **Large intermediate CTEs** - Materialized subqueries
3. **CROSS JOIN operations** - Cartesian products
4. **No temp directory usage** - Means data can't spill to disk (OOM risk)

## Related Issues

- #105 - Original segfault investigation
- #113 - Consolidate ts_fill_gaps_* and ts_fill_forward_* APIs
- #114 - PR implementing native ts_backtest_auto_by
- #115 - Research streaming API adoption for memory-intensive functions
