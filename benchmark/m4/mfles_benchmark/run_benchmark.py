#!/usr/bin/env python3
"""
Main script to run MFLES benchmark suite.

Usage:
    # Run all MFLES models on Daily data
    python run_benchmark.py run --group=Daily

    # Run specific Anofox MFLES variant
    python run_benchmark.py anofox --group=Daily --model=MFLES-Fast

    # Check statsforecast MFLES availability
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
    """Run all MFLES benchmarks."""
    print(f"Running full MFLES benchmark suite for {group}...")

    # Run Anofox models
    print("\n" + "="*60)
    print("ANOFOX MFLES VARIANTS")
    print("="*60)
    import src.anofox_mfles
    src.anofox_mfles.run_benchmark(group=group)

    # Check statsforecast availability
    print("\n" + "="*60)
    print("STATSFORECAST MFLES")
    print("="*60)
    import src.statsforecast_mfles
    src.statsforecast_mfles.run_benchmark(group=group)

    # Evaluate
    print("\n" + "="*60)
    print("EVALUATION")
    print("="*60)
    import src.evaluation_mfles
    src.evaluation_mfles.evaluate(group=group)

    print("\n✅ MFLES benchmark suite completed!")


def anofox(group: str = 'Daily', model: str = None):
    """Run Anofox MFLES models only."""
    import src.anofox_mfles
    src.anofox_mfles.run_benchmark(group=group, model=model)


def statsforecast(group: str = 'Daily'):
    """Check statsforecast MFLES availability."""
    import src.statsforecast_mfles
    src.statsforecast_mfles.run_benchmark(group=group)


def eval(group: str = 'Daily'):
    """Evaluate existing results."""
    import src.evaluation_mfles
    src.evaluation_mfles.evaluate(group=group)


def clean():
    """Clean all results."""
    results_dir = Path('mfles_benchmark/results')
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
