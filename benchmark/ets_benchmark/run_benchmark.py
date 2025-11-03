"""
Exponential Smoothing (ETS) models benchmark runner.

Main entry point for running ETS model benchmarks comparing
Anofox and Statsforecast implementations on M4 Competition datasets.
"""
import subprocess
import sys
from pathlib import Path

import fire


def run(group: str = 'Daily'):
    """
    Run complete baseline benchmark: Anofox, Statsforecast, and evaluation.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"="*80)
    print(f"EXPONENTIAL SMOOTHING (ETS) MODELS BENCHMARK - M4 {group}")
    print(f"="*80)

    # Run Anofox ETS models
    print(f"\n{'='*80}")
    print(f"STEP 1/3: Running Anofox ETS Models")
    print(f"{'='*80}")
    result = subprocess.run([
        sys.executable, 'ets_benchmark/src/anofox_ets.py', group
    ], check=False)

    if result.returncode != 0:
        print(f"\nERROR: Anofox baseline benchmark failed with code {result.returncode}")
        return result.returncode

    # Run Statsforecast ETS models
    print(f"\n{'='*80}")
    print(f"STEP 2/3: Running Statsforecast ETS Models")
    print(f"{'='*80}")
    result = subprocess.run([
        sys.executable, 'ets_benchmark/src/statsforecast_ets.py', group
    ], check=False)

    if result.returncode != 0:
        print(f"\nERROR: Statsforecast baseline benchmark failed with code {result.returncode}")
        return result.returncode

    # Run evaluation
    print(f"\n{'='*80}")
    print(f"STEP 3/3: Evaluating Results")
    print(f"{'='*80}")
    result = subprocess.run([
        sys.executable, 'ets_benchmark/src/evaluation_ets.py', group
    ], check=False)

    if result.returncode != 0:
        print(f"\nERROR: Evaluation failed with code {result.returncode}")
        return result.returncode

    print(f"\n{'='*80}")
    print(f"BASELINE BENCHMARK COMPLETE")
    print(f"{'='*80}")
    return 0


def anofox(group: str = 'Daily', model: str = None):
    """
    Run Anofox ETS models only.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    model : str, optional
        Specific ETS model to run. If None, runs all models.
        Options: 'SES', 'SESOptimized', 'SeasonalES', 'SeasonalESOptimized', 'Holt', 'HoltWinters', 'ETS', 'AutoETS'
    """
    print(f"Running Anofox ETS models on M4 {group}...")

    cmd = [sys.executable, 'ets_benchmark/src/anofox_ets.py', group]
    if model:
        cmd.extend(['--model', model])

    result = subprocess.run(cmd, check=False)
    return result.returncode


def statsforecast(group: str = 'Daily'):
    """
    Run Statsforecast ETS models only.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Running Statsforecast ETS models on M4 {group}...")

    result = subprocess.run([
        sys.executable, 'ets_benchmark/src/statsforecast_ets.py', group
    ], check=False)
    return result.returncode


def eval(group: str = 'Daily'):
    """
    Evaluate existing ETS model results.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Evaluating ETS model results for M4 {group}...")

    result = subprocess.run([
        sys.executable, 'ets_benchmark/src/evaluation_ets.py', group
    ], check=False)
    return result.returncode


def clean(group: str = None):
    """
    Clean benchmark results.

    Parameters
    ----------
    group : str, optional
        M4 frequency group to clean. If None, cleans all results.
    """
    results_dir = Path('ets_benchmark/results')

    if not results_dir.exists():
        print("No results directory found.")
        return

    if group:
        # Clean specific group
        pattern = f'*-{group}.parquet'
        files = list(results_dir.glob(pattern))
    else:
        # Clean all
        files = list(results_dir.glob('*.parquet'))

    if not files:
        print("No results files found to clean.")
        return

    print(f"Cleaning {len(files)} result files...")
    for file in files:
        file.unlink()
        print(f"  Deleted {file.name}")

    print("Clean complete.")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsforecast': statsforecast,
        'eval': eval,
        'clean': clean
    })
