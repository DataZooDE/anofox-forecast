#!/usr/bin/env python3
"""
M5 AutoARIMA Benchmark: DuckDB anofox_forecast vs Python statsforecast

Measures execution time and peak memory usage for AutoARIMA forecasting
on the M5 competition dataset (30,490 time series).

Usage:
    uv run python docker/m5_autoarima_benchmark.py [--limit N]
"""
import argparse
import gc
import os
import resource
import sys
import time

import pandas as pd

# Configuration
M5_PARQUET = '/home/simonm/projects/forecasting/test/m5_full.parquet'
HORIZON = 28
SEASONAL_PERIOD = 7
SPLIT_DATE = '2016-04-25'


def get_peak_memory_mb():
    """Get peak memory usage in MB using resource module."""
    usage = resource.getrusage(resource.RUSAGE_SELF)
    if sys.platform == 'darwin':
        return usage.ru_maxrss / 1024 / 1024
    return usage.ru_maxrss / 1024


def load_m5_data(limit: int = None):
    """Load M5 training data."""
    import duckdb

    con = duckdb.connect(':memory:')

    if limit:
        # Get first N unique item_ids
        query = f"""
            WITH unique_items AS (
                SELECT DISTINCT item_id FROM '{M5_PARQUET}'
                LIMIT {limit}
            )
            SELECT m.item_id, CAST(m.ds AS DATE) as ds, m.y
            FROM '{M5_PARQUET}' m
            JOIN unique_items u ON m.item_id = u.item_id
            WHERE m.ds < '{SPLIT_DATE}'
            ORDER BY m.item_id, m.ds
        """
    else:
        query = f"""
            SELECT item_id, CAST(ds AS DATE) as ds, y
            FROM '{M5_PARQUET}'
            WHERE ds < '{SPLIT_DATE}'
            ORDER BY item_id, ds
        """

    df = con.execute(query).fetchdf()
    con.close()

    # Rename for statsforecast compatibility
    df = df.rename(columns={'item_id': 'unique_id'})

    return df


def benchmark_duckdb(train_df, limit: int = None):
    """Benchmark DuckDB anofox_forecast AutoARIMA."""
    import duckdb

    gc.collect()
    mem_before = get_peak_memory_mb()

    start_time = time.time()

    con = duckdb.connect(':memory:', config={'allow_unsigned_extensions': 'true'})

    # Load extension - try community first, then local
    try:
        con.execute("INSTALL anofox_forecast FROM community;")
        con.execute("LOAD anofox_forecast;")
    except Exception:
        ext_path = os.environ.get('ANOFOX_EXTENSION_PATH',
            '../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension')
        con.execute(f"LOAD '{ext_path}';")

    # Load data
    con.execute("CREATE TABLE train AS SELECT * FROM train_df")

    # Run AutoARIMA forecast
    query = f"""
        SELECT * FROM ts_forecast_by(
            'train', unique_id, ds, y,
            'AutoARIMA', {HORIZON},
            {{'seasonal_period': {SEASONAL_PERIOD}}}
        )
    """

    result = con.execute(query).fetchdf()
    con.close()

    elapsed = time.time() - start_time
    mem_after = get_peak_memory_mb()

    return {
        'framework': 'DuckDB anofox',
        'model': 'AutoARIMA',
        'peak_memory_mb': mem_after,
        'time_seconds': elapsed,
        'n_series': train_df['unique_id'].nunique(),
        'n_rows': len(train_df),
        'n_forecasts': len(result)
    }


def benchmark_statsforecast(train_df, limit: int = None):
    """Benchmark Python statsforecast AutoARIMA."""
    from statsforecast import StatsForecast
    from statsforecast.models import AutoARIMA

    gc.collect()
    mem_before = get_peak_memory_mb()

    start_time = time.time()

    sf = StatsForecast(
        models=[AutoARIMA(season_length=SEASONAL_PERIOD)],
        freq='D',
        n_jobs=1  # Single thread for fair comparison
    )

    forecasts = sf.forecast(df=train_df, h=HORIZON)

    elapsed = time.time() - start_time
    mem_after = get_peak_memory_mb()

    return {
        'framework': 'statsforecast',
        'model': 'AutoARIMA',
        'peak_memory_mb': mem_after,
        'time_seconds': elapsed,
        'n_series': train_df['unique_id'].nunique(),
        'n_rows': len(train_df),
        'n_forecasts': len(forecasts)
    }


def main():
    parser = argparse.ArgumentParser(description='M5 AutoARIMA Benchmark')
    parser.add_argument('--limit', type=int, default=None,
                       help='Limit to N series (default: all 30,490)')
    parser.add_argument('--duckdb-only', action='store_true',
                       help='Run only DuckDB benchmark')
    parser.add_argument('--statsforecast-only', action='store_true',
                       help='Run only statsforecast benchmark')
    args = parser.parse_args()

    print("=" * 70)
    print("M5 AutoARIMA BENCHMARK")
    print("=" * 70)

    # Load data
    print(f"\nLoading M5 training data" + (f" (limit: {args.limit} series)" if args.limit else "") + "...")
    train_df = load_m5_data(args.limit)
    n_series = train_df['unique_id'].nunique()
    n_rows = len(train_df)
    print(f"Loaded {n_rows:,} rows from {n_series:,} series")
    print(f"Date range: {train_df['ds'].min()} to {train_df['ds'].max()}")
    print(f"Forecast horizon: {HORIZON} days, Seasonality: {SEASONAL_PERIOD}")

    results = []

    # Run DuckDB benchmark
    if not args.statsforecast_only:
        print(f"\n{'='*70}")
        print("Running DuckDB anofox AutoARIMA...")
        print(f"{'='*70}")
        try:
            duckdb_result = benchmark_duckdb(train_df, args.limit)
            print(f"  Time: {duckdb_result['time_seconds']:.1f}s")
            print(f"  Peak memory: {duckdb_result['peak_memory_mb']:.0f} MB")
            print(f"  Forecasts: {duckdb_result['n_forecasts']:,}")
            results.append(duckdb_result)
        except Exception as e:
            print(f"  Error: {e}")
            import traceback
            traceback.print_exc()

    # Run statsforecast benchmark
    if not args.duckdb_only:
        print(f"\n{'='*70}")
        print("Running Python statsforecast AutoARIMA...")
        print(f"{'='*70}")
        try:
            sf_result = benchmark_statsforecast(train_df, args.limit)
            print(f"  Time: {sf_result['time_seconds']:.1f}s")
            print(f"  Peak memory: {sf_result['peak_memory_mb']:.0f} MB")
            print(f"  Forecasts: {sf_result['n_forecasts']:,}")
            results.append(sf_result)
        except Exception as e:
            print(f"  Error: {e}")
            import traceback
            traceback.print_exc()

    # Print comparison
    if len(results) == 2:
        print(f"\n{'='*70}")
        print("COMPARISON SUMMARY")
        print(f"{'='*70}")

        duckdb_r = results[0]
        sf_r = results[1]

        time_ratio = sf_r['time_seconds'] / duckdb_r['time_seconds']
        mem_ratio = sf_r['peak_memory_mb'] / duckdb_r['peak_memory_mb']

        print(f"\n{'Metric':<25} {'DuckDB anofox':<20} {'statsforecast':<20} {'Advantage':<15}")
        print("-" * 80)
        print(f"{'Execution Time':<25} {duckdb_r['time_seconds']:.1f}s{'':<14} {sf_r['time_seconds']:.1f}s{'':<14} ", end="")
        if time_ratio > 1:
            print(f"DuckDB {time_ratio:.1f}x faster")
        else:
            print(f"statsforecast {1/time_ratio:.1f}x faster")

        print(f"{'Peak Memory':<25} {duckdb_r['peak_memory_mb']:.0f} MB{'':<13} {sf_r['peak_memory_mb']:.0f} MB{'':<13} ", end="")
        if mem_ratio > 1:
            print(f"DuckDB {mem_ratio:.1f}x less")
        else:
            print(f"statsforecast {1/mem_ratio:.1f}x less")

        print("-" * 80)
        print(f"\nDataset: M5 ({n_series:,} series, {n_rows:,} rows)")
        print(f"Model: AutoARIMA (seasonal_period={SEASONAL_PERIOD})")
        print(f"Horizon: {HORIZON} days")

    elif len(results) == 1:
        r = results[0]
        print(f"\n{'='*70}")
        print(f"RESULT: {r['framework']}")
        print(f"{'='*70}")
        print(f"Time: {r['time_seconds']:.1f}s")
        print(f"Peak Memory: {r['peak_memory_mb']:.0f} MB")
        print(f"Dataset: M5 ({n_series:,} series, {n_rows:,} rows)")


if __name__ == '__main__':
    main()
