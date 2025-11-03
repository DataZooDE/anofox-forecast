#!/usr/bin/env python3
"""
Main script to run Theta benchmark suite.

Usage:
    # Run all models on Daily data
    python run_benchmark.py run --group=Daily

    # Run specific Anofox Theta variant
    python run_benchmark.py anofox --group=Daily --model=OptimizedTheta

    # Run all Statsforecast Theta variants
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
    """Run all Theta benchmarks."""
    print(f"Running full Theta benchmark suite for {group}...")

    # Run Anofox models
    print("\n" + "="*60)
    print("ANOFOX THETA VARIANTS")
    print("="*60)
    import src.anofox_theta
    src.anofox_theta.run_benchmark(group=group)

    # Run Statsforecast models
    print("\n" + "="*60)
    print("STATSFORECAST THETA VARIANTS")
    print("="*60)
    import src.statsforecast_theta
    src.statsforecast_theta.run_benchmark(group=group)

    # Evaluate
    print("\n" + "="*60)
    print("EVALUATION")
    print("="*60)
    import src.evaluation_theta
    src.evaluation_theta.evaluate(group=group)

    print("\n✅ Theta benchmark suite completed!")


def anofox(group: str = 'Daily', model: str = None):
    """Run Anofox Theta models only."""
    import src.anofox_theta
    src.anofox_theta.run_benchmark(group=group, model=model)


def statsforecast(group: str = 'Daily'):
    """Run Statsforecast Theta models only."""
    import src.statsforecast_theta
    src.statsforecast_theta.run_benchmark(group=group)


def eval(group: str = 'Daily'):
    """Evaluate existing results."""
    import src.evaluation_theta
    src.evaluation_theta.evaluate(group=group)


def clean():
    """Clean all results."""
    results_dir = Path('theta_benchmark/results')
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
