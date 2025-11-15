#!/usr/bin/env python3
"""
Main benchmark runner script.

Runs ARIMA benchmarks comparing anofox-forecast AutoARIMA with other implementations.
"""
import sys
import subprocess
from pathlib import Path

import fire


def run_model(model: str, group: str):
    """
    Run a single model benchmark.

    Parameters
    ----------
    model : str
        Model name: anofox, statsforecast, pmdarima
    group : str
        Dataset group: Daily, Hourly, Weekly
    """
    script = Path(__file__).parent / 'src' / f'{model}.py'

    if not script.exists():
        print(f"❌ Script not found: {script}")
        return False

    print(f"\n{'=' * 80}")
    print(f"Running {model} on M4 {group}")
    print(f"{'=' * 80}\n")

    try:
        # Use uv to run the script
        result = subprocess.run(
            ['uv', 'run', 'python', str(script), group],
            check=True,
            cwd=Path(__file__).parent.parent,
        )
        return result.returncode == 0
    except subprocess.CalledProcessError as e:
        print(f"❌ Error running {model}: {e}")
        return False


def evaluate(group: str):
    """
    Evaluate all models for a dataset group.

    Parameters
    ----------
    group : str
        Dataset group: Daily, Hourly, Weekly
    """
    eval_script = Path(__file__).parent / 'src' / 'evaluation.py'

    print(f"\n{'=' * 80}")
    print(f"Evaluating Results for M4 {group}")
    print(f"{'=' * 80}\n")

    try:
        subprocess.run(
            ['uv', 'run', 'python', str(eval_script), group],
            check=True,
            cwd=Path(__file__).parent.parent,
        )
    except subprocess.CalledProcessError as e:
        print(f"❌ Error during evaluation: {e}")


def run_all(group: str = 'Daily', models: str = 'anofox,statsforecast,pmdarima'):
    """
    Run full benchmark suite.

    Parameters
    ----------
    group : str
        Dataset group: Daily, Hourly, Weekly (default: Daily)
    models : str
        Comma-separated list of models to run (default: anofox,statsforecast,pmdarima)
    """
    model_list = [m.strip() for m in models.split(',')]

    print(f"\n{'#' * 80}")
    print(f"#  ARIMA Benchmark Suite")
    print(f"#  Dataset: M4 {group}")
    print(f"#  Models: {', '.join(model_list)}")
    print(f"{'#' * 80}\n")

    # Run each model
    success_count = 0
    for model in model_list:
        if run_model(model, group):
            success_count += 1
        else:
            print(f"⚠️  Skipping {model} due to errors")

    # Evaluate if at least one model succeeded
    if success_count > 0:
        evaluate(group)
        print(f"\n✅ Benchmark completed! {success_count}/{len(model_list)} models succeeded")
    else:
        print(f"\n❌ All models failed!")
        sys.exit(1)


def clean():
    """
    Clean all benchmark results and data.
    """
    results_dir = Path(__file__).parent / 'results'
    data_dir = Path(__file__).parent / 'data'

    if results_dir.exists():
        import shutil
        shutil.rmtree(results_dir)
        print(f"✅ Cleaned {results_dir}")

    if data_dir.exists():
        import shutil
        shutil.rmtree(data_dir)
        print(f"✅ Cleaned {data_dir}")


def main():
    """Main entry point using Fire."""
    fire.Fire({
        'run': run_all,
        'model': run_model,
        'eval': evaluate,
        'clean': clean,
    })


if __name__ == '__main__':
    main()
