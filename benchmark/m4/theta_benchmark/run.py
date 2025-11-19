"""
Theta models benchmark - configuration-driven wrapper.

Uses shared common modules and configuration files to run the Theta benchmark.
"""
import sys
from pathlib import Path

import fire

# Add benchmark root to sys.path to import shared modules
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from src.common.data import get_data
from src.common.anofox_runner import run_anofox_benchmark
from src.common.evaluation import evaluate_forecasts
from configs.theta import BENCHMARK_NAME, MODELS


def run_anofox(group: str = 'Daily', dataset: str = 'm4'):
    """
    Run Anofox Theta benchmarks on the selected dataset.

    Parameters
    ----------
    group : str
        Dataset frequency group: 'Daily', 'Hourly', or 'Weekly'
    dataset : str
        Dataset identifier (currently only 'm4')
    """
    dataset_key = dataset.lower()
    print(f"Loading {dataset.upper()} {group} data for {BENCHMARK_NAME} benchmark...")
    train_df, horizon, freq, seasonality = get_data(dataset_key, group, train=True)

    output_dir = Path(__file__).parent / 'results'

    run_anofox_benchmark(
        benchmark_name=BENCHMARK_NAME,
        train_df=train_df,
        horizon=horizon,
        seasonality=seasonality,
        models_config=MODELS,
        output_dir=output_dir,
        group=group
    )


def evaluate(group: str = 'Daily', dataset: str = 'm4'):
    """
    Evaluate Theta model forecasts on the selected dataset.

    Parameters
    ----------
    group : str
        Dataset frequency group: 'Daily', 'Hourly', or 'Weekly'
    dataset : str
        Dataset identifier (currently only 'm4')
    """
    # Load test and training data
    dataset_key = dataset.lower()
    test_df, horizon, freq, seasonality = get_data(dataset_key, group, train=False)
    train_df, _, _, _ = get_data(dataset_key, group, train=True)

    results_dir = Path(__file__).parent / 'results'

    evaluate_forecasts(
        benchmark_name=BENCHMARK_NAME,
        test_df_pd=test_df,
        train_df_pd=train_df,
        seasonality=seasonality,
        results_dir=results_dir,
        group=group
    )


def run(group: str = 'Daily', dataset: str = 'm4'):
    """
    Run complete Theta benchmark: anofox + evaluation.

    Parameters
    ----------
    group : str
        Dataset frequency group: 'Daily', 'Hourly', or 'Weekly'
    dataset : str
        Dataset identifier (currently only 'm4')
    """
    print(f"{'='*80}")
    print(f"THETA BENCHMARK - {dataset.upper()} {group}")
    print(f"{'='*80}\n")

    print(f"STEP 1: Running Anofox {BENCHMARK_NAME} models...")
    run_anofox(group, dataset)

    print(f"\nSTEP 2: Evaluating forecasts...")
    evaluate(group, dataset)

    print(f"\n{'='*80}")
    print(f"THETA BENCHMARK COMPLETE")
    print(f"{'='*80}")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': run_anofox,
        'evaluate': evaluate
    })
