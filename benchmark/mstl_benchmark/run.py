"""
MSTL models benchmark - configuration-driven wrapper.

Uses shared common modules and configuration files to run the MSTL benchmark.
"""
import sys
from pathlib import Path

import fire

# Add parent directory to path to import common modules
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.common.data import get_data
from src.common.anofox_runner import run_anofox_benchmark
from src.common.evaluation import evaluate_forecasts
from configs.mstl import BENCHMARK_NAME, MODELS


def run_anofox(group: str = 'Daily'):
    """
    Run Anofox MSTL benchmarks on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data for {BENCHMARK_NAME} benchmark...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

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


def evaluate(group: str = 'Daily'):
    """
    Evaluate MSTL model forecasts on M4 test data.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    # Load test and training data
    test_df, horizon, freq, seasonality = get_data('data', group, train=False)
    train_df, _, _, _ = get_data('data', group, train=True)

    results_dir = Path(__file__).parent / 'results'

    evaluate_forecasts(
        benchmark_name=BENCHMARK_NAME,
        test_df_pd=test_df,
        train_df_pd=train_df,
        seasonality=seasonality,
        results_dir=results_dir,
        group=group
    )


def run(group: str = 'Daily'):
    """
    Run complete MSTL benchmark: anofox + evaluation.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"{'='*80}")
    print(f"MSTL BENCHMARK - M4 {group}")
    print(f"{'='*80}\n")

    print(f"STEP 1: Running Anofox {BENCHMARK_NAME} models...")
    run_anofox(group)

    print(f"\nSTEP 2: Evaluating forecasts...")
    evaluate(group)

    print(f"\n{'='*80}")
    print(f"MSTL BENCHMARK COMPLETE")
    print(f"{'='*80}")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': run_anofox,
        'evaluate': evaluate
    })

