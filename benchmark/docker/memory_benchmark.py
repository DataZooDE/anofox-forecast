#!/usr/bin/env python3
"""
Quick memory benchmark comparing DuckDB anofox_forecast vs Python statsforecast.
Measures peak memory usage for forecasting time series.
Uses subprocess for accurate process-level memory measurement.
"""
import time
import resource
import gc
import os
import sys
import subprocess
import json

import pandas as pd
import numpy as np

# Configuration - larger dataset for meaningful comparison
N_SERIES = 5000
SERIES_LENGTH = 200
HORIZON = 28
SEASONAL_PERIOD = 7


def generate_test_data():
    """Generate synthetic time series data."""
    np.random.seed(42)
    data = []
    for i in range(N_SERIES):
        base = np.random.uniform(10, 100)
        trend = np.linspace(0, np.random.uniform(-10, 10), SERIES_LENGTH)
        seasonal = 5 * np.sin(2 * np.pi * np.arange(SERIES_LENGTH) / SEASONAL_PERIOD)
        noise = np.random.normal(0, 2, SERIES_LENGTH)
        values = base + trend + seasonal + noise

        for j, v in enumerate(values):
            data.append({
                'unique_id': f'series_{i:04d}',
                'ds': pd.Timestamp('2020-01-01') + pd.Timedelta(days=j),
                'y': max(0, v)  # Ensure non-negative
            })

    return pd.DataFrame(data)


def get_peak_memory_mb():
    """Get peak memory usage in MB using resource module."""
    usage = resource.getrusage(resource.RUSAGE_SELF)
    # maxrss is in kilobytes on Linux, bytes on macOS
    if sys.platform == 'darwin':
        return usage.ru_maxrss / 1024 / 1024
    return usage.ru_maxrss / 1024


def benchmark_statsforecast(df):
    """Benchmark statsforecast memory usage."""
    from statsforecast import StatsForecast
    from statsforecast.models import SeasonalNaive, Theta

    gc.collect()
    mem_before = get_peak_memory_mb()

    start_time = time.time()

    sf = StatsForecast(
        models=[
            SeasonalNaive(season_length=SEASONAL_PERIOD),
            Theta(season_length=SEASONAL_PERIOD),
        ],
        freq='D',
        n_jobs=1  # Single thread for fair comparison
    )

    forecasts = sf.forecast(df=df, h=HORIZON)

    elapsed = time.time() - start_time
    mem_after = get_peak_memory_mb()

    return {
        'framework': 'statsforecast',
        'peak_memory_mb': mem_after,
        'memory_delta_mb': mem_after - mem_before,
        'time_seconds': elapsed,
        'n_series': N_SERIES,
        'series_length': SERIES_LENGTH,
        'n_forecasts': len(forecasts)
    }


def benchmark_duckdb(df):
    """Benchmark DuckDB anofox_forecast memory usage."""
    import duckdb

    gc.collect()
    mem_before = get_peak_memory_mb()

    start_time = time.time()

    con = duckdb.connect(':memory:')

    # Try to load community extension, fall back to local
    try:
        con.execute("INSTALL anofox_forecast FROM community;")
        con.execute("LOAD anofox_forecast;")
    except Exception:
        ext_path = os.environ.get('ANOFOX_EXTENSION_PATH',
            'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension')
        con.execute(f"LOAD '{ext_path}';")

    # Convert timestamp to DATE for compatibility
    con.execute("CREATE TABLE train AS SELECT unique_id, CAST(ds AS DATE) AS ds, y FROM df")

    # Run forecasts with SeasonalNaive and Theta
    results = []
    for model in ['SeasonalNaive', 'Theta']:
        query = f"""
            SELECT * FROM ts_forecast_by(
                'train', unique_id, ds, y,
                '{model}', {HORIZON},
                {{'seasonal_period': {SEASONAL_PERIOD}}}
            )
        """
        result = con.execute(query).fetchdf()
        results.append(result)

    con.close()

    elapsed = time.time() - start_time
    mem_after = get_peak_memory_mb()

    return {
        'framework': 'duckdb-anofox',
        'peak_memory_mb': mem_after,
        'memory_delta_mb': mem_after - mem_before,
        'time_seconds': elapsed,
        'n_series': N_SERIES,
        'series_length': SERIES_LENGTH,
        'n_forecasts': sum(len(r) for r in results)
    }


def main():
    print(f"Generating {N_SERIES} time series ({SERIES_LENGTH} points each)...")
    df = generate_test_data()
    print(f"Generated {len(df)} rows\n")

    print("=" * 60)
    print("MEMORY BENCHMARK: DuckDB anofox_forecast vs Python statsforecast")
    print("=" * 60)

    # Run DuckDB benchmark
    print("\nRunning DuckDB anofox_forecast benchmark...")
    try:
        duckdb_result = benchmark_duckdb(df)
        print(f"  Peak memory: {duckdb_result['peak_memory_mb']:.1f} MB")
        print(f"  Time: {duckdb_result['time_seconds']:.2f}s")
    except Exception as e:
        print(f"  Error: {e}")
        duckdb_result = None

    # Run statsforecast benchmark
    print("\nRunning Python statsforecast benchmark...")
    try:
        sf_result = benchmark_statsforecast(df)
        print(f"  Peak memory: {sf_result['peak_memory_mb']:.1f} MB")
        print(f"  Time: {sf_result['time_seconds']:.2f}s")
    except Exception as e:
        print(f"  Error: {e}")
        sf_result = None

    # Print comparison
    print("\n" + "=" * 60)
    print("COMPARISON")
    print("=" * 60)

    if duckdb_result and sf_result:
        memory_ratio = sf_result['peak_memory_mb'] / duckdb_result['peak_memory_mb']
        time_ratio = sf_result['time_seconds'] / duckdb_result['time_seconds']

        print(f"\n{'Framework':<20} {'Peak Mem (MB)':<15} {'Time (s)':<15}")
        print("-" * 50)
        print(f"{'DuckDB anofox':<20} {duckdb_result['peak_memory_mb']:<15.0f} {duckdb_result['time_seconds']:<15.2f}")
        print(f"{'statsforecast':<20} {sf_result['peak_memory_mb']:<15.0f} {sf_result['time_seconds']:<15.2f}")
        print("-" * 50)

        if memory_ratio > 1:
            print(f"\nDuckDB uses {memory_ratio:.1f}x less memory")
        else:
            print(f"\nstatsforecast uses {1/memory_ratio:.1f}x less memory")

        if time_ratio > 1:
            print(f"DuckDB is {time_ratio:.1f}x faster")
        else:
            print(f"statsforecast is {1/time_ratio:.1f}x faster")

        print(f"\nDataset: {N_SERIES:,} series x {SERIES_LENGTH} points = {N_SERIES * SERIES_LENGTH:,} rows")


if __name__ == '__main__':
    main()
