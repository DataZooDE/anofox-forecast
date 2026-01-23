# Memory Patterns for DuckDB Extension Development

This document captures findings from investigating memory issues in `ts_backtest_auto_by` and provides guidance for avoiding similar issues in other functions.

## Investigation Summary (#105)

**Issue**: `ts_backtest_auto_by` causes segmentation fault on M5 dataset (30,490 series × 1,941 points) with limited memory.

**Environment tested**:
- 5000 series × 500 points = 2.5M rows
- Various memory limits: 256MB, 288MB, 320MB, 384MB, 512MB, 1GB

## Root Causes Identified

### 1. Missing Allocation Error Handling in FFI

**Location**: `crates/anofox-fcst-ffi/src/lib.rs:3198-3216`

```rust
// PROBLEM: No null check after allocation
(*out_result).point_forecasts = vec_to_c_array(&forecast.point);
(*out_result).lower_bounds = vec_to_c_array(&forecast.lower);
(*out_result).upper_bounds = vec_to_c_array(&forecast.upper);
```

When `vec_to_c_array` fails (returns null), the function still returns `true`, but the result contains null pointers. C++ then segfaults when accessing them.

**Fix needed**: Check for null returns and set appropriate error.

### 2. LIST() Aggregates Cannot Spill to Disk

**DuckDB limitation**: `list()` and `string_agg()` aggregates cannot offload intermediate state to disk, unlike grouping, joining, sorting, and windowing operators.

**Impact on ts_backtest_auto_by**:
- Line 1590: `LIST(train_end ORDER BY train_end)` - fold boundaries
- Line 1671: `LIST(_target ORDER BY _dt)` - input to _ts_forecast
- Lines 1718-1726: `LIST(actual/forecast ORDER BY date)` - metrics

**Reference**: https://duckdb.org/docs/stable/guides/troubleshooting/oom_errors

### 3. Cumulative Memory from Materialized CTEs

The macro creates multiple intermediate results that may all be held in memory:

| CTE | Rows (5000 series × 500 points, 5 folds) |
|-----|------------------------------------------|
| src | 2,500,000 |
| cv_splits | 5,875,000 (CROSS JOIN with fold_bounds) |
| cv_train | 5,700,000 |
| forecast_data | 25,000 (but LIST aggregation happens here) |

Total intermediate data can exceed available memory before the LIST aggregation even starts.

### 4. Memory Threshold Behavior

| Memory Limit | Behavior |
|--------------|----------|
| ≥512MB | OOM error (graceful) |
| ~288MB | Segfault (crash) |
| ≤256MB | OOM error (graceful) |

At certain memory levels, DuckDB allows the query to proceed, but FFI allocations fail without proper error handling, causing segfault.

## Patterns That Cause Memory Issues

### 1. LIST() with Large Datasets
```sql
-- PROBLEMATIC: Cannot spill to disk
SELECT group_id, LIST(value ORDER BY date) AS values
FROM large_table
GROUP BY group_id
```

### 2. CROSS JOIN Row Expansion
```sql
-- PROBLEMATIC: Multiplies row count
SELECT s.*, f.fold_id
FROM source s
CROSS JOIN fold_bounds f
WHERE ...
```

### 3. Unguarded FFI Allocations
```rust
// PROBLEMATIC: No null check
(*out).data = vec_to_c_array(&result);
return true; // Returns success even if allocation failed
```

### 4. Multiple Large CTEs
```sql
-- PROBLEMATIC: All may be materialized
WITH
  large_cte1 AS (...),    -- 2.5M rows
  large_cte2 AS (...),    -- 5M rows
  large_cte3 AS (...),    -- 5M rows
SELECT ...
```

## Patterns That Avoid Memory Issues

### 1. table_in_out Streaming Pattern

See `src/table_functions/ts_fill_gaps_native.cpp` for implementation.

```cpp
// Streaming: receive chunks, buffer per-group, emit chunks
func.in_out_function = MyInOutFunction;
func.in_out_function_final = MyFinalizeFunction;
```

**Benefits**:
- Processes data in chunks, not all at once
- Only buffers what's needed per group
- Streams output instead of materializing

### 2. Guarded Allocations
```rust
// CORRECT: Check for allocation failure
let ptr = vec_to_c_array(&result);
if ptr.is_null() && !result.is_empty() {
    (*out_error).set_error(ErrorCode::AllocationError, "Memory allocation failed");
    return false;
}
(*out).data = ptr;
```

### 3. Two-Pass Allocation
```cpp
// Pass 1: Calculate total size needed
size_t total_size = 0;
for (auto& group : groups) {
    total_size += group.size();
}

// Pass 2: Allocate once, copy data
ListVector::Reserve(result, total_size);
for (auto& group : groups) {
    // Copy to pre-allocated space
}
```

### 4. Batch Processing Parameter
```sql
-- User can control memory usage
ts_backtest_auto_by(..., MAP{'batch_size': '1000'})
```

Process groups in batches, emit results incrementally.

## Checklist for New Functions

Before releasing a new `_by` function, verify:

- [ ] Does it use `LIST()` on potentially large datasets?
  - Consider alternative approaches or warn users
- [ ] Does it use `CROSS JOIN` that could explode row count?
  - Add row count estimation and warning
- [ ] Does it materialize all intermediate results?
  - Consider streaming with table_in_out
- [ ] Are all FFI allocations checked for failure?
  - Add null checks and proper error handling
- [ ] Has it been tested at scale?
  - Test with M5-like dataset: 30k series × 2k points
  - Test with memory limits: 256MB, 512MB, 1GB

## Functions to Audit

Based on these findings, the following functions should be audited for similar issues (see #115):

| Function | Uses LIST() | Uses CROSS JOIN | Peak Memory | Status |
|----------|-------------|-----------------|-------------|--------|
| ts_forecast_by | ~~Yes~~ | No | ~~358 MB~~ → 4 MB | **Fixed - Native streaming** |
| ts_cv_forecast_by | ~~Yes~~ | ~~Yes~~ | ~~212 MB~~ → 116 MB | **Fixed - Native streaming** |
| ts_backtest_auto_by | ~~Yes~~ | ~~Yes~~ | ~~1951 MB~~ → 63 MB | **Fixed in #114** |
| ts_cv_split_by | ~~Yes~~ | ~~Yes~~ | ~~36 MB~~ → 19 MB | **Fixed - Native streaming** |
| ts_fill_gaps_by | Yes (via macro) | No | Low | See #113 |
| ts_stats_by | Yes | No | 32 MB | No action needed |
| ts_features_by | Yes | No | 34 MB | No action needed |
| ts_mstl_decomposition_by | ~~Yes~~ | No | ~35 MB | **Fixed - Native streaming** (minimal improvement, already low) |
| ts_detect_changepoints_by | Yes | No | 33 MB | No action needed |
| ts_detect_periods_by | Yes | No | 32 MB | No action needed |
| ts_classify_seasonality_by | Yes | No | 36 MB | No action needed |

## Mitigations Implemented for #105

### Completed (PR #114):

1. **FFI Allocation Safety** - Added null checks to `vec_to_c_array` calls in `anofox_ts_forecast`
   - Returns proper error instead of segfault when allocation fails
   - See `crates/anofox-fcst-ffi/src/lib.rs:3198-3225`

2. **Native Streaming Implementation** - Converted `ts_backtest_auto_by` to use `table_in_out` pattern
   - New internal function: `_ts_backtest_native` (see `src/table_functions/ts_backtest_native.cpp`)
   - Processes groups in parallel across threads
   - Streams output instead of materializing all results
   - **Result: 31x memory reduction** (1,951 MB → 63 MB for 1M rows)

3. **Macro Refactoring** - `ts_backtest_auto_by` is now a thin wrapper
   - Public API unchanged
   - Internally calls `_ts_backtest_native`
   - Removed 259 lines of complex SQL CTEs

### Performance Comparison: ts_backtest_auto_by (1M rows, 10k series)

| Metric | Old SQL Macro | New Native | Improvement |
|--------|---------------|------------|-------------|
| Memory | 1,951 MB | 63 MB | **31x less** |
| Latency | 0.54s | 0.31s | **1.7x faster** |

### Additional Streaming Conversions (#105)

The following functions have also been converted to native streaming:

#### ts_forecast_by (100K rows, 1K groups)

| Metric | Old SQL Macro | New Native | Improvement |
|--------|---------------|------------|-------------|
| Peak Memory | 358 MB | 4 MB | **99% reduction** |
| Latency | N/A | 67ms | Excellent |

**Implementation**: `src/table_functions/ts_forecast_native.cpp`
- Uses `table_in_out` streaming pattern
- Buffers data per group, processes in finalize
- Avoids `LIST()` aggregation memory explosion

#### ts_cv_split_by (100K rows, 1K groups, 3 folds)

| Metric | Old SQL Macro | New Native | Improvement |
|--------|---------------|------------|-------------|
| Peak Memory | 35.7 MB | 18.9 MB | **47% reduction** |
| Latency | ~100ms | ~82ms | Slightly faster |
| Result rows | 143,000 | 143,000 | 100% match |

**Implementation**: `src/table_functions/ts_cv_split_native.cpp`
- Buffers input rows, expands in finalize phase
- Avoids `CROSS JOIN` intermediate materialization
- Properly handles output buffer overflow with `HAVE_MORE_OUTPUT`

### Functions Not Requiring Streaming Conversion

The following functions were profiled and found to have acceptable memory usage (32-36 MB for 100K rows, 1K groups). These don't require native streaming conversion:

| Function | Peak Memory | Notes |
|----------|-------------|-------|
| ts_stats_by | ~32 MB | Simple aggregations, low overhead |
| ts_features_by | ~34 MB | Feature extraction per group |
| ts_mstl_decomposition_by | ~35 MB | Converted to native streaming for consistency |
| ts_detect_changepoints_by | ~33 MB | Point-wise detection |
| ts_detect_periods_by | ~32 MB | FFT-based, efficient |
| ts_classify_seasonality_by | ~36 MB | Classification results are small |

**Recommendation**: Focus streaming conversion efforts on functions with >100 MB memory usage, particularly those using `LIST()` aggregation or `CROSS JOIN` patterns.

### Remaining Work

See GitHub issues:
- #115 - Research streaming API adoption for other memory-intensive functions
- #116 - Add null checks to remaining FFI allocation calls (~35 sites)

## DuckDB Memory Configuration

For users hitting memory issues:

```sql
-- Reduce memory limit to trigger graceful OOM earlier
SET memory_limit = '50%';

-- Configure temp directory for spilling
SET temp_directory = '/path/to/fast/ssd';

-- Reduce parallelism
SET threads = 4;

-- Disable insertion order preservation
SET preserve_insertion_order = false;
```

**Note**: These help with operators that CAN spill (grouping, sorting, joins), but not with LIST() aggregates.

## References

- [DuckDB Memory Management](https://duckdb.org/2024/07/09/memory-management)
- [DuckDB OOM Errors](https://duckdb.org/docs/stable/guides/troubleshooting/oom_errors)
- [DuckDB Tuning Workloads](https://duckdb.org/docs/stable/guides/performance/how_to_tune_workloads)
