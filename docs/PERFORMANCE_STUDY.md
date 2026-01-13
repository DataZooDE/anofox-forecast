# Performance Study: MSTL/STL Bottleneck Analysis

**Issue:** forecast-extension-1ys
**Date:** 2026-01-13
**Author:** Performance analysis for 100k series scenario

## Executive Summary

This study investigates performance bottlenecks when processing large datasets (100k+ time series) with the forecast extension. The analysis identifies several critical bottlenecks in period detection methods, while finding that the core MSTL decomposition is relatively efficient.

**Key Finding:** The bottleneck is NOT in MSTL decomposition itself, but in certain period detection methods. For 100k series scenarios, using fast methods (FFT, autoperiod) keeps processing time reasonable (~2-3 seconds), while slow methods (SAZED, SSA, Matrix Profile) would take hours.

## Benchmark Results

### 1. MSTL Decomposition Performance

| Series Length | Single Period | 3 Periods |
|--------------|---------------|-----------|
| 100          | 2.8µs         | 2.4µs     |
| 500          | 41µs          | 45µs      |
| 1,000        | 157µs         | 176µs     |
| 5,000        | 3.4ms         | 3.4ms     |
| 10,000       | 12.8ms        | 13.2ms    |

**Complexity:** O(n × periods) - Linear scaling, very efficient.

### 2. Period Detection Performance (Critical Bottleneck)

| Method        | n=500    | n=1000   | n=2000     | Complexity |
|---------------|----------|----------|------------|------------|
| FFT           | 75µs     | 203µs    | 186µs      | O(n log n) |
| Autoperiod    | 70µs     | 128µs    | 221µs      | O(n log n) |
| AIC           | 874µs    | 1.8ms    | 3.5ms      | O(n × candidates) |
| ACF           | 288µs    | 1.2ms    | 4.1ms      | O(n × max_lag) |
| STL           | 895µs    | 4.1ms    | 17.3ms     | O(n × candidates × period) |
| Lomb-Scargle  | 18ms     | 35ms     | 75ms       | O(n × n_freq) |
| **Matrix Profile** | **11ms** | **85ms** | **665ms** | **O(n²)** |
| **SSA**       | **43ms** | **219ms**| **1.05s**  | **O(L² × K)** |
| **SAZED**     | **40ms** | **161ms**| **642ms**  | **O(n²)** |

### 3. 100k Series Scalability

Processing 10,000 series (length=100 each):

| Operation              | Time      | Extrapolated 100k |
|-----------------------|-----------|-------------------|
| MSTL Decompose        | 23ms      | ~230ms            |
| Period Detection (FFT)| 213ms     | ~2.1s             |
| Forecast (Naive)      | 92ms      | ~920ms            |
| Forecast (MSTL)       | 13ms      | ~130ms            |

### 4. Large Series Analysis

Single series with n=50,000:

| Method               | Time       |
|---------------------|------------|
| MSTL Decompose      | 336ms      |
| STL Period Detect   | 11.4s      |

## Identified Bottlenecks

### Critical (O(n²) or worse)

1. **SAZED Period Detection** (`periods.rs:1250`)
   - Uses naive O(n²) DFT instead of FFT
   - Impact: 642ms for n=2000 vs 186µs for FFT
   - **Improvement:** Replace with rustfft or use fdars-core FFT
   - **Estimated speedup:** 1000x+

2. **SSA Period Detection** (`periods.rs:791`)
   - Power iteration for eigenvalues: O(L² × K × iterations)
   - Builds full covariance matrix: O(L²) memory
   - Impact: 1.05s for n=2000
   - **Improvement:** Use faer's eigenvalue decomposition (already a dependency)
   - **Estimated speedup:** 10-50x

3. **Matrix Profile Period Detection** (`periods.rs:1125`)
   - Naive O(n²) distance computation
   - Impact: 665ms for n=2000
   - **Improvement:** Implement STOMP/SCRIMP algorithms
   - **Estimated speedup:** 10-100x (depending on implementation)

### Moderate

4. **STL Period Detection** (`periods.rs:943`)
   - Performs full MSTL decomposition for each candidate period
   - Impact: 17ms for n=2000, 11.4s for n=50000
   - **Improvement:** Use early termination, cache intermediate results
   - **Estimated speedup:** 2-5x

5. **Lomb-Scargle** (`periods.rs:513`)
   - O(n × n_frequencies) with n_frequencies=1000 default
   - Impact: 75ms for n=2000
   - **Improvement:** Use NFFT (Non-uniform FFT) or reduce n_frequencies
   - **Estimated speedup:** 5-10x

### Already Efficient

- **FFT Period Detection:** Uses fdars-core, O(n log n)
- **MSTL Decomposition:** Custom implementation, O(n × periods)
- **ACF Period Detection:** Could be optimized with FFT-based autocorrelation

## Recommendations

### Immediate Actions (This Extension)

1. **Replace SAZED DFT with FFT**
   ```rust
   // Current: O(n²) naive DFT
   for k in 0..padded_len/2 {
       for t in 0..padded_len {
           // manual DFT computation
       }
   }

   // Improved: Use rustfft or fdars-core
   use rustfft::{FftPlanner, num_complex::Complex};
   let mut planner = FftPlanner::new();
   let fft = planner.plan_fft_forward(padded_len);
   fft.process(&mut buffer);
   ```

2. **Use faer for SSA eigendecomposition**
   ```rust
   // Current: manual power iteration
   // Improved: use faer's efficient SVD
   use faer::decomposition::svd;
   let svd = trajectory_matrix.svd();
   ```

3. **Add method recommendations in API**
   - Warn users about slow methods for large datasets
   - Default to FFT-based methods for auto-detection

### Future Improvements (Upstream Crates)

1. **fdars-core improvements:**
   - Add optimized Matrix Profile (STOMP algorithm)
   - Expose parallel period detection

2. **Consider augurs-mstl:**
   - The augurs ecosystem has optimized MSTL implementations
   - May provide better performance for very large series

### Configuration Recommendations

For 100k series workloads:

```sql
-- Use fast period detection
SELECT forecast_detect_periods(values, 'fft') ...

-- Avoid slow methods
-- DO NOT USE: 'sazed', 'ssa', 'matrix_profile' for large datasets

-- For batch processing, consider disabling auto-detection
SELECT forecast(values, 'MSTL', 12, ..., false) -- auto_detect=false
```

## Conclusion

The MSTL/STL implementation in this extension is efficient for the core decomposition task. The performance bottleneck lies in certain period detection methods that use O(n²) algorithms:

| Method | Status | Action Required |
|--------|--------|-----------------|
| MSTL Decompose | Fast | None |
| FFT Detection | Fast | None |
| Autoperiod | Fast | None |
| AIC | Acceptable | Monitor |
| ACF | Acceptable | Could optimize with FFT |
| STL Detection | Slow | Optimize for large n |
| Lomb-Scargle | Slow | Use NFFT or reduce frequencies |
| Matrix Profile | Very Slow | Implement STOMP |
| SSA | Very Slow | Use faer eigendecomposition |
| SAZED | Very Slow | Replace with FFT |

**Priority:** Fix SAZED (easiest, biggest impact), then SSA, then Matrix Profile.

## SQL Interface Benchmark Results

The following benchmarks were run through the DuckDB SQL interface to validate end-to-end performance.

### 100k Series Benchmark

**Dataset:** 100,000 series × 100 points each = 10 million rows

| Operation | Time | Per Series |
|-----------|------|------------|
| Data Generation (10M rows) | 2.5s | - |
| **MSTL Decomposition (100k series)** | **1.18s** | **11.8µs** |
| Period Detection FFT (1k series) | 1ms | 1µs |

### Forecast Scalability (MSTL Model)

| Series Count | Time | Per Series |
|-------------|------|------------|
| 1,000 | 42ms | 42µs |
| 10,000 | 205ms | 20.5µs |
| 50,000 | 761ms | 15.2µs |
| **100,000** | **1.6s** | **16µs** |

**Key Finding:** Linear scaling with excellent throughput. 100k series forecasted in 1.6 seconds.

### Model Comparison (10k series × 100 points)

| Model | Time | Notes |
|-------|------|-------|
| MSTL | 147ms | Fastest |
| AutoMSTL | 150ms | Similar |
| SES | 187ms | - |
| HoltWinters | 192ms | - |
| Naive | 216ms | Baseline |

**Surprising Result:** MSTL is actually faster than simpler models at scale, likely due to vectorized operations.

### Longer Series Test (1k series × 500 points)

| Operation | Time |
|-----------|------|
| MSTL Decomposition (2 periods) | 15ms |
| MSTL Forecast | 359ms |

### Short Series Fallback Behavior

MSTL handles series shorter than 2 seasonal periods gracefully:

| Series Length | Decomposition Result |
|--------------|---------------------|
| 5 points | Trend-only (OK) |
| 10 points | Trend-only (OK) |
| 15 points | Trend-only (OK) |
| 20 points | Trend-only (OK) |
| 23 points | Trend-only (OK) |
| **24 points** | **Full decomposition** (minimum) |
| 25+ points | Full decomposition |

For period=12, the minimum is 24 points (2 × period). Series below this threshold receive a trend-only decomposition without error.

## Appendix: Benchmark Code

### Rust Benchmarks
See `crates/anofox-fcst-core/benches/mstl_perf.rs` for the Rust benchmark implementation.

To run:
```bash
cargo bench --bench mstl_perf -p anofox-fcst-core
```

### SQL Benchmarks
See `benchmark/mstl/` for SQL-based benchmarks:
- `mstl_100k_series_perf.sql` - 100k series performance test
- `mstl_short_series_fallback.sql` - Short series fallback behavior test

To run:
```bash
duckdb -unsigned < benchmark/mstl/mstl_100k_series_perf.sql
duckdb -unsigned < benchmark/mstl/mstl_short_series_fallback.sql
```
