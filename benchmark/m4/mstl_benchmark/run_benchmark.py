#!/usr/bin/env python3
"""
Main script to run MSTL benchmark suite.

Usage:
    # Run all models on Daily data
    python run_benchmark.py run --group=Daily

    # Run specific Anofox MSTL variant
    python run_benchmark.py anofox --group=Daily --model=MSTL

    # Run all Statsforecast MSTL variants
    python run_benchmark.py statsforecast --group=Daily

    # Evaluate results
    python run_benchmark.py eval --group=Daily

    # Clean results
    python run_benchmark.py clean
"""
import sys
from pathlib import Path
import shutil

import fire


def run(group: str = 'Daily'):
    """Run all MSTL benchmarks."""
    print(f"Running full MSTL benchmark suite for {group}...")

    # Run Anofox models
    print("\n" + "="*60)
    print("ANOFOX MSTL VARIANTS")
    print("="*60)
    import run as anofox_run
    anofox_run.run_anofox(group=group)

    # Run Statsforecast models
    print("\n" + "="*60)
    print("STATSFORECAST MSTL")
    print("="*60)
    import run_statsforecast
    run_statsforecast.run_benchmark(group=group)

    # Evaluate
    print("\n" + "="*60)
    print("EVALUATION")
    print("="*60)
    import run as eval_run
    eval_run.evaluate(group=group)

    print("\n✅ MSTL benchmark suite completed!")


def anofox(group: str = 'Daily', model: str = None):
    """Run Anofox MSTL models only."""
    import run as anofox_run
    anofox_run.run_anofox(group=group)


def statsforecast(group: str = 'Daily'):
    """Run Statsforecast MSTL models only."""
    import run_statsforecast
    run_statsforecast.run_benchmark(group=group)


def eval(group: str = 'Daily'):
    """Evaluate existing results."""
    import run as eval_run
    eval_run.evaluate(group=group)


def clean():
    """Clean all results."""
    results_dir = Path('results')
    if results_dir.exists():
        shutil.rmtree(results_dir)
        print(f"✅ Cleaned {results_dir}")
    else:
        print(f"Results directory {results_dir} does not exist")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsforecast': statsforecast,
        'eval': eval,
        'clean': clean,
    })

