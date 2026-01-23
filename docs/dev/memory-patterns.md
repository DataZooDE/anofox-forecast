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

Based on these findings, the following functions should be audited for similar issues:

| Function | Uses LIST() | Uses CROSS JOIN | FFI Allocations |
|----------|-------------|-----------------|-----------------|
| ts_forecast_by | Yes | No | Yes |
| ts_decompose_by | Yes | No | Yes |
| ts_detect_anomalies_by | Yes | No | Yes |
| ts_fill_gaps_by | Yes (via macro) | No | Yes (native) |
| ts_backtest_auto_by | Yes | Yes | Yes |
| ts_cv_forecast_by | Yes | Yes | Yes |

## Recommended Mitigations for #105

### Short-term (Investigation complete, implementation pending):
1. Add null checks to FFI allocation calls in `anofox_ts_forecast`
2. Return proper error instead of segfault

### Medium-term:
1. Add memory estimation before backtest execution
2. Warn or fail early if estimated memory exceeds threshold

### Long-term:
1. Convert `ts_backtest_auto_by` to table_in_out pattern
2. Process folds one at a time, stream results
3. Avoid materializing all intermediate CTEs

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
