# Performance Analysis: `ts_fill_gaps` Function

## Current Implementation Summary

### Architecture
The `ts_fill_gaps` function is implemented as a DuckDB table function with `bind_replace` that dynamically generates SQL. The function fills missing timestamps in time series data by:
1. Generating a complete date range for each series using `GENERATE_SERIES`
2. Expanding this range using `UNNEST`
3. Left-joining the expanded range with original data to fill gaps with NULL

### Current Code Implementation (OPTIMIZED)

**Location**: `src/data_prep_bind_replace.cpp` (lines 21-128)

**Status**: ✅ **Optimized per expert recommendations** - Removed complex join condition, normalized types upfront

**Key SQL Pattern** (VARCHAR frequency - date-based):

```sql
WITH orig_aliased AS (
    SELECT 
        group_col AS __gid,
        CAST(date_col AS DATE) AS __did,  -- Normalize type once upfront
        value_col AS __vid,
        *
    FROM QUERY_TABLE('table_name')
),
frequency_parsed AS (
    SELECT 
        CASE 
            WHEN UPPER(TRIM('frequency')) IN ('1D', '1DAY') THEN INTERVAL '1 day'
            WHEN UPPER(TRIM('frequency')) IN ('30M', '30MIN', ...) THEN INTERVAL '30 minutes'
            -- ... other frequency mappings ...
            ELSE INTERVAL '1 day'
        END AS __interval
    FROM (SELECT 1) t
),
series_ranges AS (
    SELECT 
        __gid,
        MIN(__did) AS __min,
        MAX(__did) AS __max
    FROM orig_aliased
    GROUP BY __gid
),
grid AS (
    SELECT 
        sr.__gid,
        CAST(UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__interval)) AS DATE) AS __did
    FROM series_ranges sr
    CROSS JOIN frequency_parsed fp
),
with_original_data AS (
    SELECT 
        g.__gid,
        g.__did,
        oa.__vid,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did  -- Simple equality join
)
SELECT 
    with_original_data.* EXCLUDE (__gid, __did, __vid, group_col, date_col, value_col),
    with_original_data.__gid AS group_col,
    with_original_data.__did AS date_col,
    with_original_data.__vid AS value_col
FROM with_original_data
ORDER BY group_col, date_col
```

**Key Optimizations Applied**:
- ✅ Removed complex `(is_date AND ... OR ...)` join condition
- ✅ Normalized date types upfront (single cast at beginning)
- ✅ Simplified from 6 CTEs to 4 CTEs
- ✅ Enables DuckDB optimizer to use efficient hash joins

### Performance Bottlenecks Identified

#### 1. **Massive Intermediate Result Set from GENERATE_SERIES + UNNEST**
   - **Problem**: For each series, `GENERATE_SERIES` creates a complete date range, then `UNNEST` expands it into individual rows
   - **Impact**: 
     - Series with 1000 days → 1000 rows per series
     - 1000 series → 1,000,000 rows in `expanded` CTE
     - 10,000 series → 10,000,000 rows (if average 1000 days each)
   - **Memory**: All expanded rows must be materialized before the join

#### 2. **Inefficient LEFT JOIN on Large Expanded Dataset**
   - **Problem**: LEFT JOIN between potentially millions of expanded rows and original data
   - **Impact**: 
     - Join condition involves type checking (`__is_date_type`) and conditional comparisons
     - No index on the expanded dataset (it's a CTE)
     - DuckDB must scan the entire expanded dataset for each original row
   - **Complexity**: O(n × m) where n = expanded rows, m = original rows per group

#### 3. **Multiple Type Casting Operations**
   - **Problem**: 
     - `CAST(date_col AS DATE)` in `orig_aliased` for every row
     - `CAST(__did_raw AS DATE)` in `expanded` for DATE types
     - Conditional type checks in join conditions
   - **Impact**: Additional CPU overhead for every row processed

#### 4. **Final ORDER BY on Entire Result Set**
   - **Problem**: Sorting potentially millions of rows at the end
   - **Impact**: Requires full materialization and sorting of final result

#### 5. **Multiple CTE Materializations**
   - **Problem**: Each CTE may be materialized separately
   - **Impact**: Multiple passes over data, increased memory pressure

### Comparison with Polars

Polars likely uses:
- **Vectorized operations**: Processes entire columns at once
- **Direct indexing/hashing**: Uses hash maps or sorted indices for O(1) or O(log n) lookups
- **Range joins**: Specialized operations for time-based joins
- **Lazy evaluation**: Defers materialization until necessary
- **Native date range generation**: More efficient than SQL `GENERATE_SERIES`

## Suggested Performance Improvements

### Option 1: Use ASOF JOIN (Recommended)

DuckDB supports `ASOF JOIN` which is optimized for time-series joins. This avoids generating the full expanded dataset.

**Approach**:
```sql
WITH series_ranges AS (
    SELECT 
        group_col,
        MIN(date_col) AS __min,
        MAX(date_col) AS __max
    FROM table_name
    GROUP BY group_col
),
date_series AS (
    SELECT 
        sr.group_col,
        UNNEST(GENERATE_SERIES(sr.__min, sr.__max, INTERVAL '1 day')) AS date_col
    FROM series_ranges sr
)
SELECT 
    ds.group_col,
    ds.date_col,
    orig.value_col
FROM date_series ds
ASOF JOIN table_name orig 
    ON ds.group_col = orig.group_col 
    AND ds.date_col >= orig.date_col
    QUALIFY ROW_NUMBER() OVER (
        PARTITION BY ds.group_col, ds.date_col 
        ORDER BY orig.date_col DESC
    ) = 1
```

**Benefits**:
- ASOF JOIN is optimized for time-series data
- Avoids full cartesian expansion
- More efficient than LEFT JOIN for this use case

**Limitations**:
- Requires DuckDB version with ASOF JOIN support
- May need adjustment for exact date matching vs. nearest-neighbor

### Option 2: Use Window Functions with LEAD/LAG

Instead of generating all dates, identify gaps and fill them using window functions.

**Approach**:
```sql
WITH ordered AS (
    SELECT 
        group_col,
        date_col,
        value_col,
        LAG(date_col) OVER (PARTITION BY group_col ORDER BY date_col) AS prev_date,
        LEAD(date_col) OVER (PARTITION BY group_col ORDER BY date_col) AS next_date
    FROM table_name
),
gaps AS (
    SELECT 
        group_col,
        date_col + INTERVAL '1 day' AS gap_date,
        value_col
    FROM ordered
    WHERE date_col - prev_date > INTERVAL '1 day'
    -- Generate intermediate dates using recursive CTE or GENERATE_SERIES per gap
)
-- Union original data with filled gaps
SELECT * FROM table_name
UNION ALL
SELECT group_col, gap_date, NULL AS value_col FROM gaps
ORDER BY group_col, date_col
```

**Benefits**:
- Only generates dates for actual gaps
- Smaller intermediate result set
- Leverages window functions (typically optimized)

**Limitations**:
- More complex logic for generating intermediate dates
- Still requires some date generation for large gaps

### Option 3: Optimize Current Approach

Keep the current pattern but optimize:

**Changes**:
1. **Remove unnecessary type casts**: Pre-determine date type from schema instead of runtime checks
2. **Use hash join hints**: Ensure DuckDB uses hash join for the LEFT JOIN
3. **Partition by group_col**: Process groups in parallel
4. **Remove final ORDER BY if not needed**: Let caller sort if required
5. **Use RANGE JOIN if available**: DuckDB's range join might be more efficient

**Optimized SQL Pattern**:
```sql
WITH orig_aliased AS (
    SELECT 
        group_col AS __gid,
        date_col AS __did,
        value_col AS __vid,
        *
    FROM QUERY_TABLE('table_name')
),
series_ranges AS (
    SELECT 
        __gid,
        MIN(__did) AS __min,
        MAX(__did) AS __max
    FROM orig_aliased
    GROUP BY __gid
),
expanded AS (
    SELECT 
        sr.__gid,
        UNNEST(GENERATE_SERIES(sr.__min, sr.__max, INTERVAL '1 day')) AS __did
    FROM series_ranges sr
)
SELECT 
    e.__gid AS group_col,
    e.__did AS date_col,
    oa.__vid AS value_col,
    oa.* EXCLUDE (__gid, __did, __vid, group_col, date_col, value_col)
FROM expanded e
LEFT JOIN orig_aliased oa 
    ON e.__gid = oa.__gid 
    AND e.__did = oa.__did
-- Remove ORDER BY or make it optional
```

### Option 4: Native C++ Implementation (Long-term)

For maximum performance, implement as a native DuckDB table function:

**Approach**:
- Read input data in chunks
- For each group:
  - Determine min/max date
  - Generate date range in C++ (more efficient than SQL)
  - Use hash map to look up original values
  - Output rows directly without intermediate materialization

**Benefits**:
- Full control over memory usage
- Can process groups in parallel
- Avoid SQL overhead
- Can use SIMD for date arithmetic

**Trade-offs**:
- More complex implementation
- Requires maintaining C++ code
- Less flexible than SQL-based approach

## Recommended Implementation Strategy (Updated per Expert Feedback)

### Phase 1: Immediate Fix (✅ IMPLEMENTED)
**Status**: Completed - Removed complex join condition, normalized types upfront

**Changes Applied**:
1. ✅ **Normalized date types upfront**: Cast `date_col` to `DATE` once at the beginning in `orig_aliased` CTE
2. ✅ **Simplified join condition**: Removed `(is_date AND ... OR ...)` predicate, now uses simple `ON gid = gid AND date = date`
3. ✅ **Removed unnecessary type checks**: Eliminated `__is_date_type` logic and `__did_normalized` column
4. ✅ **Streamlined CTEs**: Reduced from 6 CTEs to 4 CTEs (removed `expanded_raw` and type-checking logic)

**Expected Impact**: 50-80% performance improvement by enabling hash joins instead of nested loop joins

### Phase 2: Strategic Optimization (Next)
1. **Test current optimized version**: Benchmark against previous implementation
2. **Consider ASOF JOIN for forward-fill variant**: If LOCF (Last Observation Carried Forward) is needed
3. **Evaluate Grid-CTE pattern**: May already be optimal with simplified join

### Phase 3: Advanced (If Still Needed)
1. **Implement Native C++ Table Function**: For datasets >10M rows
2. **Streaming gap filling**: O(1) memory usage with lazy row emission
3. **Maintain SQL version**: As fallback for edge cases

## Performance Metrics to Track

1. **Execution time**: Total query time
2. **Memory usage**: Peak memory during execution
3. **Intermediate result sizes**: Size of `expanded` CTE
4. **Join efficiency**: Time spent in LEFT JOIN
5. **Scalability**: Performance with varying:
   - Number of series (1, 10, 100, 1000, 10000)
   - Series length (10, 100, 1000, 10000 days)
   - Gap percentage (0%, 10%, 50%, 90%)

## Example Performance Test Query

```sql
-- Create test data
CREATE TABLE test_data AS
SELECT 
    (i % 1000) AS series_id,
    '2020-01-01'::DATE + (j * 7) AS date_col,  -- Weekly data with gaps
    RANDOM() * 100 AS value
FROM generate_series(1, 1000) t(i)
CROSS JOIN generate_series(0, 100) t2(j);

-- Measure performance
EXPLAIN ANALYZE
SELECT * FROM ts_fill_gaps('test_data', 'series_id', 'date_col', 'value', '1d');
```

## Expert Review and Recommendations

### Expert Confirmation

The expert confirmed that:
1. **The bottleneck is real**: The "explode-then-filter" anti-pattern is causing the performance issue
2. **ASOF JOIN is available**: DuckDB has excellent `ASOF JOIN` support optimized for sorted time-series data
3. **The join condition is the problem**: The complex `(is_date AND ... OR ...)` predicate prevents optimizer from using efficient hash joins

### Expert Recommendations

#### Immediate Fix (Low Effort, High Impact)

**Problem**: The complex join condition with type checking prevents DuckDB's optimizer from using efficient hash joins.

**Solution**: Simplify the join to a direct equality check by normalizing types upfront.

**Key Changes**:
1. Cast `date_col` to `DATE` once at the beginning (if needed)
2. Remove the `(e.__is_date_type AND ... OR ...)` logic from join condition
3. Use simple `ON gid = gid AND date = date` join

#### Strategic Fix (High Impact)

**Grid-CTE Pattern** with clean LEFT JOIN:

```sql
WITH series_ranges AS (
    SELECT 
        group_col,
        MIN(date_col) AS min_d,
        MAX(date_col) AS max_d
    FROM QUERY_TABLE('table_name')
    GROUP BY group_col
),
grid AS (
    SELECT 
        sr.group_col,
        UNNEST(GENERATE_SERIES(sr.min_d, sr.max_d, INTERVAL '1 day')) AS grid_date
    FROM series_ranges sr
)
SELECT 
    grid.group_col,
    grid.grid_date AS date_col,
    orig.value_col
FROM grid
LEFT JOIN QUERY_TABLE('table_name') orig 
    ON grid.group_col = orig.group_col 
    AND grid.grid_date = orig.date_col
ORDER BY grid.group_col, grid.grid_date
```

**For Forward Fill (LOCF)**, use ASOF JOIN:
```sql
FROM grid
ASOF JOIN orig 
ON grid.group_col = orig.group_col AND grid.grid_date >= orig.date_col
```

#### Long-term Solution

For datasets >10M rows, implement a **Native C++ Table Function** that:
- Streams rows lazily (O(1) memory)
- Emits gaps on-the-fly without materializing full date ranges
- Processes groups in parallel

### Answers to Questions

1. **ASOF JOIN availability**: ✅ Yes, DuckDB has excellent `ASOF JOIN` support optimized for sorted data
2. **GENERATE_SERIES optimization**: Fast, but `UNNEST` materializes results in current pipeline
3. **Range joins**: ASOF JOIN is the specialized operation for time-series
4. **Parallelization**: DuckDB parallelizes `GROUP BY` well; avoid `ORDER BY` in CTEs (forces single-threaded merge)
5. **Memory management**: `UNNEST(GENERATE_SERIES(...))` is the memory hog; only fix is to not generate rows (Native C++) or use streaming
6. **Index suggestions**: ❌ DuckDB indexes are NOT used for CTEs or temporary results; only help with highly selective filters on base tables

## Implementation Plan

### Phase 1: Immediate Fix (Implement Now)
1. Remove complex join condition with type checking
2. Normalize date types upfront (cast once at beginning)
3. Use simple equality join: `ON gid = gid AND date = date`
4. Remove unnecessary type casts from join predicates

### Phase 2: Strategic Fix (Next)
1. Refactor to Grid-CTE pattern shown above
2. Ensure both sides of join are sorted for optimal performance
3. Test with real-world datasets

### Phase 3: Advanced (If Needed)
1. Implement Native C++ Table Function for streaming gap filling
2. Maintain SQL version as fallback

## Implementation Changes Applied

### Before (Slow Implementation)

**CTE Structure** (6 CTEs):
```sql
WITH orig_aliased AS (
    SELECT group_col AS __gid, date_col AS __did, value_col AS __vid, *,
           CAST(date_col AS DATE) AS __did_normalized  -- Extra cast
    FROM QUERY_TABLE('table_name')
),
series_ranges AS (
    SELECT __gid, MIN(__did) AS __min, MAX(__did) AS __max,
           MIN(__did) = CAST(MIN(__did) AS DATE) AS __is_date_type  -- Type check
    FROM orig_aliased GROUP BY __gid
),
expanded_raw AS (  -- Extra CTE
    SELECT sr.__gid, UNNEST(GENERATE_SERIES(...)) AS __did_raw, sr.__is_date_type
    FROM series_ranges sr CROSS JOIN frequency_parsed fp
),
expanded AS (  -- Another CTE for type casting
    SELECT __gid, CASE WHEN __is_date_type THEN CAST(__did_raw AS DATE) ELSE __did_raw END AS __did
    FROM expanded_raw
),
with_original_data AS (
    SELECT e.__gid, COALESCE(oa.__did, e.__did) AS __did, oa.__vid, oa.* EXCLUDE (...)
    FROM expanded e
    LEFT JOIN orig_aliased oa ON e.__gid = oa.__gid 
        AND (  -- COMPLEX OR CONDITION - PREVENTS HASH JOIN
            (e.__is_date_type AND e.__did = oa.__did_normalized)
            OR
            (NOT e.__is_date_type AND e.__did = oa.__did)
        )
)
```

**Problems**:
- Complex join condition prevents hash join optimization
- Multiple type casts and checks
- Extra CTEs increase materialization overhead

### After (Optimized Implementation)

**CTE Structure** (4 CTEs):
```sql
WITH orig_aliased AS (
    SELECT group_col AS __gid, 
           CAST(date_col AS DATE) AS __did,  -- Normalize once upfront
           value_col AS __vid, *
    FROM QUERY_TABLE('table_name')
),
series_ranges AS (
    SELECT __gid, MIN(__did) AS __min, MAX(__did) AS __max
    FROM orig_aliased GROUP BY __gid
),
grid AS (  -- Renamed from 'expanded' to match expert terminology
    SELECT sr.__gid,
           CAST(UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__interval)) AS DATE) AS __did
    FROM series_ranges sr CROSS JOIN frequency_parsed fp
),
with_original_data AS (
    SELECT g.__gid, g.__did, oa.__vid, oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did  -- SIMPLE EQUALITY
)
```

**Improvements**:
- ✅ Simple equality join enables hash join optimization
- ✅ Single type normalization upfront
- ✅ Removed unnecessary CTEs and type checking
- ✅ Cleaner execution plan for better parallelization

### Key Optimizations
1. **Normalized types upfront**: `CAST(date_col AS DATE) AS __did` in first CTE
2. **Removed OR predicate**: Enables DuckDB optimizer to use hash joins
3. **Simplified CTE structure**: Fewer materializations, cleaner execution plan
4. **Grid naming**: Renamed `expanded` to `grid` to match expert terminology

### Expected Performance Improvement
- **50-80% faster**: By enabling hash joins instead of nested loop joins
- **Lower memory usage**: Fewer intermediate CTEs to materialize
- **Better parallelization**: Simpler query plan allows better GROUP BY parallelization

## Code References

- **Implementation**: `src/data_prep_bind_replace.cpp:21-128` (TSFillGapsBindReplace) - **OPTIMIZED**
- **Integer frequency variant**: `src/data_prep_bind_replace.cpp:369-440` (TSFillGapsIntegerBindReplace) - Already optimized
- **Registration**: `src/data_prep_macros.cpp:738-762`

