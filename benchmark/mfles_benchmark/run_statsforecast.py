"""
Statsforecast MFLES benchmark - configuration-driven wrapper.

Uses shared common modules and configuration files to run statsforecast MFLES model.
"""
import sys
from pathlib import Path

import fire

# Add parent directory to path to import common modules
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.common.data import get_data
from src.common.statsforecast_runner import run_statsforecast_benchmark
from configs.statsforecast_mfles import (
    BENCHMARK_NAME,
    get_models_config,
    INCLUDE_PREDICTION_INTERVALS,
)


def run_benchmark(group: str = 'Daily'):
    """
    Run statsforecast MFLES benchmark on M4 dataset.

    Parameters
    ----------
    group : str
        M4 frequency group: 'Daily', 'Hourly', or 'Weekly'
    """
    print(f"Loading M4 {group} data for statsforecast {BENCHMARK_NAME} benchmark...")
    train_df, horizon, freq, seasonality = get_data('data', group, train=True)

    output_dir = Path(__file__).parent / 'results'

    # Get models configuration
    models_config = get_models_config(seasonality, horizon)

    run_statsforecast_benchmark(
        benchmark_name=BENCHMARK_NAME,
        train_df=train_df,
        horizon=horizon,
        freq=freq,
        seasonality=seasonality,
        models_config=models_config,
        output_dir=output_dir,
        group=group,
        include_prediction_intervals=INCLUDE_PREDICTION_INTERVALS,
    )


if __name__ == '__main__':
    fire.Fire(run_benchmark)
