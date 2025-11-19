#!/usr/bin/env python3
"""
Run Anofox Theta benchmarks on M4 Daily data.
"""
import sys
from pathlib import Path

# Add benchmark src to path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))

from common.data import get_data
from common.anofox_runner import run_anofox_benchmark

def run_theta_benchmarks():
    """Run all Anofox Theta variants on M4 Daily dataset."""
    
    # Load M4 Daily data using proper loader
    dataset = 'm4'
    group = 'Daily'
    print(f"Loading {dataset.upper()} {group} training data...")
    train_df, horizon, freq, seasonality = get_data(dataset, group, train=True)
    
    # Define Theta models to benchmark
    models_config = [
        {
            'name': 'Theta',
            'params': lambda s: f"{{'seasonal_period': {s}, 'theta_param': 2.0}}"
        },
        {
            'name': 'DynamicTheta',
            'params': lambda s: f"{{'seasonal_period': {s}, 'theta_param': 2.0}}"
        },
        {
            'name': 'OptimizedTheta',
            'params': lambda s: f"{{'seasonal_period': {s}}}"
        },
        {
            'name': 'DynamicOptimizedTheta',
            'params': lambda s: f"{{'seasonal_period': {s}}}"
        },
    ]
    
    output_dir = Path(__file__).parent / 'results'
    
    print(f"\n{'='*70}")
    print(f"  ANOFOX THETA BENCHMARK - {dataset.upper()} {group}")
    print(f"{'='*70}")
    print(f"Series: {train_df['unique_id'].nunique()}")
    print(f"Horizon: {horizon}")
    print(f"Seasonality: {seasonality}")
    print(f"Models: {len(models_config)}")
    print(f"{'='*70}\n")
    
    # Run benchmarks
    results = run_anofox_benchmark(
        benchmark_name='theta',
        train_df=train_df,
        horizon=horizon,
        seasonality=seasonality,
        models_config=models_config,
        output_dir=output_dir,
        group=group
    )
    
    # Print summary
    print(f"\n{'='*70}")
    print(f"  BENCHMARK RESULTS SUMMARY")
    print(f"{'='*70}")
    for result in results:
        print(f"{result['model']:25s} {result['time_seconds']:8.2f}s  ({result['series_count']} series)")
    print(f"{'='*70}\n")
    
    print("✅ All Theta benchmarks completed successfully!")
    print(f"Results saved to: {output_dir}")


if __name__ == '__main__':
    run_theta_benchmarks()

