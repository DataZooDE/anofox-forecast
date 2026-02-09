"""
Run all benchmarks for a specific dataset and group.

This script runs all available benchmarks (anofox + statsforecast) for the specified
dataset and frequency group. Currently supports M4 and M5 datasets.

Usage:
    # Run all benchmarks for M4 Daily
    uv run python run_all_benchmarks.py --dataset m4 --group Daily

    # Run all benchmarks for M5 Daily (M5 only has Daily)
    uv run python run_all_benchmarks.py --dataset m5 --group Daily
"""
import sys
from pathlib import Path
from typing import Optional

import fire

# Add benchmark root to sys.path to import shared modules
sys.path.insert(0, str(Path(__file__).resolve().parent))


def run_all(
    dataset: str = 'm4',
    group: str = 'Daily',
    benchmarks: Optional[str] = None
):
    """
    Run all benchmarks (anofox + statsforecast) for the specified dataset and group.

    Parameters
    ----------
    dataset : str
        Dataset identifier ('m4' or 'm5'). Default: 'm4'
    group : str
        Frequency group ('Daily', 'Hourly', 'Weekly' for M4; 'Daily' for M5).
        Default: 'Daily'
    benchmarks : str, optional
        Comma-separated list of benchmarks to run. If None, runs all benchmarks.
        Available: baseline, ets, theta, arima, mfles, mstl
        Example: 'baseline,ets,theta'
    """
    dataset = dataset.lower()
    group = group.capitalize()  # Normalize to 'Daily', 'Hourly', 'Weekly'

    # Validate dataset
    if dataset not in ['m4', 'm5']:
        print(f"Error: Unsupported dataset '{dataset}'. Supported datasets: m4, m5")
        return

    # Import benchmark functions dynamically based on dataset
    benchmark_module = __import__(f'{dataset}.baseline_benchmark.run', fromlist=['run'])
    run_baseline = benchmark_module.run
    
    benchmark_module = __import__(f'{dataset}.ets_benchmark.run', fromlist=['run'])
    run_ets = benchmark_module.run
    
    benchmark_module = __import__(f'{dataset}.arima_benchmark.run', fromlist=['run'])
    run_arima = benchmark_module.run
    
    benchmark_module = __import__(f'{dataset}.mfles_benchmark.run', fromlist=['run'])
    run_mfles = benchmark_module.run
    
    benchmark_module = __import__(f'{dataset}.mstl_benchmark.run', fromlist=['run'])
    run_mstl = benchmark_module.run

    # Theta benchmark - try custom structure first, fall back to standard factory
    try:
        theta_module = __import__(f'{dataset}.theta_benchmark.run', fromlist=['run_anofox', 'run', 'evaluate'])
        if hasattr(theta_module, 'run_anofox'):
            # Custom structure (M4): separate run_anofox, run_statsforecast, evaluate
            theta_anofox = theta_module.run_anofox
            theta_evaluate = theta_module.evaluate
            theta_sf_module = __import__(f'{dataset}.theta_benchmark.run_statsforecast', fromlist=['run_benchmark'])
            theta_statsforecast = theta_sf_module.run_benchmark
            THETA_CUSTOM = True
        else:
            # Standard factory structure (M5): uses run() which does anofox + statsforecast + evaluate
            theta_run = theta_module.run
            THETA_CUSTOM = False
        THETA_AVAILABLE = True
    except (ImportError, AttributeError):
        THETA_AVAILABLE = False
        THETA_CUSTOM = False
        theta_anofox = None
        theta_evaluate = None
        theta_statsforecast = None
        theta_run = None

    print(f"{'='*80}")
    print(f"RUNNING ALL BENCHMARKS - {dataset.upper()} {group}")
    print(f"{'='*80}\n")

    # Define all available benchmarks
    all_benchmarks = {
        'baseline': {
            'name': 'Baseline',
            'run': run_baseline,
            'has_statsforecast': True
        },
        'ets': {
            'name': 'ETS',
            'run': run_ets,
            'has_statsforecast': True
        },
        'theta': {
            'name': 'Theta',
            'run': None,  # Custom handling
            'has_statsforecast': True,
            'available': THETA_AVAILABLE
        },
        'arima': {
            'name': 'ARIMA',
            'run': run_arima,
            'has_statsforecast': True
        },
        'mfles': {
            'name': 'MFLES',
            'run': run_mfles,
            'has_statsforecast': True
        },
        'mstl': {
            'name': 'MSTL',
            'run': run_mstl,
            'has_statsforecast': True
        },
    }

    # Filter benchmarks if specified
    if benchmarks:
        if isinstance(benchmarks, (list, tuple)):
            requested = [b.strip().lower() for b in benchmarks]
        else:
            requested = [b.strip().lower() for b in benchmarks.split(',')]
        benchmarks_to_run = {k: v for k, v in all_benchmarks.items() if k in requested}
        if not benchmarks_to_run:
            print(f"Error: No valid benchmarks found in '{benchmarks}'")
            print(f"Available benchmarks: {', '.join(all_benchmarks.keys())}")
            return
    else:
        benchmarks_to_run = all_benchmarks

    total_benchmarks = len(benchmarks_to_run)
    current = 0

    for benchmark_key, benchmark_info in benchmarks_to_run.items():
        current += 1
        benchmark_name = benchmark_info['name']
        
        # Check if benchmark is available
        if benchmark_info.get('available', True) is False:
            print(f"\n{'='*80}")
            print(f"BENCHMARK {current}/{total_benchmarks}: {benchmark_name.upper()} - SKIPPED")
            print(f"{'='*80}\n")
            print(f"⚠️  {benchmark_name} benchmark is not available (missing dependencies)")
            continue
        
        print(f"\n{'='*80}")
        print(f"BENCHMARK {current}/{total_benchmarks}: {benchmark_name.upper()}")
        print(f"{'='*80}\n")

        try:
            if benchmark_key == 'theta':
                if THETA_CUSTOM:
                    # Custom structure (M4): separate steps
                    print(f"STEP 1: Running Anofox {benchmark_name} models...")
                    theta_anofox(group=group, dataset=dataset)

                    print(f"\nSTEP 2: Running Statsforecast {benchmark_name} models...")
                    theta_statsforecast(group=group, dataset=dataset)

                    print(f"\nSTEP 3: Evaluating {benchmark_name} forecasts...")
                    theta_evaluate(group=group, dataset=dataset)
                else:
                    # Standard factory structure (M5): run() handles everything
                    theta_run(group=group, dataset=dataset)
            else:
                # Other benchmarks use the standard run function
                benchmark_info['run'](group=group, dataset=dataset)
            
            print(f"\n✅ {benchmark_name} benchmark completed successfully!")
            
        except Exception as e:
            print(f"\n❌ Error running {benchmark_name} benchmark: {e}")
            import traceback
            traceback.print_exc()
            print(f"\nContinuing with next benchmark...\n")

    print(f"\n{'='*80}")
    print(f"ALL BENCHMARKS COMPLETE - {dataset.upper()} {group}")
    print(f"{'='*80}")
    print(f"\nResults saved in individual benchmark result directories:")
    for benchmark_key in benchmarks_to_run.keys():
        result_dir = Path(__file__).parent / dataset / f'{benchmark_key}_benchmark' / 'results'
        print(f"  - {benchmark_key}: {result_dir}")


if __name__ == '__main__':
    fire.Fire(run_all)

